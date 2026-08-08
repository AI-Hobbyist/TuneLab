namespace TuneLab.Bridge;

// 与 Bridge/protocol/TLBridgeProtocol.h 一一对应的共享内存协议（C# 镜像）。
// 字段偏移必须与头文件 TL_BRIDGE_OFF_* / TL_BRIDGE_TRACK_OFF_* 宏一致——由
// BridgeProtocolLayoutTests 对照头文件守护，勿手改偏移。
internal static class BridgeProtocol
{
    public const uint Magic = 0x544C4252u;            // "TLBR"
    public const uint Version = 1;
    public const int MaxTracks = 64;
    public const int SessionNameMax = 128;
    public const int TrackNameMax = 64;
    public const string ShmPrefix = "TuneLab.Bridge.";

    // 心跳（毫秒）：两侧各自递增自己的 tick；对侧 tick 停滞超过超时判为断开。
    public const int HeartbeatMs = 500;
    public const int HeartbeatTimeoutMs = 3000;

    // 协议错误码（TLBridgeProtocolError）
    public const uint ErrorNone = 0;
    public const uint ErrorMagicMismatch = 1;
    public const uint ErrorVersionMismatch = 2;
    public const uint ErrorBusy = 3;

    public static string ShmName(string sessionId) => ShmPrefix + sessionId;

    // TLBridgeControl 各字段字节偏移（与 TL_BRIDGE_OFF_* 一致）。
    public static class Offset
    {
        public const int Magic = 0;
        public const int Version = 4;
        public const int Connected = 8;
        public const int ProtocolError = 12;
        public const int SamplePos = 16;
        public const int State = 24;
        public const int Tempo = 32;
        public const int TimeSigNum = 40;
        public const int TimeSigDen = 44;
        public const int PpqPosition = 48;
        public const int PpqOfLastBarStart = 56;
        public const int SampleRate = 64;
        public const int BlockSize = 68;
        public const int ActiveBuses = 72;
        public const int LatencySamples = 76;
        public const int Tracks = 80;
        public const int TrackSize = 80;
        public const int HostTick = 5200;
        public const int PluginTick = 5208;
        public const int SessionName = 5216;
        public const int HostPid = 5344;
        public const int PluginPid = 5348;
        public const int HostAppVersion = 5352;
        public const int Reserved = 5356;
        public const int ControlSize = 5360;
    }

    // TLBridgeTrack 字段偏移（与 TL_BRIDGE_TRACK_OFF_* 一致）。
    public static class TrackOffset
    {
        public const int Name = 0;
        public const int Enabled = 64;
        public const int BusIndex = 68;
        public const int FollowGainPan = 72;
        public const int MirrorMuteSolo = 76;
    }

    public static int TrackStart(int trackIndex) => Offset.Tracks + trackIndex * Offset.TrackSize;
}
