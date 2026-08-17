using System;
using System.Collections.Generic;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading.Tasks;
using Avalonia.Threading;
using TuneLab.Bridge;
using TuneLab.Data;

namespace TuneLab.Audio;

// IBridgeAudioProvider 的 TuneLab 实现：把 BridgeRenderer 的请求翻译成对 AudioEngine/AudioGraph
// 的调用（音轨快照、逐轨渲染、静音/独奏查询、采样率跟随）。
internal sealed class AudioBridgeProvider : IBridgeAudioProvider
{
    public static readonly AudioBridgeProvider Instance = new();

    public IReadOnlyList<BridgeTrack> GetTracks()
    {
        var tracks = AudioEngine.GetTracksSnapshot();
        string signature = BuildSignature(tracks);
        lock (mConfigurationLock)
        {
            if (mCached != null && mCachedCount == tracks.Length && mCachedSignature == signature)
                return mCached;

            var list = new BridgeTrack[tracks.Length];
            for (int i = 0; i < tracks.Length; i++)
            {
                var source = tracks[i];
                if (!mConfigurations.TryGetValue(source, out var configuration))
                {
                    configuration = new(true, i, true, true);
                    mConfigurations[source] = configuration;
                }

                list[i] = new BridgeTrack
                {
                    Name = ((ITrack)source).Name.Value,
                    Enabled = configuration.Enabled,
                    BusIndex = configuration.BusIndex,
                    FollowGainPan = configuration.FollowGainPan,
                    MirrorMuteSolo = configuration.MirrorMuteSolo,
                    Source = source,
                };
            }

            mCached = list;
            mCachedCount = tracks.Length;
            mCachedSignature = signature;
            return mCached;
        }
    }

    public void UpdateTrackConfiguration(BridgeTrack track, bool enabled, int busIndex, bool followGainPan, bool mirrorMuteSolo)
    {
        if (track.Source == null)
            return;

        lock (mConfigurationLock)
        {
            mConfigurations[track.Source] = new(
                enabled,
                Math.Clamp(busIndex, 0, BridgeTrack.MaxBusCount - 1),
                followGainPan,
                mirrorMuteSolo);
            mCached = null;
        }
    }

    public void RenderTrack(BridgeTrack track, int position, int endPosition, float[] buffer, int offset)
    {
        if (track.Source is not IAudioTrack it || position >= endPosition)
            return;

        if (track.FollowGainPan)
        {
            AudioEngine.MixBridgeData(it, position, endPosition, buffer, offset);
        }
        else
        {
            // raw：不含音量/声像，直接叠加各源（与 AudioGraph.AddData 同构但去掉 volume/pan）。
            int sampleRate = AudioEngine.SampleRate.Value;
            foreach (var s in it.AudioSources)
            {
                int sourceStart = (int)(s.StartTime * sampleRate);
                int sourceEnd = sourceStart + s.SampleCount;
                if (sourceEnd < position)
                    continue;
                if (sourceStart > endPosition)
                    break;

                int start = Math.Max(position, sourceStart);
                int end = Math.Min(endPosition, sourceEnd);
                if (start == end)
                    continue;

                var data = s.GetAudioData(start - sourceStart, end - start);
                for (int i = start; i < end; i++)
                {
                    buffer[2 * (i - position) + offset] += data.GetLeft(i - start);
                    buffer[2 * (i - position) + offset + 1] += data.GetRight(i - start);
                }
            }
        }
    }

    public bool IsMute(BridgeTrack track) => track.Source is IAudioTrack it && it.IsMute;
    public bool IsSolo(BridgeTrack track) => track.Source is IAudioTrack it && it.IsSolo;
    public bool HasSolo => AudioEngine.GetTracksSnapshot().Any(t => t.IsSolo);

    public int EndTime(BridgeTrack track)
    {
        if (track.Source is not IAudioTrack it)
            return 0;
        return (int)(it.EndTime * AudioEngine.SampleRate.Value);
    }

    public bool ApplySampleRate(int sampleRate)
    {
        if (sampleRate <= 0 || AudioEngine.SampleRate.Value == sampleRate)
            return false;

        // 采样率变更会同步触发整链重合成 + UI 重绘（EffectGraph.Schedule 同步跑 + mOnChanged），
        // 必须在 UI 线程执行；渲染线程只发请求（Post 非阻塞，避免与面板 Stop 交叉死锁）。
        Dispatcher.UIThread.Post(() =>
        {
            if (AudioEngine.SampleRate.Value != sampleRate)
                AudioEngine.SampleRate.Value = sampleRate;
        });
        return true;
    }

    public void SetBridgeActive(bool active) => AudioEngine.BridgeMode = active;

    // —— M2 传输同步（DAW 为 master，TuneLab 跟随）——
    // 传输回调来自渲染（后台）线程：播放/暂停/位置/时基都会触发 UI 事件或重合成，
    // 一律切到 UI 线程执行（Post 非阻塞，与 ApplySampleRate 同范式）。

    public void SetTransportPlaying(bool playing)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (playing)
                AudioEngine.Play();
            else
                AudioEngine.Pause();
        });
    }

    public void SetTransportSeek(double seconds)
    {
        // 播放头位置跟随：AudioEngine.Seek 触发 ProgressChanged → PlayheadForProject 光标同步。
        Dispatcher.UIThread.Post(() => AudioEngine.Seek(seconds));
    }

    public void SetTransportTempo(double? bpm)
    {
        // 会话时基覆盖挂到当前工程的 TempoManager：任一图内轨的 TempoManager 即工程曲速表
        // （多轨共享同一 Project 实例）；无轨（空工程）则跳过覆盖。
        Dispatcher.UIThread.Post(() =>
        {
            foreach (var t in AudioEngine.GetTracksSnapshot())
            {
                if (t is Track track && track.TempoManager is TempoManager tempoManager)
                {
                    tempoManager.SetTimebaseOverride(bpm);
                    return;
                }
            }
        });
    }

    static string BuildSignature(IAudioTrack[] tracks)
    {
        var sb = new StringBuilder();
        for (int i = 0; i < tracks.Length; i++)
            sb.Append(RuntimeHelpers.GetHashCode(tracks[i])).Append(':')
                .Append(((ITrack)tracks[i]).Name.Value).Append('\u001f');
        return sb.ToString();
    }

    readonly object mConfigurationLock = new();
    readonly Dictionary<object, TrackConfiguration> mConfigurations = new(ReferenceEqualityComparer.Instance);
    BridgeTrack[]? mCached;
    int mCachedCount;
    string mCachedSignature = "";

    readonly record struct TrackConfiguration(bool Enabled, int BusIndex, bool FollowGainPan, bool MirrorMuteSolo);
}
