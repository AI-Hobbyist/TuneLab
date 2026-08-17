using System;
using System.Collections.Generic;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;
using System.Text;
using System.Threading;
using TuneLab.Foundation;

namespace TuneLab.Bridge;

// M1 宿主渲染线程：以 DAW 播放位置 + 提前量（push-ahead）为目标，把音轨实时渲染进每总线
// 环形缓冲（BridgeRingBuffer）。写者唯一（本线程），无锁。
// 生命周期：面板在连接态 Start()、断开态 Stop()。仅在 Connected 态渲染。
[SupportedOSPlatform("windows")]
internal sealed class BridgeRenderer
{
    public const int LeadMs = 200;              // push-ahead 提前量（毫秒）→ 同时上报为延迟
    const int RenderChunkFrames = 1024;         // 单块渲染帧数（staging 缓冲）
    const int SyncSleepMs = 5;

    public BridgeRenderer(BridgeClient client, IBridgeAudioProvider provider)
    {
        mClient = client;
        mProvider = provider;
    }

    public bool IsRunning => mThread is { IsAlive: true };

    public void Start()
    {
        if (mThread is { IsAlive: true })
            return;
        mCts?.Dispose();
        mCts = new CancellationTokenSource();
        mThread = new Thread(Loop) { IsBackground = true, Name = "TuneLab.Bridge.Renderer" };
        mThread.Start();
    }

    public void Stop()
    {
        mCts?.Cancel();
        if (mThread != null)
        {
            mThread.Join(500);
            mThread = null;
        }
        mCts?.Dispose();
        mCts = null;
    }

    void Loop()
    {
        var token = mCts!.Token;
        int sampleRate = 44100;
        int lead = LeadMs * sampleRate / 1000;
        var ring = new BridgeRingBuffer(mClient.Accessor!);
        var staging = new float[RenderChunkFrames * 2];
        IReadOnlyList<BridgeTrack>? lastTracks = null;

        while (!token.IsCancellationRequested)
        {
            if (mClient.CurrentState != BridgeClient.State.Connected)
            {
                Thread.Sleep(SyncSleepMs);
                continue;
            }

            try
            {
                RenderOnce(ring, staging, ref sampleRate, ref lead, ref lastTracks);
            }
            catch (ObjectDisposedException)
            {
                // 面板 Stop() 与断连竞态：accessor 已释放 → 退出线程（Stop 已汇合）。
                break;
            }
            catch (Exception ex)
            {
                // 后台渲染线程任何未处理异常都会终止进程：记录并停止渲染（比崩溃安全）。
                Log.Warning($"Bridge renderer stopped due to error: {ex}");
                break;
            }

            Thread.Sleep(SyncSleepMs);
        }
    }

