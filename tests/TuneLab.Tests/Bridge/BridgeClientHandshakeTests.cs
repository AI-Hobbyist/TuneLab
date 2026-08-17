using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;
using System.Threading;
using TuneLab.Bridge;
using Xunit;

namespace TuneLab.Tests.Bridge;

// 宿主侧握手/心跳单测：用进程内 FakePlugin 模拟插件侧共享内存会话，
// 验证 BridgeClient 的连接生命周期（等待→连接、协议不符拒绝、心跳超时、断开）。
[SupportedOSPlatform("windows")]
public class BridgeClientHandshakeTests
{
    [Fact]
    public void ConnectsToPluginSession()
    {
        using var plugin = FakePlugin.Create();
        using var client = new BridgeClient(plugin.SessionId);

        client.Connect();
        Assert.True(WaitForState(client, BridgeClient.State.Connected), $"state={client.CurrentState} err={client.ErrorMessage}");

        Assert.True(plugin.IsConnected);
        Assert.Equal(BridgeClient.State.Connected, client.CurrentState);
    }

    [Fact]
    public void WaitsForPluginThenConnects()
    {
        var sessionId = "test-" + Guid.NewGuid().ToString("N");
        using var client = new BridgeClient(sessionId);

        client.Connect();
        // 插件尚未创建会话 → 保持"等待插件"。
        Thread.Sleep(100);
        Assert.Equal(BridgeClient.State.WaitingForPlugin, client.CurrentState);

        using var plugin = FakePlugin.Create(sessionId);
        Assert.True(WaitForState(client, BridgeClient.State.Connected), $"state={client.CurrentState} err={client.ErrorMessage}");
        Assert.True(plugin.IsConnected);
    }

    [Fact]
    public void RejectsVersionMismatch()
    {
        using var plugin = FakePlugin.Create(version: 999);
        using var client = new BridgeClient(plugin.SessionId);

        client.Connect();
        Assert.True(WaitForState(client, BridgeClient.State.Error), $"state={client.CurrentState}");
        Assert.Equal(BridgeClient.State.Error, client.CurrentState);
        Assert.Contains("version", client.ErrorMessage ?? string.Empty, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void RejectsMagicMismatch()
    {
        using var plugin = FakePlugin.Create(magic: 0xDEADBEEFu);
        using var client = new BridgeClient(plugin.SessionId);

        client.Connect();
        Assert.True(WaitForState(client, BridgeClient.State.Error), $"state={client.CurrentState}");
        Assert.Contains("magic", client.ErrorMessage ?? string.Empty, StringComparison.OrdinalIgnoreCase);
    }

    [Fact]
    public void DisconnectResetsConnection()
    {
        using var plugin = FakePlugin.Create();
        using var client = new BridgeClient(plugin.SessionId);

        client.Connect();
        Assert.True(WaitForState(client, BridgeClient.State.Connected));

        client.Disconnect();
        Assert.Equal(BridgeClient.State.Disconnected, client.CurrentState);
        Assert.False(plugin.IsConnected);
    }

    [Fact]
    public void DetectsPluginHeartbeatTimeout()
    {
        using var plugin = FakePlugin.Create();
        using var client = new BridgeClient(plugin.SessionId);

        client.Connect();
        Assert.True(WaitForState(client, BridgeClient.State.Connected));

        // 停止插件心跳 → 超过超时后宿主应自动断开。
        plugin.StopHeartbeat();
        Assert.True(WaitForState(client, BridgeClient.State.Disconnected, timeoutMs: BridgeProtocol.HeartbeatTimeoutMs + 3000),
            $"state={client.CurrentState} err={client.ErrorMessage}");
    }

    [Fact]
    public void ReconnectsAfterPluginRestart()
    {
        var sessionId = "test-" + Guid.NewGuid().ToString("N");
        using var client = new BridgeClient(sessionId);

        using (var plugin = FakePlugin.Create(sessionId))
        {
            client.Connect();
            Assert.True(WaitForState(client, BridgeClient.State.Connected));
            plugin.StopHeartbeat();
        }

        Assert.True(WaitForState(client, BridgeClient.State.Disconnected,
            timeoutMs: BridgeProtocol.HeartbeatTimeoutMs + 3000));

        using var restartedPlugin = FakePlugin.Create(sessionId);
        Assert.True(WaitForState(client, BridgeClient.State.Connected,
            timeoutMs: BridgeProtocol.HeartbeatTimeoutMs + 3000),
            $"state={client.CurrentState} err={client.ErrorMessage}");
        Assert.True(restartedPlugin.IsConnected);
    }

    static bool WaitForState(BridgeClient client, BridgeClient.State state, int timeoutMs = 5000)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (Environment.TickCount64 < deadline)
        {
            if (client.CurrentState == state)
                return true;
            Thread.Sleep(10);
        }
        return client.CurrentState == state;
    }

    // 进程内模拟插件侧：创建共享内存会话 + 心跳（递增 pluginTick）。
    sealed class FakePlugin : IDisposable
    {
        public string SessionId { get; }
        public bool IsConnected => mAccessor.ReadUInt32(BridgeProtocol.Offset.Connected) != 0;

        public static FakePlugin Create(string? sessionId = null, uint version = BridgeProtocol.Version, uint magic = BridgeProtocol.Magic)
            => new(sessionId ?? "test-" + Guid.NewGuid().ToString("N"), version, magic);

        FakePlugin(string sessionId, uint version, uint magic)
        {
            SessionId = sessionId;
            mMapping = MemoryMappedFile.CreateNew(BridgeProtocol.ShmName(sessionId), BridgeProtocol.TotalSize);
            mAccessor = mMapping.CreateViewAccessor(0, BridgeProtocol.TotalSize, MemoryMappedFileAccess.ReadWrite);
            mAccessor.Write(BridgeProtocol.Offset.Magic, magic);
            mAccessor.Write(BridgeProtocol.Offset.Version, version);
            mAccessor.Write(BridgeProtocol.Offset.Connected, (uint)0);
            mAccessor.Write(BridgeProtocol.Offset.PluginPid, (uint)Environment.ProcessId);
            mTimer = new Timer(_ => Tick(), null, 0, BridgeProtocol.HeartbeatMs);
        }

        void Tick()
        {
            mTick++;
            try { mAccessor.Write(BridgeProtocol.Offset.PluginTick, mTick); }
            catch { }
        }

        public void StopHeartbeat() => mTimer?.Dispose();

        public void Dispose()
        {
            mTimer?.Dispose();
            mAccessor?.Dispose();
            mMapping?.Dispose();
        }

        readonly MemoryMappedFile mMapping;
        readonly MemoryMappedViewAccessor mAccessor;
        Timer? mTimer;
        ulong mTick;
    }
}
