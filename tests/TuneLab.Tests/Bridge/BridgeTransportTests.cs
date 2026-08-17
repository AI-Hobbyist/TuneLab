using System;
using System.Collections.Generic;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;
using TuneLab.Bridge;
using Xunit;

namespace TuneLab.Tests.Bridge;

// M2 传输同步（BridgeTransport）行为测试：进程内共享内存写入插件侧的传输字段，
// 断言渲染线程 Apply 后正确翻译成 IBridgeAudioProvider 回调（play/pause 边沿、位置跟随、曲速覆盖）。
[SupportedOSPlatform("windows")]
public class BridgeTransportTests : IDisposable
{
    public BridgeTransportTests()
    {
        mName = "TuneLab.Tests.Transport." + Guid.NewGuid().ToString("N");
        mMapping = MemoryMappedFile.CreateNew(mName, BridgeProtocol.TotalSize);
        mAccessor = mMapping.CreateViewAccessor(0, BridgeProtocol.TotalSize, MemoryMappedFileAccess.ReadWrite);
    }

    public void Dispose()
    {
        mAccessor.Dispose();
        mMapping.Dispose();
    }

    // 写插件侧传输状态字段（偏移与 C# 镜像一致，布局测试已覆盖）。
    void WriteState(ulong state) => mAccessor.Write(BridgeProtocol.Offset.State, state);
    void WriteSamplePos(ulong pos) => mAccessor.Write(BridgeProtocol.Offset.SamplePos, pos);
    void WriteTempo(double bpm) => mAccessor.Write(BridgeProtocol.Offset.Tempo, bpm);

    string mName = "";
    MemoryMappedFile? mMapping;
    MemoryMappedViewAccessor? mAccessor;

    [Fact]
    public void PlaybackEdge_TranslatesPlayPause()
    {
        var provider = new FakeProvider();
        var transport = new BridgeTransport();

        // 静止 → 播放：上升沿触发 SetTransportPlaying(true)。
        WriteState(BridgeProtocol.StatePlaying);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([true], provider.PlayingCalls);

        // 重复同一状态：不重复通知。
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([true], provider.PlayingCalls);

        // 播放 → 停止：下降沿触发 SetTransportPlaying(false)。
        WriteState(0);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([true, false], provider.PlayingCalls);
    }

    [Fact]
    public void PositionFollow_WhenPlaying_JumpsFollowed_SmallStepsThrottled()
    {
        var provider = new FakeProvider();
        var transport = new BridgeTransport();

        WriteState(BridgeProtocol.StatePlaying);
        WriteSamplePos(48000); // 1s @48k：从 0 起步算大跳变，立即跟随。
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([1.0], provider.SeekCalls);

        // 前进 0.1s（100ms 内、未达 0.25s 跳变阈值）：50ms 间隔未到，忽略。
        WriteSamplePos(48000 + 4800);
        transport.Apply(mAccessor, provider, 48000);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([1.0], provider.SeekCalls);

        // 跳到 2s（跳变 0.9s > 0.25s）：立即跟随最新位置。
        WriteSamplePos(48000 * 2);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([1.0, 2.0], provider.SeekCalls);
    }

    [Fact]
    public void PositionJump_WhenStoppedFollowsDrag()
    {
        var provider = new FakeProvider();
        var transport = new BridgeTransport();

        WriteState(0);
        WriteSamplePos(48000 * 5); // 停止时从 0 跳到 5s：跳变阈值内跟随。
        transport.Apply(mAccessor, provider, 48000);
        Assert.Single(provider.SeekCalls);
        Assert.Equal(5.0, provider.SeekCalls[0], 3);

        // 位置未再大跳：不再重复跟随。
        transport.Apply(mAccessor, provider, 48000);
        Assert.Single(provider.SeekCalls);
    }

    [Fact]
    public void TempoOverride_AppliesAndRetainsLastValid()
    {
        var provider = new FakeProvider();
        var transport = new BridgeTransport();

        WriteTempo(120.0);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([120.0], provider.TempoCalls);

        // 微小变化（0.1 < 0.5 阈值）：不重复触发。
        WriteTempo(120.1);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([120.0], provider.TempoCalls);

        // 明显变化：更新覆盖。
        WriteTempo(132.0);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([120.0, 132.0], provider.TempoCalls);

        // DAW 停止 / 暂不报曲速（0）：保持上次有效曲速，不回落工程曲速表。
        WriteTempo(0.0);
        transport.Apply(mAccessor, provider, 48000);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([120.0, 132.0], provider.TempoCalls);

        // 新曲速恢复上报：继续应用。
        WriteTempo(150.0);
        transport.Apply(mAccessor, provider, 48000);
        Assert.Equal([120.0, 132.0, 150.0], provider.TempoCalls);
    }

    sealed class FakeProvider : IBridgeAudioProvider
    {
        public List<bool> PlayingCalls { get; } = [];
        public List<double> SeekCalls { get; } = [];
        public List<double?> TempoCalls { get; } = [];

        public IReadOnlyList<BridgeTrack> GetTracks() => [];
        public void UpdateTrackConfiguration(BridgeTrack track, bool enabled, int busIndex, bool followGainPan, bool mirrorMuteSolo) { }
        public void RenderTrack(BridgeTrack track, int position, int endPosition, float[] buffer, int offset) { }
        public bool IsMute(BridgeTrack track) => false;
        public bool IsSolo(BridgeTrack track) => false;
        public bool HasSolo => false;
        public int EndTime(BridgeTrack track) => 0;
        public bool ApplySampleRate(int sampleRate) => false;
        public void SetBridgeActive(bool active) { }
        public void SetTransportPlaying(bool playing) => PlayingCalls.Add(playing);
        public void SetTransportSeek(double seconds) => SeekCalls.Add(seconds);
        public void SetTransportTempo(double? bpm) => TempoCalls.Add(bpm);
    }
}