    void RenderOnce(BridgeRingBuffer ring, float[] staging, ref int sampleRate, ref int lead, ref IReadOnlyList<BridgeTrack>? lastTracks)
    {
        if (mClient.CurrentState != BridgeClient.State.Connected)
            return;

        var accessor = mClient.Accessor!;

        ulong dawPos = accessor.ReadUInt64(BridgeProtocol.Offset.SamplePos);

        // 采样率协商：跟随插件写回的 DAW 采样率（prepareToPlay 后非 0）。
        // 采样率切换后，旧环数据对应的绝对位置已经不再可靠；从当前 DAW 位置重铺。
        uint dawSampleRate = accessor.ReadUInt32(BridgeProtocol.Offset.SampleRate);
        if (dawSampleRate != 0 && dawSampleRate != sampleRate && mProvider.ApplySampleRate((int)dawSampleRate))
        {
            sampleRate = (int)dawSampleRate;
            lead = LeadMs * sampleRate / 1000;
            ResetRingPositions(ring, dawPos);
            mIdleRefill = 0;
        }

        ulong target = dawPos + (ulong)lead;

        // M2 传输同步：DAW 播放/暂停/位置/曲速 → TuneLab（播放头跟随 + 时基覆盖）。
        mTransport.Apply(accessor, mProvider, sampleRate);

        var tracks = mProvider.GetTracks();

        // 轨道表变化时才重写控制块（名称/属性低频变更）。
        if (!ReferenceEquals(tracks, lastTracks))
        {
            WriteTrackTable(accessor, tracks);
            lastTracks = tracks;
        }

        // 总线映射：多轨可叠加到同一 bus（M1 默认 track[i]→bus[i]）。
        var buses = BuildBusMap(tracks);

        bool advanced = false;
        bool playing = (accessor.ReadUInt64(BridgeProtocol.Offset.State) & BridgeProtocol.StatePlaying) != 0;
        foreach (var kv in buses)
        {
            int bus = kv.Key;
            var list = kv.Value;
            ulong writePos = ring.GetWritePos(bus);

            // 环容量有限：写位与读位间距超过环容量时，读位所在旧数据已被回绕覆盖。
            // DAW 循环/回跳进该区域时不能复用旧数据——必须重置写位到 dawPos 重铺，否则
            // 插件读到错位/旧数据（症状："有声音但不连续"，每圈开头一段错乱）。
            ulong validStart = writePos > (ulong)BridgeProtocol.RingSamples ? writePos - (ulong)BridgeProtocol.RingSamples : 0;
            if (dawPos < validStart)
            {
                ring.SetWritePos(bus, dawPos);
                writePos = dawPos;
            }

            if (writePos >= target)
            {
                // 已达成/超前于目标（读位数据有效）。DAW 停止时周期性整环回填 [dawPos, writePos)：
                // 修复"连接/合成未就绪时把区域写成静音、写位越过后再不重写"的永久空洞
                // （症状：开头音符缺失/播放不全）。回填只重写数据不推进写位（updateWritePos:false），
                // 合成一完成，之前写成静音的区域即被补上。播放中不回填（避免与插件实时读竞态）。
                if (!playing && (++mIdleRefill & 0x1F) == 0)
                    RenderBus(ring, staging, list, bus, dawPos, writePos, updateWritePos: false);
                continue;
            }
            if (writePos < dawPos)
            {
                // DAW 跳到未渲染位置（播放/快进）：重置到 dawPos 重新铺满。
                ring.SetWritePos(bus, dawPos);
                writePos = dawPos;
            }
            RenderBus(ring, staging, list, bus, writePos, target, updateWritePos: true);
            advanced = true;
        }

        if (advanced)
            accessor.Write(BridgeProtocol.Offset.LatencySamples, (uint)lead);

        // 诊断（M1 音频链路）：每 ~1s 记录 DAW 位置 / 总线 0 写位 / 是否渲染 / 环数据峰值
        // （区分"宿主写了静音"与"宿主根本没写/插件没读"两个断点）。
        // peak = 写位前 4096 帧峰值（刚刚渲染了什么）；now = DAW 当前位置窗口峰值（插件即将读到什么）；
        // map16 = 全环 [0, writePos) 均分 16 窗口的峰值串，直接显示音符实际渲染在环里哪个位置。
        if ((++mDiagCount & 0x7F) == 0)
        {
            ulong w0 = ring.GetWritePos(0);
            float peak = PeakRange(accessor, 0, (long)w0 - 4096, (long)w0);
            float now = PeakRange(accessor, 0, (long)dawPos, (long)dawPos + 4096);
            double tempo = accessor.ReadDouble(BridgeProtocol.Offset.Tempo); // 插件上报的 DAW 曲速（诊断：停止/播放时是否持续有效）
            var sb = new StringBuilder(96);
            long span = (long)w0;
            long win = Math.Max(1, span / 16);
            for (int k = 0; k < 16; k++)
            {
                long s = k * win;
                long e = Math.Min(span, s + win);
                if (k > 0) sb.Append(',');
                sb.Append(PeakRange(accessor, 0, s, e).ToString("0.000"));
            }
            Foundation.Log.Info($"Bridge renderer: dawPos={dawPos} target={target} bus0Write={w0} advanced={advanced} tracks={tracks.Count} tempo={tempo:0.0} peak={peak:0.0000} now={now:0.0000} map16={sb}");
        }
    }

    internal static Dictionary<int, List<BridgeTrack>> BuildBusMap(IReadOnlyList<BridgeTrack> tracks)
    {
        var buses = new Dictionary<int, List<BridgeTrack>>();
        foreach (var track in tracks)
        {
            if (!track.Enabled || track.BusIndex < 0 || track.BusIndex >= BridgeProtocol.MaxTracks)
                continue;
            if (!buses.TryGetValue(track.BusIndex, out var list))
                buses[track.BusIndex] = list = new List<BridgeTrack>();
            list.Add(track);
        }
        return buses;
    }

    internal static void ResetRingPositions(BridgeRingBuffer ring, ulong position)
    {
        for (int bus = 0; bus < BridgeProtocol.MaxTracks; bus++)
            ring.SetWritePos(bus, position);
    }

