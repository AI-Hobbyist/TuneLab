namespace TuneLab.Bridge;

// 轨道快照 DTO：宿主渲染线程每帧刷新控制块轨道表时产出。
// Source 为宿主对象的不透明句柄（TuneLab 侧实现据此取音频与属性），本程序集不解释其类型。
public sealed class BridgeTrack
{
    public const int MaxBusCount = 64;

    public string Name { get; set; } = "";
    public bool Enabled { get; set; } = true;
    public int BusIndex { get; set; }
    public bool FollowGainPan { get; set; } = true;
    public bool MirrorMuteSolo { get; set; } = true;
    public object? Source { get; set; }
}
