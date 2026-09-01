using System.Collections.Generic;
using System.Linq;
using TuneLab.Data;
using TuneLab.Foundation;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

// part 合并（IMidiPartExtension.MergePartInfos）的内容归属口径回归。
// 判据必须是各段的**可见区间**（Pos + StartOffset ~ Pos + EndOffset），不是锚点 Pos：拖边缘只改偏移、不裁内容，
// 所以真实 part 普遍携带可见区间外的历史内容。曾按锚点划分归属，导致前段可见尾部丢失、后段隐藏头部被平移进结果
// （现象 = 合并后音符错位 + 重复）。夹具几何取自一份真实工程（同一份内容被裁成三段、每段都有锚点外内容）。
public class PartMergeTests
{
    static NoteInfo Note(double pos, string lyric, double dur = 100) => new() { Pos = pos, Dur = dur, Pitch = 60, Lyric = lyric };

    static MidiPartInfo Part(double pos, double startOffset, double endOffset, params NoteInfo[] notes) => new()
    {
        Pos = pos,
        StartOffset = startOffset,
        EndOffset = endOffset,
        Notes = notes.ToList(),
    };

    // 真实工程第一轨的三段几何：可见区间首尾相接，但每段的内容都溢出自己的可见区间。
    static MidiPartInfo[] ThreeSegments() =>
    [
        // 可见 [0, 116160)：local 15903 可见、local 69312 也可见（曾被"下一段锚点 69288"错误截掉）
        Part(0, 0, 116160, Note(15903, "qing"), Note(69312, "zui"), Note(116200, "beyond-end")),
        // 可见 [116148, 151080)：local 15903 是裁掉的历史内容（曾被平移 +69288 → 绝对 85191 冒出重复音符）
        Part(69288, 46860, 81792, Note(15903, "hidden-head"), Note(46900, "mid"), Note(81800, "hidden-tail")),
        // 可见 [151104, 170880)：local 414 是裁掉的历史内容
        Part(149760, 1344, 21120, Note(414, "hidden-head2"), Note(1400, "tail")),
    ];

    static List<string> Lyrics(MidiPartInfo part) => part.Notes.Select(n => n.Lyric).ToList();
    static List<double> Positions(MidiPartInfo part) => part.Notes.Select(n => n.Pos).ToList();

    [Fact]
    public void Merge_TakesOnlyContentInsideVisibleRanges()
    {
        var merged = IMidiPartExtension.MergePartInfos(ThreeSegments());

        // 各段仅贡献自己可见区间内的音符，位置按绝对时间线（锚点 = 首段锚点 0）落位。
        Assert.Equal(new List<string> { "qing", "zui", "mid", "tail" }, Lyrics(merged));
        Assert.Equal(new List<double> { 15903, 69312, 69288 + 46900, 149760 + 1400 }, Positions(merged));
    }

    [Fact]
    public void Merge_GeometryIsEnvelopeOfVisibleRanges()
    {
        var merged = IMidiPartExtension.MergePartInfos(ThreeSegments());

        Assert.Equal(0, merged.Pos);
        Assert.Equal(0, merged.StartOffset);
        Assert.Equal(149760 + 21120, merged.EndOffset);
    }

    [Fact]
    public void Merge_IsIndependentOfInputOrder()
    {
        var forward = IMidiPartExtension.MergePartInfos(ThreeSegments());
        var shuffled = ThreeSegments();
        var reversed = IMidiPartExtension.MergePartInfos([shuffled[2], shuffled[0], shuffled[1]]);

        Assert.Equal(Lyrics(forward), Lyrics(reversed));
        Assert.Equal(Positions(forward), Positions(reversed));
        Assert.Equal(forward.EndOffset, reversed.EndOffset);
    }

    // 锚点序 ≠ 可见起点序：后段锚点更靠左（StartOffset 大幅前向裁剪），排序须按可见起点。
    [Fact]
    public void Merge_SortsByVisibleStartNotAnchor()
    {
        var first = Part(1000, 0, 500, Note(100, "first"));
        var second = Part(0, 1600, 2000, Note(1700, "second"));

        var merged = IMidiPartExtension.MergePartInfos([second, first]);

        Assert.Equal(1000, merged.Pos);
        Assert.Equal(new List<string> { "first", "second" }, Lyrics(merged));
        Assert.Equal(new List<double> { 100, 700 }, Positions(merged));
        Assert.Equal(0, merged.StartOffset);
        Assert.Equal(1000, merged.EndOffset); // 可见包络 [1000, 2000) → 相对锚点 1000
    }

