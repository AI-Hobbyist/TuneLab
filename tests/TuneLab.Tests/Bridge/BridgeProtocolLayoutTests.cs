using System;
using System.IO;
using System.Text.RegularExpressions;
using TuneLab.Bridge;
using Xunit;

namespace TuneLab.Tests.Bridge;

// 布局一致性测试：对照 Bridge/protocol/TLBridgeProtocol.h（唯一真源）的
// TL_BRIDGE_OFF_* / TL_BRIDGE_TRACK_OFF_* / 常量宏，守护 C# 镜像（BridgeProtocol）不漂移。
public class BridgeProtocolLayoutTests
{
    [Fact]
    public void ControlOffsetsMatchHeader()
    {
        var header = ReadHeader();

        AssertMacroUInt(header, "TL_BRIDGE_OFF_MAGIC", BridgeProtocol.Offset.Magic);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_VERSION", BridgeProtocol.Offset.Version);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_CONNECTED", BridgeProtocol.Offset.Connected);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_PROTOCOL_ERROR", BridgeProtocol.Offset.ProtocolError);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_SAMPLE_POS", BridgeProtocol.Offset.SamplePos);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_STATE", BridgeProtocol.Offset.State);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_TEMPO", BridgeProtocol.Offset.Tempo);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_TIME_SIG_NUM", BridgeProtocol.Offset.TimeSigNum);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_TIME_SIG_DEN", BridgeProtocol.Offset.TimeSigDen);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_PPQ_POSITION", BridgeProtocol.Offset.PpqPosition);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_PPQ_BAR_START", BridgeProtocol.Offset.PpqOfLastBarStart);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_SAMPLE_RATE", BridgeProtocol.Offset.SampleRate);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_BLOCK_SIZE", BridgeProtocol.Offset.BlockSize);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_ACTIVE_BUSES", BridgeProtocol.Offset.ActiveBuses);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_LATENCY_SAMPLES", BridgeProtocol.Offset.LatencySamples);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_TRACKS", BridgeProtocol.Offset.Tracks);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_HOST_TICK", BridgeProtocol.Offset.HostTick);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_PLUGIN_TICK", BridgeProtocol.Offset.PluginTick);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_SESSION_NAME", BridgeProtocol.Offset.SessionName);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_HOST_PID", BridgeProtocol.Offset.HostPid);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_PLUGIN_PID", BridgeProtocol.Offset.PluginPid);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_HOST_APP_VERSION", BridgeProtocol.Offset.HostAppVersion);
        AssertMacroUInt(header, "TL_BRIDGE_OFF_RESERVED", BridgeProtocol.Offset.Reserved);
        AssertMacroUInt(header, "TL_BRIDGE_CONTROL_SIZE", BridgeProtocol.Offset.ControlSize);
    }

    [Fact]
    public void TrackOffsetsMatchHeader()
    {
        var header = ReadHeader();

        AssertMacroUInt(header, "TL_BRIDGE_TRACK_SIZE", BridgeProtocol.Offset.TrackSize);
        AssertMacroUInt(header, "TL_BRIDGE_TRACK_OFF_NAME", BridgeProtocol.TrackOffset.Name);
        AssertMacroUInt(header, "TL_BRIDGE_TRACK_OFF_ENABLED", BridgeProtocol.TrackOffset.Enabled);
        AssertMacroUInt(header, "TL_BRIDGE_TRACK_OFF_BUS_INDEX", BridgeProtocol.TrackOffset.BusIndex);
        AssertMacroUInt(header, "TL_BRIDGE_TRACK_OFF_FOLLOW_GAIN_PAN", BridgeProtocol.TrackOffset.FollowGainPan);
        AssertMacroUInt(header, "TL_BRIDGE_TRACK_OFF_MIRROR_MUTE_SOLO", BridgeProtocol.TrackOffset.MirrorMuteSolo);
    }

    [Fact]
    public void ConstantsMatchHeader()
    {
        var header = ReadHeader();

        AssertMacroUInt(header, "TL_BRIDGE_MAGIC", (int)BridgeProtocol.Magic);
        AssertMacroUInt(header, "TL_BRIDGE_VERSION", (int)BridgeProtocol.Version);
        AssertMacroUInt(header, "TL_BRIDGE_MAX_TRACKS", BridgeProtocol.MaxTracks);
        AssertMacroUInt(header, "TL_BRIDGE_SESSION_NAME_MAX", BridgeProtocol.SessionNameMax);
        AssertMacroUInt(header, "TL_BRIDGE_TRACK_NAME_MAX", BridgeProtocol.TrackNameMax);
        AssertMacroUInt(header, "TL_BRIDGE_HEARTBEAT_MS", BridgeProtocol.HeartbeatMs);
        AssertMacroUInt(header, "TL_BRIDGE_HEARTBEAT_TIMEOUT_MS", BridgeProtocol.HeartbeatTimeoutMs);
        AssertMacroUInt(header, "TL_BRIDGE_STATE_PLAYING", (int)BridgeProtocol.StatePlaying);
        AssertMacroUInt(header, "TL_BRIDGE_STATE_LOOPING", (int)BridgeProtocol.StateLooping);
    }

    [Fact]
    public void RingOffsetsMatchHeader()
    {
        var header = ReadHeader();

        AssertMacroUInt(header, "TL_BRIDGE_RING_SAMPLES", BridgeProtocol.RingSamples);
        AssertMacroUInt(header, "TL_BRIDGE_RING_SIZE", BridgeProtocol.RingSize);
        AssertMacroUInt(header, "TL_BRIDGE_RING_OFF_WRITE_POS", BridgeProtocol.RingOffWritePos);
        AssertMacroUInt(header, "TL_BRIDGE_RING_OFF_READ_POS", BridgeProtocol.RingOffReadPos);
        AssertMacroUInt(header, "TL_BRIDGE_RING_OFF_UNDERFLOW", BridgeProtocol.RingOffUnderflow);

        // 派生布局（C# 与头文件 TL_BRIDGE_RING_STATE_OFF / TL_BRIDGE_RING_DATA_OFF / TL_BRIDGE_TOTAL_SIZE 同公式）。
        Assert.Equal(BridgeProtocol.Offset.ControlSize, BridgeProtocol.RingStateStart(0));
        Assert.Equal(BridgeProtocol.Offset.ControlSize + BridgeProtocol.MaxTracks * BridgeProtocol.RingSize, BridgeProtocol.RingDataBase);
        Assert.Equal(BridgeProtocol.RingDataBase + BridgeProtocol.RingDataTotalSamples * sizeof(float), BridgeProtocol.TotalSize);
        // 总线 1 的环状态起点 = 总线 0 + RingSize（不重叠）。
        Assert.Equal(BridgeProtocol.RingStateStart(0) + BridgeProtocol.RingSize, BridgeProtocol.RingStateStart(1));
        // 总线 1 数据区起点 = 总线 0 + RingSamples*2*sizeof(float)（不重叠）。
        Assert.Equal(BridgeProtocol.RingDataStart(0) + BridgeProtocol.RingSamples * 2 * sizeof(float), BridgeProtocol.RingDataStart(1));
    }

    static void AssertMacroUInt(string header, string macro, int expected)
    {
        var match = Regex.Match(header, $@"#define\s+{macro}\s+((?:0[xX][0-9a-fA-F]+)|\d+)u?");
        Assert.True(match.Success, $"Missing or unparseable macro: {macro}");
        var raw = match.Groups[1].Value;
        int actual = raw.StartsWith("0x", StringComparison.OrdinalIgnoreCase)
            ? Convert.ToInt32(raw, 16)
            : int.Parse(raw);
        Assert.Equal(expected, actual);
    }

    static string ReadHeader()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null)
        {
            var candidate = Path.Combine(dir.FullName, "Bridge", "protocol", "TLBridgeProtocol.h");
            if (File.Exists(candidate))
                return File.ReadAllText(candidate);
            dir = dir.Parent;
        }

        throw new FileNotFoundException("TLBridgeProtocol.h not found (walking up from test output)");
    }
}
