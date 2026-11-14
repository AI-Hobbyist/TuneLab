using System.Globalization;

namespace TuneLab.Hosting;

internal readonly record struct CubaseRenderStatus(int Song, int Preview, long PreviewMicroseconds)
{
    public string DisplayText()
    {
        string song = Song switch
        {
            -1 => "工程：尚未准备",
            0 => "工程：就绪",
            7 => "工程：合成中",
            6 => "工程：已取消",
            _ => $"工程：合成失败（{Song}）"
        };
        string preview = Preview switch
        {
            -1 => "MIDI 试听：按键后准备",
            0 => "MIDI 试听：音符已缓存",
            7 => "MIDI 试听：合成中",
            6 => "MIDI 试听：已取消",
            _ => $"MIDI 试听：合成失败（{Preview}）"
        };
        if (Preview == 0 && PreviewMicroseconds > 0)
        {
            string milliseconds = (PreviewMicroseconds / 1000.0).ToString("0.0", CultureInfo.InvariantCulture);
            preview += $"，最近准备 {milliseconds} ms";
        }
        return $"{song}    {preview}";
    }
}
