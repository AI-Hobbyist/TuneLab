using System;
using System.Collections.Generic;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;
using System.Threading;
using TuneLab.Bridge;
using Xunit;

namespace TuneLab.Tests.Bridge;

[SupportedOSPlatform("windows")]
public class BridgeRendererTests
{
    [Fact]
    public void RestartsAfterRenderFailure()
    {
        using var plugin = FakePlugin.Create();
        using var client = new BridgeClient(plugin.SessionId);
        var provider = new ThrowOnceProvider();

        client.Connect();
        Assert.True(WaitFor(() => client.CurrentState == BridgeClient.State.Connected));

        var renderer = new BridgeRenderer(client, provider);
        try
        {
            renderer.Start();
            Assert.True(WaitFor(() => !renderer.IsRunning));
            int callsAfterFailure = provider.RenderCalls;

            renderer.Start();

            Assert.True(WaitFor(() => provider.RenderCalls > callsAfterFailure));
        }
        finally
        {
            renderer.Stop();
        }
    }

    static bool WaitFor(Func<bool> condition, int timeoutMs = 5000)
    {
        var deadline = Environment.TickCount64 + timeoutMs;
        while (Environment.TickCount64 < deadline)
        {
            if (condition())
                return true;
            Thread.Sleep(10);
        }
        return condition();
    }

    sealed class ThrowOnceProvider : IBridgeAudioProvider
    {
        public IReadOnlyList<BridgeTrack> GetTracks() => mTracks;

        public void UpdateTrackConfiguration(BridgeTrack track, bool enabled, int busIndex, bool followGainPan, bool mirrorMuteSolo) { }

        public void RenderTrack(BridgeTrack track, int position, int endPosition, float[] buffer, int offset)
        {
            RenderCalls++;
            if (mShouldThrow)
            {
                mShouldThrow = false;
                throw new InvalidOperationException("test render failure");
            }
        }

        public bool IsMute(BridgeTrack track) => false;
        public bool IsSolo(BridgeTrack track) => false;
        public bool HasSolo => false;
        public int EndTime(BridgeTrack track) => int.MaxValue;
        public bool ApplySampleRate(int sampleRate) => false;
        public void SetBridgeActive(bool active) { }
        public void SetTransportPlaying(bool playing) { }
        public void SetTransportSeek(double seconds) { }
        public void SetTransportTempo(double? bpm) { }

        public int RenderCalls;

        bool mShouldThrow = true;
        readonly BridgeTrack[] mTracks = [new BridgeTrack { Name = "Test", BusIndex = 0 }];
    }

    sealed class FakePlugin : IDisposable
    {
        public string SessionId { get; }

        public static FakePlugin Create()
            => new("TuneLab.Tests.Renderer." + Guid.NewGuid().ToString("N"));

        FakePlugin(string sessionId)
        {
            SessionId = sessionId;
            mMapping = MemoryMappedFile.CreateNew(BridgeProtocol.ShmName(sessionId), BridgeProtocol.TotalSize);
            mAccessor = mMapping.CreateViewAccessor(0, BridgeProtocol.TotalSize, MemoryMappedFileAccess.ReadWrite);
            mAccessor.Write(BridgeProtocol.Offset.Magic, BridgeProtocol.Magic);
            mAccessor.Write(BridgeProtocol.Offset.Version, BridgeProtocol.Version);
            mTimer = new Timer(_ => Tick(), null, 0, BridgeProtocol.HeartbeatMs);
        }

        void Tick()
        {
            try
            {
                mAccessor.Write(BridgeProtocol.Offset.PluginTick, ++mTick);
            }
            catch (ObjectDisposedException)
            {
            }
        }

        public void Dispose()
        {
            mTimer.Dispose();
            mAccessor.Dispose();
            mMapping.Dispose();
        }

        readonly MemoryMappedFile mMapping;
        readonly MemoryMappedViewAccessor mAccessor;
        readonly Timer mTimer;
        ulong mTick;
    }
}