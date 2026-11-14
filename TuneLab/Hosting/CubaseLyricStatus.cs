namespace TuneLab.Hosting;

// Private integration status codes shared with the native ingress. Diagnostic
// state is process-local; it is not an engine API or serialized musical data.
internal readonly record struct CubaseLyricStatus(int Kind, ulong SourceNoteId = 0)
{
    public string DisplayText() => Kind switch
    {
        -1 => "歌词：等待宿主文本",
        0 => "歌词：无变化",
        1 => "歌词：已应用",
        2 => "歌词：存在冲突，已保留 TuneLab 中的编辑",
        3 => "歌词：未找到唯一对应音符，未应用",
        4 => "歌词：输入过多，部分歌词未应用，请重新播放片段",
        5 => "歌词：等待当前编辑结束后重试",
        6 => "歌词：应用失败，等待重试",
        7 => "歌词：文本格式或长度不支持，已拒绝",
        _ => "歌词：状态不可用"
    };
}
