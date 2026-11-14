using System;
using System.Linq;
using TuneLab.Hosting;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

public class CubaseAraLyricsTests
{
    static CubaseAraScoreProjection Score() => CubaseAraScore.Create(
        [new(0, 0), new(10, 20)], [new(11, 0, 1, 60), new(22, 1, 1, 62)],
        new(0, 10, 0, 10), new SoundSourceInfo());

    static byte[] State() => CubaseAraMerge.UpdateState([], Score());

    [Fact]
    public void HostCopyDeletionAndUndoSnapshotsKeepLyricsOnTheirOwnStableIds()
    {
        var original = CubaseAraLyrics.UpdateState(State(), "", [new(11, "你"), new(22, "好")]).State;
        var copiedProjection = CubaseAraScore.Create([new(0, 0), new(10, 20)],
            [new(11, 0, 1, 60), new(22, 1, 1, 62), new(33, 2, 1, 60)],
            new(0, 10, 0, 10), new SoundSourceInfo());
        var copied = CubaseAraMerge.UpdateState(original, copiedProjection);
        copied = CubaseAraLyrics.UpdateState(copied, "", [new(33, "复制")]).State;
        Assert.Equal(new[] { "你", "好", "复制" }, CubaseAraLyrics.SourcePart(CubaseProjectState.Decode(copied)).Notes.Select(note => note.Lyric));
        var removedProjection = CubaseAraScore.Create([new(0, 0), new(10, 20)],
            [new(22, 1, 1, 62), new(33, 2, 1, 60)], new(0, 10, 0, 10), new SoundSourceInfo());
        var removed = CubaseAraMerge.UpdateState(copied, removedProjection);
        Assert.Equal(0UL, CubaseAraLyrics.ResolveSourceNote(removed, "", 0, 60));
        Assert.Throws<ArgumentException>(() => CubaseAraLyrics.UpdateState(removed, "", [new(11, "stale")]));
        Assert.Equal(new[] { "好", "复制" }, CubaseAraLyrics.SourcePart(CubaseProjectState.Decode(removed)).Notes.Select(note => note.Lyric));
        // Host undo restores its archived snapshot, including the old identity set.
        var restored = CubaseAraLyrics.UpdateState(copied, "", [new(11, "恢复")]);
        Assert.Equal(new ulong[] { 11 }, restored.ChangedNotes);
        Assert.Equal(new[] { "恢复", "好", "复制" }, CubaseAraLyrics.SourcePart(CubaseProjectState.Decode(restored.State)).Notes.Select(note => note.Lyric));
    }

    [Fact]
    public void SourceRevisionIgnoresLyricsAndLocalGeometryButDetectsHostGeometry()
    {
        var initial = CubaseProjectState.DecodeEnvelope(State());
        var lyrics = CubaseProjectState.DecodeEnvelope(CubaseAraLyrics.UpdateState(State(), "", [new(11, "你")]).State);
        Assert.True(CubaseAraLyrics.SameSourceAssociation(initial, lyrics));
        CubaseAraLyrics.SourcePart(lyrics.Project).Notes[0].Pos += 120;
        CubaseAraLyrics.SourcePart(lyrics.Project).Notes[0].Pitch += 3;
        Assert.True(CubaseAraLyrics.SameSourceAssociation(initial, lyrics));
        var baseline = CubaseProjectState.Decode(lyrics.Ara!.BaselineState);
        CubaseAraLyrics.SourcePart(baseline).Notes[0].Pos += 120;
        lyrics.Ara.BaselineState = CubaseProjectState.Encode(baseline);
        Assert.False(CubaseAraLyrics.SameSourceAssociation(initial, lyrics));
    }

    [Theory]
    [InlineData("identity")]
    [InlineData("origin")]
    [InlineData("tempo")]
    public void SourceRevisionDetectsIdentityOriginAndClockChanges(string change)
    {
        var initial = CubaseProjectState.DecodeEnvelope(State());
        var next = CubaseProjectState.DecodeEnvelope(State());
        if (change == "identity")
        {
            next.Ara!.BaselineNoteIds[0] = 99;
            next.Ara.CurrentNoteIds[0] = 99;
        }
        else if (change == "origin")
            next.Ara!.SourceZeroSongQuarter = 4;
        else
        {
            var baseline = CubaseProjectState.Decode(next.Ara!.BaselineState);
            baseline.Project.Tempos[0].Bpm = 60;
            next.Ara.BaselineState = CubaseProjectState.Encode(baseline);
        }
        Assert.False(CubaseAraLyrics.SameSourceAssociation(initial, next));
        Assert.True(CubaseAraLyrics.SameSourceAssociation(null, null));
        Assert.False(CubaseAraLyrics.SameSourceAssociation(null, initial));
    }

