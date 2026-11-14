using System.Linq;
using TuneLab.Data.Timing;
using TuneLab.Foundation;
using TuneLab.Hosting;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

public class CubaseAraMergeTests
{
    static CubaseAraScoreProjection Score(params CubaseAraSourceNote[] notes) => CubaseAraScore.Create(
        [new(0, 0), new(10, 20)], notes, new(0, 10, 0, 10),
        new SoundSourceInfo { Type = "engine", Id = "singer" });

    static MidiPartInfo Part(CubaseAraScoreProjection score) => (MidiPartInfo)score.File.Project.Tracks[0].Parts[0];

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void DifferentRegionTempoKeepsSourceSecondsForOverridesManualNotesAndExpression(bool segmented)
    {
        CubaseAraScoreProjection Region(double bpm) => CubaseAraScore.Create(
            [new(10, 0), new(30, bpm / 3)], [new(1, 1, 1, 60)], new(0, 10, 10, 10),
            new SoundSourceInfo(), [new(0, 4, 4)]);
        var first = Region(120);
        var edited = Region(120);
        var part = Part(edited);
        part.Notes[0].Pos = 1200;
        part.Notes[0].Dur = 720;
        part.Notes[0].Lyric = "你";
        part.Notes[0].BodyOffset = -0.05;
        part.Notes[0].BodyPhonemes.Add(new PhonemeInfo { Symbol = "i", Duration = 0.5 });
        part.Notes.Add(new NoteInfo { Pos = 3840, Dur = 480, Pitch = 65, Lyric = "手工" });
        part.Pitch.Segments.Add([new Point(1200, 60.5), new Point(1920, 61), new Point(3840, 62)]);
        part.Automations.Add("volume", new AutomationInfo { Points = [new Point(1200, 0.8)] });
        part.PiecewiseAutomations.Add("tone", new PiecewiseAutomationInfo
        {
            Segments = [[new Point(1200, 0.2), new Point(1920, 0.4)]]
        });
        var effect = new EffectInfo { Type = "test-effect" };
        effect.Automations.Add("wet", new AutomationInfo { Points = [new Point(1200, 0.3)] });
        part.Effects.Add(effect);
        part.Vibratos.Add(new VibratoInfo { Pos = 1200, Dur = 720, Frequency = 5, Attack = 0.1 });
        edited = edited with { NoteIds = new ulong[] { 1, 0 } };
        part.EndOffset = 4320;
        var original = CubaseProjectState.Encode(edited.File);
        var second = segmented ? CubaseAraScore.Create(
            [new(10, 0), new(12, 2), new(30, 38)], [new(1, 1, 1, 60)], new(0, 10, 11, 10),
            new SoundSourceInfo(), [new(0, 4, 4)]) : Region(60);
        var mapped = CubaseAraMerge.Merge(first, edited, second);
        var result = Part(mapped);
        TempoSnapshot Timing(CubaseAraScoreProjection score) => new(score.File.Project.Tempos.Select(
            tempo => new TempoMark(tempo.Pos, tempo.Bpm)).ToArray(), MusicTheory.RESOLUTION);
        var time = Timing(mapped);
        Assert.Equal(1.25, time.ToSecond(result.Notes[0].Pos), 9);
        Assert.Equal(2, time.ToSecond(result.Notes[0].Pos + result.Notes[0].Dur), 9);
        Assert.Equal(4, time.ToSecond(result.Notes[1].Pos), 9);
        Assert.Equal(1.25, time.ToSecond(result.Pitch.Segments[0][0].X), 9);
        Assert.Equal(4, time.ToSecond(result.Pitch.Segments[0][2].X), 9);
        Assert.Equal(1.25, time.ToSecond(result.Automations["volume"].Points[0].X), 9);
        Assert.Equal(1.25, time.ToSecond(result.PiecewiseAutomations["tone"].Segments[0][0].X), 9);
        Assert.Equal(1.25, time.ToSecond(result.Effects[0].Automations["wet"].Points[0].X), 9);
        Assert.Equal(1.25, time.ToSecond(result.Vibratos[0].Pos), 9);
        Assert.Equal(2, time.ToSecond(result.Vibratos[0].Pos + result.Vibratos[0].Dur), 9);
        Assert.Equal(5, result.Vibratos[0].Frequency);
        Assert.Equal(0.1, result.Vibratos[0].Attack);
        Assert.Equal(-0.05, result.Notes[0].BodyOffset);
        var back = CubaseAraMerge.Merge(second, mapped, first);
        Assert.Equal(original, CubaseProjectState.Encode(back.File));
        Assert.Equal(original, CubaseProjectState.Encode(edited.File));
        var association = CubaseProjectState.DecodeEnvelope(CubaseAraMerge.UpdateState([], first)).Ara!;
        association.CurrentNoteIds = edited.NoteIds.ToArray();
        var saved = CubaseProjectState.Encode(edited.File, association);
        var secondState = CubaseAraMerge.UpdateState(saved, second);
        var persisted = CubaseProjectState.DecodeEnvelope(secondState);
        Assert.Equal(CubaseProjectState.Encode(mapped.File), CubaseProjectState.Encode(persisted.Project));
        Assert.Equal(segmented ? -480 : 0, persisted.Ara!.MeterOriginTick);
        var restored = CubaseProjectState.DecodeEnvelope(CubaseAraMerge.UpdateState(secondState, first));
        Assert.Equal(original, CubaseProjectState.Encode(restored.Project));
    }