    // 回读 bus 环数据 [start, end) 的峰值（仅诊断；读到有效数据即宿主已产出非静音）。
    static float PeakRange(MemoryMappedViewAccessor accessor, int bus, long start, long end)
    {
        if (start < 0)
            start = 0;
        if (end <= start)
            return 0;
        float peak = 0;
        long byteBase = BridgeProtocol.RingDataStart(bus);
        for (long i = start; i < end; i++)
        {
            float l = accessor.ReadSingle(byteBase + (i % BridgeProtocol.RingSamples) * 2 * sizeof(float));
            float r = accessor.ReadSingle(byteBase + (i % BridgeProtocol.RingSamples) * 2 * sizeof(float) + sizeof(float));
            float a = Math.Abs(l);
            if (a > peak) peak = a;
            a = Math.Abs(r);
            if (a > peak) peak = a;
        }
        return peak;
    }

    void RenderBus(BridgeRingBuffer ring, float[] staging, List<BridgeTrack> tracks, int bus, ulong startPos, ulong endPos, bool updateWritePos = true)
    {
        long pos = (long)startPos;
        long end = (long)endPos;
        while (pos < end)
        {
            int chunk = (int)Math.Min(RenderChunkFrames, end - pos);
            int chunkEnd = (int)pos + chunk;
            Array.Clear(staging, 0, chunk * 2);
            foreach (var t in tracks)
            {
                // mirrorMuteSolo：在 TuneLab 中静音/独奏的轨不进 DAW（其余场合仍整轨进）。
                if (t.MirrorMuteSolo && (mProvider.IsMute(t) || (mProvider.HasSolo && !mProvider.IsSolo(t))))
                    continue;
                int trackEnd = Math.Min(chunkEnd, mProvider.EndTime(t));
                mProvider.RenderTrack(t, (int)pos, trackEnd, staging, 0);
            }
            ring.Write(bus, pos, staging, 0, chunk);
            pos += chunk;
        }
        // 回填（updateWritePos:false）只重写数据，不推进写位——否则会把已渲染的读区写位拉回。
        if (updateWritePos)
            ring.SetWritePos(bus, (ulong)endPos);
    }

    void WriteTrackTable(MemoryMappedViewAccessor accessor, IReadOnlyList<BridgeTrack> tracks)
    {
        int count = Math.Min(tracks.Count, BridgeProtocol.MaxTracks);
        for (int i = 0; i < BridgeProtocol.MaxTracks; i++)
        {
            long start = BridgeProtocol.TrackStart(i);
            if (i < count)
            {
                var t = tracks[i];
                var nameBytes = Encoding.UTF8.GetBytes(t.Name);
                int n = Math.Min(nameBytes.Length, BridgeProtocol.TrackNameMax);
                accessor.WriteArray(start + BridgeProtocol.TrackOffset.Name, nameBytes, 0, n);
                for (int j = n; j < BridgeProtocol.TrackNameMax; j++)   // 名称区尾部清零
                    accessor.Write(start + BridgeProtocol.TrackOffset.Name + j, (byte)0);
                accessor.Write(start + BridgeProtocol.TrackOffset.Enabled, t.Enabled ? 1u : 0u);
                accessor.Write(start + BridgeProtocol.TrackOffset.BusIndex, (uint)t.BusIndex);
                accessor.Write(start + BridgeProtocol.TrackOffset.FollowGainPan, t.FollowGainPan ? 1u : 0u);
                accessor.Write(start + BridgeProtocol.TrackOffset.MirrorMuteSolo, t.MirrorMuteSolo ? 1u : 0u);
            }
            else
            {
                // 空槽位：清零（避免残留旧轨道）。
                for (int j = 0; j < BridgeProtocol.TrackNameMax; j++)
                    accessor.Write(start + BridgeProtocol.TrackOffset.Name + j, (byte)0);
                accessor.Write(start + BridgeProtocol.TrackOffset.Enabled, 0u);
                accessor.Write(start + BridgeProtocol.TrackOffset.BusIndex, 0u);
                accessor.Write(start + BridgeProtocol.TrackOffset.FollowGainPan, 0u);
                accessor.Write(start + BridgeProtocol.TrackOffset.MirrorMuteSolo, 0u);
            }
        }
    }

    readonly BridgeClient mClient;
    readonly IBridgeAudioProvider mProvider;
    readonly BridgeTransport mTransport = new();
    Thread? mThread;
    CancellationTokenSource? mCts;
    int mDiagCount;
    int mIdleRefill;
}
