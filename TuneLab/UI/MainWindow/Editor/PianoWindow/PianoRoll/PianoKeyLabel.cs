using Avalonia.Media;
using System;
using TuneLab.Configs;
using TuneLab.Foundation;
using TuneLab.GUI;

namespace TuneLab.UI;

// 音高标签的共享换算（CDEFGAB / 简谱两种模式）。钢琴键（PianoRoll）与音符前端标签（PianoScrollView）共用，
// 保证两处显示口径一致：简谱数字与八度偏移依同一主音（Settings.NumberedPianoKeyTonic）换算。
internal static class PianoKeyLabel
{
    public static bool IsNumbered => Settings.PianoKeyLabelStyle.Value == "Numbered";

    // CDEFGAB：如 "C4"
    public static string PitchNameLabel(int keyNumber)
    {
        int pitchClass = PositiveMod(keyNumber - MusicTheory.C0_PITCH, 12);
        int octave = (keyNumber - MusicTheory.C0_PITCH) / 12;
        return PitchNames[pitchClass] + octave;
    }

    // 简谱：数字(1-7，#升号) + 相对基准八度(N4)的八度偏移。
    public static bool TryGetNumberedPitchLabelParts(int keyNumber, out string number, out int octaveOffset)
    {
        int pitchClass = PositiveMod(keyNumber - MusicTheory.C0_PITCH, 12);
        int tonic = TonicPitchClass(Settings.NumberedPianoKeyTonic.Value);
        int relative = PositiveMod(pitchClass - tonic, 12);
        int baseTonicPitch = MusicTheory.C0_PITCH + NumberedBaseOctave * 12 + tonic;

        number = NumberedPitchNames[relative];
        octaveOffset = (keyNumber - baseTonicPitch - relative) / 12;
        return true;
    }

    // 简谱的 Jianpu 字体 ASCII 写法（"1''" 高八度点在上 / "1,," 低八度点在下）；超出字体连字范围(±2)返回 null，
    // 调用方应退回 NumberedPitchLabel。
    public static string? NumberedAsciiText(int keyNumber)
    {
        if (!TryGetNumberedPitchLabelParts(keyNumber, out var number, out int octaveOffset))
            return null;

        if (Math.Abs(octaveOffset) > JianpuMaxOctaveOffset)
            return null;

        return octaveOffset > 0
            ? number + new string('\'', octaveOffset)
            : octaveOffset < 0
                ? number + new string(',', -octaveOffset)
                : number;
    }

    // 简谱带八度的普通文本（Jianpu 字体不可用时的退回）：如 "1 (C4)"
    public static string NumberedPitchLabel(int keyNumber)
    {
        int pitchClass = PositiveMod(keyNumber - MusicTheory.C0_PITCH, 12);
        int octave = (keyNumber - MusicTheory.C0_PITCH) / 12;
        int tonic = TonicPitchClass(Settings.NumberedPianoKeyTonic.Value);
        int relative = PositiveMod(pitchClass - tonic, 12);
        return $"{NumberedPitchNames[relative]} (C{octave})";
    }

    public static int TonicPitchClass(string? tonic)
    {
        for (int i = 0; i < PitchNames.Length; i++)
        {
            if (string.Equals(tonic, PitchNames[i], StringComparison.OrdinalIgnoreCase))
                return i;
        }
        return 0;
    }

    public static int PositiveMod(int value, int mod)
        => (value % mod + mod) % mod;

    public static readonly string[] PitchNames = ["C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"];
    public static readonly string[] NumberedPitchNames = ["1", "#1", "2", "#2", "3", "4", "#4", "5", "#5", "6", "#6", "7"];
    public const int NumberedBaseOctave = 4;

    // 简谱字体（Assets.Jianpu，jianpu-ascii-font）——其 ASCII 连字只支持 ±2 个八度。
    public static readonly Typeface JianpuTypeface = new(Assets.Jianpu);
    public const int JianpuMaxOctaveOffset = 2;
}
