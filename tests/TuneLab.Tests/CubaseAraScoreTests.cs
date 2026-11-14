using System;
using System.Linq;
using TuneLab.Data.Timing;
using TuneLab.Foundation;
using TuneLab.Hosting;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

public class CubaseAraScoreTests
{
    [Theory]
    [InlineData(10, 2, -960)]
    [InlineData(-1, -1, -1440)]
    public void HostBarPhaseAndNumbersSurviveSourceLocalOrigin(double quarter, int bar, double tick)
    {
        var projection = CubaseAraScore.Create([new(0, 0), new(20, 40)],
            [new(1, 0, 1, 60)], new(0, 2, quarter / 2, 2), new SoundSourceInfo(),
            [new(0, 4, 4), new(8, 3, 4), new(14, 5, 8)]);
        Assert.Equal(tick, projection.MeterOriginTick);
        Assert.Equal(bar, projection.File.Project.TimeSignatures[0].BarIndex);
        using var project = new TuneLab.Data.Project(new ProjectInfo
        {
            TimeSignatures = projection.File.Project.TimeSignatures
        }, registerWithStandaloneAudio: false, meterOriginTick: projection.MeterOriginTick);
        var signatures = project.TimeSignatureManager.TimeSignatures;
        Assert.Equal(tick, signatures[0].Pos);
        double nextQuarter = quarter == 10 ? 14 : 8;
        Assert.Equal((nextQuarter - quarter) * MusicTheory.RESOLUTION, signatures[1].Pos);
        var state = CubaseAraMerge.UpdateState([], projection);
        Assert.Equal(tick, CubaseProjectState.DecodeEnvelope(state).Ara!.MeterOriginTick);
        var render = CubaseProjectState.DecodeEnvelope(CubaseAraScore.CreateRenderState(state, 2));
        Assert.Equal(tick, render.Ara!.MeterOriginTick);
        using var standalone = new TuneLab.Data.Project(new ProjectInfo());
        Assert.Equal(0, standalone.TimeSignatureManager.TimeSignatures[0].Pos);
    }

    [Fact]
    public void HostMeterUpdatePreservesLyricsAndUpdatesBarOrigin()
    {
        CubaseAraScoreProjection Score(int numerator) => CubaseAraScore.Create(
            [new(0, 0), new(20, 40)], [new(1, 0, 1, 60)], new(0, 2, 5, 2),
            new SoundSourceInfo(), [new(0, 4, 4), new(8, numerator, 4), new(14, 5, 8)]);
        var state = CubaseAraMerge.UpdateState([], Score(3));
        var edited = CubaseProjectState.DecodeEnvelope(state);
        ((MidiPartInfo)edited.Project.Project.Tracks[0].Parts[0]).Notes[0].Lyric = "la";
        var merged = CubaseProjectState.DecodeEnvelope(CubaseAraMerge.UpdateState(
            CubaseProjectState.Encode(edited.Project, edited.Ara), Score(2)));
        Assert.Equal(0, merged.Ara!.MeterOriginTick);
        Assert.Equal(2, merged.Project.Project.TimeSignatures[0].Numerator);
        Assert.Equal("la", ((MidiPartInfo)merged.Project.Project.Tracks[0].Parts[0]).Notes[0].Lyric);
    }

    [Theory]
    [InlineData(1)]
    [InlineData(2)]
    public void SourceTimingSurvivesTempoChangeAndNonzeroSongOrigin(double ratio)
    {
        var projection = CubaseAraScore.Create(
            [new(10, 0), new(12, 4), new(16, 8)],
            [new(11, 0.25, 0.5, 60), new(12, 0.25, 1.25, 60)],
            new(0.5, 2, 11 + 0.5 * ratio, 2 * ratio),
            new SoundSourceInfo { Type = "test-engine", Id = "test-singer" });
        Assert.Equal(11, projection.SourceZeroSongSeconds);
        Assert.Equal(2, projection.SourceZeroSongQuarter);
        Assert.Equal(new ulong[] { 11, 12 }, projection.NoteIds);
        var part = Assert.IsType<MidiPartInfo>(Assert.Single(Assert.Single(projection.File.Project.Tracks).Parts));
        Assert.Equal("test-engine", part.SoundSource.Type);
        Assert.Equal("test-singer", part.SoundSource.Id);
        Assert.Equal(2, part.Notes.Count);
        Assert.All(part.Notes, note => Assert.Equal(string.Empty, note.Lyric));
        var timing = new TempoSnapshot(projection.File.Project.Tempos.Select(
            tempo => new TempoMark(tempo.Pos, tempo.Bpm)).ToArray(), MusicTheory.RESOLUTION);
        foreach (var note in part.Notes)
        {
            Assert.Equal(0.25, timing.ToSecond(note.Pos), 9);
        }
        Assert.Equal(0.75, timing.ToSecond(part.Notes[0].Pos + part.Notes[0].Dur), 9);
        Assert.Equal(1.5, timing.ToSecond(part.Notes[1].Pos + part.Notes[1].Dur), 9);
    }

    [Theory]
    [InlineData(0.5)]
    [InlineData(1)]
    [InlineData(2)]
    public void RenderCopyScalesTimeAcrossTempoBoundariesAndPreservesPitch(double scale)
    {
        var projection = CubaseAraScore.Create(
            [new(10, 0), new(12, 4), new(16, 8)],
            [new(11, 0.25, 1.25, 60)], new(0.5, 2, 11 + 0.5 * scale, 2 * scale),
            new SoundSourceInfo { Type = "engine", Id = "singer" });
        var source = CubaseProjectState.Encode(projection.File);
        var rendered = CubaseProjectState.Decode(CubaseAraScore.CreateRenderState(source, scale));
        var part = Assert.IsType<MidiPartInfo>(Assert.Single(Assert.Single(rendered.Project.Tracks).Parts));
        var note = Assert.Single(part.Notes);
        var timing = new TempoSnapshot(rendered.Project.Tempos.Select(
            tempo => new TempoMark(tempo.Pos, tempo.Bpm)).ToArray(), MusicTheory.RESOLUTION);
        Assert.Equal(0.25 * scale, timing.ToSecond(note.Pos), 9);
        Assert.Equal(1.5 * scale, timing.ToSecond(note.Pos + note.Dur), 9);
        Assert.Equal(60, note.Pitch);
        Assert.Equal("singer", part.SoundSource.Id);
        Assert.Equal(source, CubaseProjectState.Encode(projection.File));
    }

    [Fact]
    public void DuplicateIdentityAndEmptyRegionAreRejected()
    {
        CubaseAraTempoPoint[] tempo = [new(0, 0), new(1, 2)];
        var voice = new SoundSourceInfo();
        Assert.Throws<ArgumentException>(() => CubaseAraScore.Create(tempo,
            [new(1, 0, 1, 60), new(1, 0, 1, 60)], new(0, 1, 0, 1), voice));
        Assert.Throws<ArgumentException>(() => CubaseAraScore.Create(tempo, [], new(0, 0, 0, 0), voice));
    }
}