    // 可见区间重叠时后段覆盖前段：前段的取值上界收到后段可见起点。
    [Fact]
    public void Merge_OverlappingVisibleRanges_LatterWins()
    {
        var former = Part(0, 0, 1000, Note(100, "a"), Note(600, "a-covered"));
        var latter = Part(500, 0, 1000, Note(100, "b"));

        var merged = IMidiPartExtension.MergePartInfos([former, latter]);

        Assert.Equal(new List<string> { "a", "b" }, Lyrics(merged));
        Assert.Equal(new List<double> { 100, 600 }, Positions(merged));
    }

    // 可见起点相同时前段被后段整段覆盖（贡献空区间，不因 upper == lower 反向纳入内容）。
    [Fact]
    public void Merge_CoveredSegmentContributesNothing()
    {
        var covered = Part(0, 500, 1000, Note(600, "covered"));
        var cover = Part(500, 0, 2000, Note(100, "cover"));

        var merged = IMidiPartExtension.MergePartInfos([covered, cover]);

        Assert.Equal(new List<string> { "cover" }, Lyrics(merged));
    }

    [Fact]
    public void Merge_ClipsCurvesToVisibleRanges()
    {
        var former = new MidiPartInfo
        {
            Pos = 0,
            StartOffset = 0,
            EndOffset = 1000,
            Pitch = new PitchInfo { Segments = [[new(100, 60), new(600, 61), new(1200, 62)]] },
            Automations = { { "volume", new AutomationInfo { DefaultValue = 0, Points = [new(100, 1), new(1200, 2)] } } },
            PiecewiseAutomations = { { "tension", new PiecewiseAutomationInfo { Segments = [[new(100, 1)], [new(1200, 2)]] } } },
        };
        var latter = new MidiPartInfo
        {
            Pos = 1000,
            StartOffset = 0,
            EndOffset = 1000,
            // local [-500, 0) 是可见区间左侧的历史内容，须整段丢弃（不留空 segment）。
            Pitch = new PitchInfo { Segments = [[new(-500, 50)], [new(100, 63)]] },
            Automations = { { "volume", new AutomationInfo { DefaultValue = 0, Points = [new(-500, 9), new(100, 3)] } } },
        };

        var merged = IMidiPartExtension.MergePartInfos([former, latter]);

        var expectedPitch = new List<List<Point>>
        {
            new() { new Point(100, 60), new Point(600, 61) },
            new() { new Point(1100, 63) },
        };
        Assert.Equal(expectedPitch, merged.Pitch.Segments);
        Assert.Equal(new List<Point> { new(100, 1), new(1100, 3) }, merged.Automations["volume"].Points);
        Assert.Equal(new List<List<Point>> { new() { new Point(100, 1) } }, merged.PiecewiseAutomations["tension"].Segments);
    }

    // vibrato 按起点归属；effect 链取首段（逐段的链无从叠加）。
    [Fact]
    public void Merge_KeepsVibratosByStartAndEffectsOfFirstSegment()
    {
        var former = new MidiPartInfo
        {
            Pos = 0,
            StartOffset = 0,
            EndOffset = 1000,
            Vibratos = [new VibratoInfo { Pos = 100, Dur = 50 }, new VibratoInfo { Pos = 1200, Dur = 50 }],
            Effects = [new EffectInfo { Id = "e1", Type = "reverb" }],
        };
        var latter = new MidiPartInfo
        {
            Pos = 1000,
            StartOffset = 0,
            EndOffset = 1000,
            Vibratos = [new VibratoInfo { Pos = -100, Dur = 50 }, new VibratoInfo { Pos = 200, Dur = 50 }],
            Effects = [new EffectInfo { Id = "e2", Type = "eq" }],
        };

        var merged = IMidiPartExtension.MergePartInfos([former, latter]);

        Assert.Equal(new List<double> { 100, 1200 }, merged.Vibratos.Select(v => v.Pos).ToList());
        Assert.Equal(new List<string> { "e1" }, merged.Effects.Select(e => e.Id).ToList());
    }
}
