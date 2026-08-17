using System;
using System.IO;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;
using System.Text;
using System.Threading;
using TuneLab.Foundation;

namespace TuneLab.Bridge;

// 宿主侧 VST 桥会话客户端：打开插件（Bridge_VST3）创建的共享内存会话，握手（魔数/版本校验）、
// 心跳保活、手动连接/断开。M0 只负责连接生命周期与协议校验；传输/音频在后续里程碑接入。
//
// 线程模型：Connect/Disconnect 在 UI 线程；心跳由 Timer 在 ThreadPool 触发，状态变更经
// StateChanged 事件广播（UI 侧需自行切回 UI 线程）。
[SupportedOSPlatform("windows")]
internal sealed class BridgeClient : IDisposable
{
    public enum State { Disconnected, WaitingForPlugin, Connected, Error }

    public string SessionId { get; }
    public uint HostAppVersion { get; set; }

    public State CurrentState { get; private set; } = State.Disconnected;
    public string? ErrorMessage { get; private set; }

    // 共享内存视图访问器（连接后非空）：渲染线程/环形缓冲经此读写控制块与音频环。
    public MemoryMappedViewAccessor? Accessor => mAccessor;

    // 状态变更回调（在心跳线程触发；UI 侧需自行切回 UI 线程）。
    public event Action? StateChanged;

    public BridgeClient(string sessionId)
    {
        SessionId = sessionId;
    }

    public void Connect()
    {
        lock (mLock)
        {
            if (mWantedConnect)
                return;

            mWantedConnect = true;
            ErrorMessage = null;
            SetState(State.WaitingForPlugin);
            StartTimer();
        }
    }

    public void Disconnect()
    {
        lock (mLock)
        {
            mWantedConnect = false;
            StopTimer();
            ReleaseSession();
            SetState(State.Disconnected);
        }
    }

    public void Dispose() => Disconnect();

    // 定时轮询：未打开时尝试打开（等待插件），已打开时做心跳/超时检测。
    void Poll()
    {
        lock (mLock)
        {
            if (!mWantedConnect)
                return;

            if (mMapping == null)
            {
                if (!TryOpenSession())
                    return;
            }

            if (!CheckPluginAlive())
            {
                ReleaseSession();
                ErrorMessage = "Plugin heartbeat timeout";
                SetState(State.Disconnected);
                return;
            }

            WriteHostTick();
        }
    }

    // 打开插件创建的映射并握手。返回 false 表示尚未就绪（继续轮询）或已置 Error（停止轮询）。
    bool TryOpenSession()
    {
        MemoryMappedFile? mapping = null;
        MemoryMappedViewAccessor? accessor = null;
        try
        {
            mapping = MemoryMappedFile.OpenExisting(BridgeProtocol.ShmName(SessionId), MemoryMappedFileRights.ReadWrite);
            accessor = mapping.CreateViewAccessor(0, BridgeProtocol.TotalSize, MemoryMappedFileAccess.ReadWrite);

            uint magic = accessor.ReadUInt32(BridgeProtocol.Offset.Magic);
            uint version = accessor.ReadUInt32(BridgeProtocol.Offset.Version);
            if (magic != BridgeProtocol.Magic)
            {
                Fail(accessor, mapping, "Protocol magic mismatch", BridgeProtocol.ErrorMagicMismatch);
                return false;
            }
            if (version != BridgeProtocol.Version)
            {
                Fail(accessor, mapping, $"Protocol version mismatch (expected {BridgeProtocol.Version}, got {version})", BridgeProtocol.ErrorVersionMismatch);
                return false;
            }

            // 已被其他宿主占用：除非插件已死（pluginTick 停滞为 0），否则拒绝连接。
            uint connected = accessor.ReadUInt32(BridgeProtocol.Offset.Connected);
            ulong pluginTick = accessor.ReadUInt64(BridgeProtocol.Offset.PluginTick);
            if (connected != 0 && pluginTick != 0)
            {
                Fail(accessor, mapping, "Session is busy", BridgeProtocol.ErrorBusy);
                return false;
            }

            mMapping = mapping;
            mAccessor = accessor;
            mLastPluginTick = pluginTick;
            mStaleTickMs = 0;

            // 握手：登记宿主身份并置 connected。
            WriteHostTick();
            accessor.Write(BridgeProtocol.Offset.Connected, (uint)1);
            accessor.Write(BridgeProtocol.Offset.HostPid, (uint)Environment.ProcessId);
            accessor.Write(BridgeProtocol.Offset.HostAppVersion, HostAppVersion);
            WriteUtf8(accessor, BridgeProtocol.Offset.SessionName, BridgeProtocol.SessionNameMax, SessionId);

            SetState(State.Connected);
            return true;
        }
        catch (FileNotFoundException)
        {
            // 插件尚未创建会话：保持"等待插件"，下轮再试。
            accessor?.Dispose();
            mapping?.Dispose();
            return false;
        }
        catch (Exception ex)
        {
            Log.Warning($"Bridge connect error: {ex}");
            accessor?.Dispose();
            mapping?.Dispose();
            ErrorMessage = ex.Message;
            StopTimer();
            SetState(State.Error);
            return false;
        }
    }

