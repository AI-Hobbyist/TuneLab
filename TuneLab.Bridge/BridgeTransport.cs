using System;
using System.IO.MemoryMappedFiles;
using System.Runtime.Versioning;

namespace TuneLab.Bridge;

// M2 传输同步：把控制块里插件写回的 DAW 传输状态翻译成 IBridgeAudioProvider 回调
// （play/pause 边沿、播放头位置跟随、曲速时基覆盖）。在渲染线程每轮 Apply；
// 回调内部自行切 UI 线程，本类只做无锁读取与节流判据。
[SupportedOSPlatform("windows")]
internal sealed class BridgeTransport
{
    public const int SeekIntervalMs = 50;                 // 播放中平滑跟随间隔（毫秒）
    public const double SeekJumpThresholdSeconds = 0.25;  // 位置跳变阈值（秒）：拖动/起步/连上对齐
    public const double TempoEpsilon = 0.5;               // 曲速变化触发阈值（BPM，防抖动）

    public void Apply(MemoryMappedViewAccessor accessor, IBridgeAudioProvider provider, int sampleRate)
    {
        // 播放/暂停：仅在状态边沿变化时透传（避免每轮重复通知）。
        ulong stateBits = accessor.ReadUInt64(BridgeProtocol.Offset.State);
        bool playing = (stateBits & BridgeProtocol.StatePlaying) != 0;
        if (playing != mLastPlaying)
        {
            mLastPlaying = playing;
            provider.SetTransportPlaying(playing);
        }

        // 播放头位置跟随：秒 = samplePos / sampleRate。播放中按间隔平滑推进；
        // 停止时只在明显跳变（DAW 拖动播放头 / 连接对齐）时跟随。
        ulong samplePos = accessor.ReadUInt64(BridgeProtocol.Offset.SamplePos);
        if (sampleRate > 0)
        {
            double seconds = (double)samplePos / sampleRate;
            double delta = Math.Abs(seconds - mLastSeekSeconds);
            long now = Environment.TickCount64;
            if (playing)
            {
                if (delta > SeekJumpThresholdSeconds || now - mLastSeekTick >= SeekIntervalMs)
                {
                    mLastSeekTick = now;
                    provider.SetTransportSeek(seconds);
                }
            }
            else if (delta > SeekJumpThresholdSeconds)
            {
                provider.SetTransportSeek(seconds);
            }
            mLastSeekSeconds = seconds;
        }

        // 曲速时基覆盖：tempo > 0 视为 DAW 当前曲速，超过阈值去抖后应用。DAW 停止/传输边界
        // 会短暂不报曲速（tempo=0），此时【保持】上次有效曲速而不清空——否则 TuneLab 瞬间回落
        // 到工程曲速表，造成"曲速与 DAW 不同步"并反复重建合成。覆盖的清除在断连时统一处理
        // （BridgePanel.StopRenderer → SetTransportTempo(null)）。
        double tempo = accessor.ReadDouble(BridgeProtocol.Offset.Tempo);
        if (tempo > 0 && Math.Abs(tempo - mLastTempo) > TempoEpsilon)
        {
            mLastTempo = tempo;
            provider.SetTransportTempo(tempo);
        }
    }

    bool mLastPlaying;
    double mLastSeekSeconds;
    long mLastSeekTick;
    double mLastTempo;
}
