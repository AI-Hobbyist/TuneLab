using System.Collections.Generic;

namespace TuneLab.Bridge;

// TuneLab 侧实现的音频提供者契约。本程序集（TuneLab.Bridge）被 TuneLab 引用，不能反向引用
// TuneLab；因此把"读取音轨音频/属性、应用采样率"的职责下放给实现方（见 TuneLab/Audio/AudioBridgeProvider.cs）。
// M1 渲染线程（BridgeRenderer）只做总线级编排，不做任何音频数据搬运细节。
public interface IBridgeAudioProvider
{
    // 当前参与桥接的音轨快照（宿主每帧据此刷新控制块轨道表并做总线映射）。
    IReadOnlyList<BridgeTrack> GetTracks();

    // 把音轨 [position, endPosition) 的立体声渲染累加进 buffer（L/R 交错，自 offset 起）。
    // 按 track.FollowGainPan 决定是否叠加音量/声像；调用前 buffer 对应区须已清零。
    // 帧数 = endPosition - position（不足部分静音补零）。
    void RenderTrack(BridgeTrack track, int position, int endPosition, float[] buffer, int offset);

    bool IsMute(BridgeTrack track);
    bool IsSolo(BridgeTrack track);
    bool HasSolo { get; }
    int EndTime(BridgeTrack track);

    // DAW 采样率变化 → 通知音频引擎（使渲染/时间线对齐）。返回是否已应用。
    bool ApplySampleRate(int sampleRate);

    // 桥接激活/停用：连接时让宿主本地播放静音（避免双路出声），断开时恢复。
    void SetBridgeActive(bool active);

    // —— M2 传输同步（DAW 为 master，TuneLab 跟随）——
    // DAW 播放/暂停状态边沿变化 → 通知音频引擎（实现侧切 UI 线程后调用 Play/Pause）。
    void SetTransportPlaying(bool playing);

    // DAW 播放头位置（秒，samplePos/sampleRate）→ 让 TuneLab 播放头跟随（实现侧切 UI 线程）。
    void SetTransportSeek(double seconds);

    // DAW 曲速（BPM；null = 无有效曲速/断开）→ 会话时基覆盖（实现侧切 UI 线程）。
    void SetTransportTempo(double? bpm);
}