    [Fact]
    public void ResolveUsesHostBaselineAndRejectsAmbiguousOrMissingOnsets()
    {
        var state = State();
        var edited = CubaseProjectState.DecodeEnvelope(state);
        var note = CubaseAraLyrics.SourcePart(edited.Project).Notes[0];
        note.Pos = 1200;
        note.Pitch = 72;
        var saved = CubaseProjectState.Encode(edited.Project, edited.Ara);
        Assert.Equal(11UL, CubaseAraLyrics.ResolveSourceNote(saved, "", 0, 60));
        Assert.Equal(22UL, CubaseAraLyrics.ResolveSourceNote(saved, "", 2, 62));
        Assert.Equal(0UL, CubaseAraLyrics.ResolveSourceNote(saved, "", 0, 72));
        var projection = Score();
        var notes = CubaseAraLyrics.SourcePart(projection.File).Notes;
        notes[1].Pos = notes[0].Pos;
        notes[1].Pitch = notes[0].Pitch;
        var ambiguous = CubaseAraMerge.UpdateState([], projection);
        Assert.Equal(0UL, CubaseAraLyrics.ResolveSourceNote(ambiguous, "", 0, 60));
        Assert.Throws<ArgumentException>(() => CubaseAraLyrics.ResolveSourceNote(saved, "wrong", 0, 60));
    }

    [Fact]
    public void ResolveIncludesSourceOriginAndPartAnchor()
    {
        var envelope = CubaseProjectState.DecodeEnvelope(State());
        envelope.Ara!.SourceZeroSongQuarter = -4;
        var baseline = CubaseProjectState.Decode(envelope.Ara.BaselineState);
        CubaseAraLyrics.SourcePart(baseline).Pos = 240;
        envelope.Ara.BaselineState = CubaseProjectState.Encode(baseline);
        var saved = CubaseProjectState.Encode(envelope.Project, envelope.Ara);
        Assert.Equal(11UL, CubaseAraLyrics.ResolveSourceNote(saved, "", -3.5, 60));
        Assert.Equal(0UL, CubaseAraLyrics.ResolveSourceNote(saved, "", 0, 60));
    }

    [Theory]
    [InlineData("你好🌸")]
    [InlineData("こんにちは")]
    [InlineData("hello")]
    [InlineData("-")]
    [InlineData("")]
    public void SuccessiveTextUpdatesSurviveRoundTripAndAraRefresh(string text)
    {
        var first = CubaseAraLyrics.UpdateState(State(), "", [new(11, "先")]);
        var second = CubaseAraLyrics.UpdateState(first.State, "", [new(11, text)]);
        Assert.Equal(new ulong[] { 11 }, second.ChangedNotes);
        Assert.Empty(second.Conflicts);
        var incoming = Score();
        var refreshed = CubaseProjectState.DecodeEnvelope(CubaseAraMerge.UpdateState(second.State, incoming));
        Assert.Equal(text, CubaseAraLyrics.SourcePart(refreshed.Project).Notes[0].Lyric);
        Assert.Equal(text, CubaseAraLyrics.SourcePart(CubaseProjectState.Decode(refreshed.Ara!.BaselineState)).Notes[0].Lyric);
        Assert.Equal("", CubaseAraLyrics.SourcePart(incoming.File).Notes[0].Lyric);
        var repeated = CubaseAraLyrics.UpdateState(CubaseProjectState.Encode(refreshed.Project, refreshed.Ara),
            "", [new(11, text)]);
        Assert.Empty(repeated.ChangedNotes);
        Assert.Empty(repeated.Conflicts);
    }