    [Fact]
    public void HostUpdatePreservesLyricsPhonemesAndOverriddenPitch()
    {
        var baseline = Score(new(1, 0, 1, 60), new(2, 0, 2, 60));
        var edited = Score(new(1, 0, 1, 60), new(2, 0, 2, 60));
        var note = Part(edited).Notes[1];
        note.Pitch = 65;
        note.Lyric = "你";
        note.Pronunciation = "ni";
        note.LeadingPhonemes.Add(new PhonemeInfo { Symbol = "n", Duration = 0.1 });
        note.BodyPhonemes.Add(new PhonemeInfo { Symbol = "i", Duration = 0.5, StretchWeight = 1 });
        note.BodyOffset = -0.05;
        var incoming = Score(new(2, 1, 3, 62), new(3, 5, 1, 67));
        var merged = CubaseAraMerge.Merge(baseline, edited, incoming);
        Assert.Equal(new ulong[] { 2, 3 }, merged.NoteIds);
        var result = Part(merged).Notes[0];
        Assert.Equal(960, result.Pos);
        Assert.Equal(2880, result.Dur);
        Assert.Equal(65, result.Pitch);
        Assert.Equal("你", result.Lyric);
        Assert.Equal("ni", result.Pronunciation);
        Assert.Equal("n", Assert.Single(result.LeadingPhonemes).Symbol);
        Assert.Equal("i", Assert.Single(result.BodyPhonemes).Symbol);
        Assert.Equal(-0.05, result.BodyOffset);
        Assert.Equal("singer", Part(merged).SoundSource.Id);
        result.BodyPhonemes[0].Symbol = "changed";
        Assert.Equal("i", note.BodyPhonemes[0].Symbol);
        Assert.Equal(0, Part(baseline).Notes[1].Pos);
        Assert.Equal(62, Part(incoming).Notes[0].Pitch);
    }

    [Fact]
    public void RepeatedSnapshotKeepsUserDeletionAndManualNotes()
    {
        var baseline = Score(new(1, 0, 1, 60), new(2, 2, 1, 62));
        var edited = Score(new CubaseAraSourceNote(2, 2, 1, 62));
        Part(edited).Notes.Add(new NoteInfo { Pos = 0, Dur = 120, Pitch = 70, Lyric = "手工" });
        edited = edited with { NoteIds = new ulong[] { 2, 0 } };
        var merged = CubaseAraMerge.Merge(baseline, edited, baseline);
        Assert.Equal(new ulong[] { 0, 2 }, merged.NoteIds);
        Assert.Equal("手工", Part(merged).Notes[0].Lyric);
        Assert.DoesNotContain(1UL, merged.NoteIds);
    }

    [Fact]
    public void PersistedStateFeedsSuccessiveUpdatesWithoutLosingOverrides()
    {
        var state = CubaseAraMerge.UpdateState([], Score(new CubaseAraSourceNote(1, 0, 1, 60)));
        var envelope = CubaseProjectState.DecodeEnvelope(state);
        ((MidiPartInfo)envelope.Project.Project.Tracks[0].Parts[0]).Notes[0].Lyric = "你";
        state = CubaseProjectState.Encode(envelope.Project, envelope.Ara);
        var updated = CubaseAraMerge.UpdateState(state, Score(new CubaseAraSourceNote(1, 2, 1, 62)));
        var reopened = CubaseProjectState.DecodeEnvelope(updated);
        var note = ((MidiPartInfo)reopened.Project.Project.Tracks[0].Parts[0]).Notes[0];
        Assert.Equal("你", note.Lyric);
        Assert.Equal(1920, note.Pos);
        Assert.Equal(62, note.Pitch);
        var repeated = CubaseAraMerge.UpdateState(updated, Score(new CubaseAraSourceNote(1, 2, 1, 62)));
        Assert.Equal(updated, repeated);
    }
}