    // 协议不符：写入错误码、清理、置 Error 并停止轮询（用户须手动重连）。
    void Fail(MemoryMappedViewAccessor accessor, MemoryMappedFile mapping, string message, uint errorCode)
    {
        try
        {
            accessor.Write(BridgeProtocol.Offset.ProtocolError, errorCode);
        }
        catch { }
        accessor.Dispose();
        mapping.Dispose();
        ErrorMessage = message;
        StopTimer();
        SetState(State.Error);
    }

    bool CheckPluginAlive()
    {
        ulong tick = mAccessor!.ReadUInt64(BridgeProtocol.Offset.PluginTick);
        if (tick != mLastPluginTick)
        {
            mLastPluginTick = tick;
            mStaleTickMs = 0;
            return true;
        }

        mStaleTickMs += BridgeProtocol.HeartbeatMs;
        return mStaleTickMs < BridgeProtocol.HeartbeatTimeoutMs;
    }

    void WriteHostTick()
    {
        if (mAccessor == null)
            return;

        mHostTick++;
        mAccessor.Write(BridgeProtocol.Offset.HostTick, mHostTick);
    }

    void ReleaseSession()
    {
        if (mAccessor != null && mMapping != null)
        {
            try
            {
                mAccessor.Write(BridgeProtocol.Offset.Connected, (uint)0);
                mAccessor.Write(BridgeProtocol.Offset.ProtocolError, BridgeProtocol.ErrorNone);
            }
            catch { }
        }

        mAccessor?.Dispose();
        mAccessor = null;
        mMapping?.Dispose();
        mMapping = null;
    }

    void StartTimer()
    {
        mTimer?.Dispose();
        mTimer = new Timer(_ => Poll(), null, 0, BridgeProtocol.HeartbeatMs);
    }

    void StopTimer()
    {
        mTimer?.Dispose();
        mTimer = null;
    }

    void SetState(State state)
    {
        if (CurrentState == state)
            return;

        CurrentState = state;
        StateChanged?.Invoke();
    }

    static void WriteUtf8(MemoryMappedViewAccessor accessor, int offset, int maxLength, string text)
    {
        var bytes = Encoding.UTF8.GetBytes(text);
        int count = Math.Min(bytes.Length, maxLength - 1);
        accessor.WriteArray(offset, bytes, 0, count);
        accessor.Write(offset + count, (byte)0);
    }

    MemoryMappedFile? mMapping;
    MemoryMappedViewAccessor? mAccessor;
    Timer? mTimer;
    readonly object mLock = new();
    bool mWantedConnect;
    ulong mHostTick;
    ulong mLastPluginTick;
    int mStaleTickMs;
}