    [Fact]
    public void LocalLyricWinsWhileHostBaselineAdvancesAcrossRefresh()
    {
        var first = CubaseAraLyrics.UpdateState(State(), "", [new(11, "host")]);
        var edited = CubaseProjectState.DecodeEnvelope(first.State);
        CubaseAraLyrics.SourcePart(edited.Project).Notes[0].Lyric = "local";
        var saved = CubaseProjectState.Encode(edited.Project, edited.Ara);
        var result = CubaseAraLyrics.UpdateState(saved, "", [new(11, "new host")]);
        Assert.Empty(result.ChangedNotes);
        Assert.Equal(new ulong[] { 11 }, result.Conflicts);
        var refreshed = CubaseAraMerge.UpdateState(result.State, Score());
        var next = CubaseAraLyrics.UpdateState(refreshed, "", [new(11, "third host")]);
        Assert.Equal(new ulong[] { 11 }, next.Conflicts);
        Assert.Equal("local", CubaseAraLyrics.SourcePart(CubaseProjectState.Decode(next.State)).Notes[0].Lyric);
        Assert.Equal(saved, CubaseProjectState.Encode(edited.Project, edited.Ara));
    }

    [Fact]
    public void RepeatedConflictPersistsUntilTheUserResolvesTheLocalLyric()
    {
        var edited = CubaseProjectState.DecodeEnvelope(State());
        CubaseAraLyrics.SourcePart(edited.Project).Notes[0].Lyric = "local";
        var first = CubaseAraLyrics.UpdateState(CubaseProjectState.Encode(edited.Project, edited.Ara),
            "", [new(11, "host")]);
        var repeated = CubaseAraLyrics.UpdateState(first.State, "", [new(11, "host")]);
        Assert.Equal(new ulong[] { 11 }, repeated.Conflicts);
        Assert.Equal(first.State, repeated.State);
        var resolved = CubaseProjectState.DecodeEnvelope(repeated.State);
        CubaseAraLyrics.SourcePart(resolved.Project).Notes[0].Lyric = "host";
        var result = CubaseAraLyrics.UpdateState(CubaseProjectState.Encode(resolved.Project, resolved.Ara),
            "", [new(11, "host")]);
        Assert.Empty(result.Conflicts);
        Assert.Empty(result.ChangedNotes);
    }

    [Theory]
    [InlineData(false)]
    [InlineData(true)]
    public void ExplicitPronunciationOrPhonemesArePreserved(bool phonemes)
    {
        var edited = CubaseProjectState.DecodeEnvelope(State());
        var note = CubaseAraLyrics.SourcePart(edited.Project).Notes[0];
        if (phonemes)
            note.BodyPhonemes.Add(new PhonemeInfo { Symbol = "a", Duration = 0.5 });
        else
            note.Pronunciation = "custom";
        var result = CubaseAraLyrics.UpdateState(CubaseProjectState.Encode(edited.Project, edited.Ara),
            "", [new(11, "host")]);
        Assert.Equal(new ulong[] { 11 }, result.Conflicts);
        var restored = CubaseAraLyrics.SourcePart(CubaseProjectState.Decode(result.State)).Notes[0];
        Assert.Equal(note.Pronunciation, restored.Pronunciation);
        Assert.Equal(note.BodyPhonemes.Count, restored.BodyPhonemes.Count);
    }

    [Fact]
    public void DeletedNotesStayDeletedAndOtherNotesRemainUntouched()
    {
        var edited = CubaseProjectState.DecodeEnvelope(State());
        CubaseAraLyrics.SourcePart(edited.Project).Notes.RemoveAt(0);
        edited.Ara!.CurrentNoteIds = [22];
        var result = CubaseAraLyrics.UpdateState(CubaseProjectState.Encode(edited.Project, edited.Ara),
            "", [new(11, "deleted"), new(22, "active")]);
        Assert.Equal(new ulong[] { 22 }, result.ChangedNotes);
        var refreshed = CubaseProjectState.DecodeEnvelope(CubaseAraMerge.UpdateState(result.State, Score()));
        Assert.Equal(new ulong[] { 22 }, refreshed.Ara!.CurrentNoteIds);
        Assert.Equal("active", Assert.Single(CubaseAraLyrics.SourcePart(refreshed.Project).Notes).Lyric);
    }

    [Fact]
    public void WrongSourceUnknownAndDuplicateIdsRejectWithoutChangingInput()
    {
        var state = State();
        var original = state.ToArray();
        Assert.Throws<ArgumentException>(() => CubaseAraLyrics.UpdateState(state, "other", [new(11, "x")]));
        Assert.Throws<ArgumentException>(() => CubaseAraLyrics.UpdateState(state, "", [new(11, "x"), new(99, "y")]));
        Assert.Throws<ArgumentException>(() => CubaseAraLyrics.UpdateState(state, "", [new(11, "x"), new(11, "y")]));
        Assert.Equal(original, state);
    }
}
