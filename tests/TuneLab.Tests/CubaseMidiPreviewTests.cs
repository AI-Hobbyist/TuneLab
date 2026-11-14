using System;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.Hosting;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

public class CubaseMidiPreviewTests
{
    [Fact]
    public void PreviewUsesSelectedSourceAndLyricWithoutChangingSavedSong()
    {
        var file = new NativeProjectFile();
        file.Project.Tempos.Add(new TempoInfo { Pos = 0, Bpm = 83 });
        file.Project.Tracks.Add(new TrackInfo
        {
            Parts =
            {
                new MidiPartInfo
                {
                    Pos = 4800, StartOffset = 240, EndOffset = 1920,
                    SoundSource = new SoundSourceInfo { Type = "engine", Id = "singer" },
                    Notes =
                    {
                        new NoteInfo { Pos = 360, Dur = 240, Pitch = 60, Lyric = "啦", Pronunciation = "la" },
                        new NoteInfo { Pos = 960, Dur = 480, Pitch = 62, Lyric = "你" }
                    }
                }
            }
        });
        var state = CubaseProjectState.Encode(file);
        var before = (byte[])state.Clone();
        var preview = CubaseMidiPreview.CreateScore(state, 67);
        var part = Assert.IsType<MidiPartInfo>(Assert.Single(Assert.Single(preview.Project.Tracks).Parts));
        var note = Assert.Single(part.Notes);
        Assert.Equal("engine", part.SoundSource.Type);
        Assert.Equal("singer", part.SoundSource.Id);
        Assert.Equal(0, part.Pos);
        Assert.Equal(0, part.StartOffset);
        Assert.Equal(960, part.EndOffset);
        Assert.Equal(120, Assert.Single(preview.Project.Tempos).Bpm);
        Assert.Equal(67, note.Pitch);
        Assert.Equal(0, note.Pos);
        Assert.Equal(960, note.Dur);
        Assert.Equal("啦", note.Lyric);
        Assert.Equal("la", note.Pronunciation);
        Assert.Equal(before, state);
        var original = CubaseProjectState.Decode(state);
        var originalPart = Assert.IsType<MidiPartInfo>(Assert.Single(Assert.Single(original.Project.Tracks).Parts));
        Assert.Equal(2, originalPart.Notes.Count);
        Assert.Equal(4800, originalPart.Pos);
        Assert.Equal(83, Assert.Single(original.Project.Tempos).Bpm);
    }

    [Theory]
    [InlineData(-1)]
    [InlineData(128)]
    public void RejectsInvalidMidiPitchBeforeReadingState(int pitch)
    {
        Assert.Throws<ArgumentOutOfRangeException>(() => CubaseMidiPreview.CreateScore([], pitch));
    }

    [Fact]
    public void RequiresSoundSourcePart()
    {
        Assert.Throws<InvalidOperationException>(() => CubaseMidiPreview.CreateScore(
            CubaseProjectState.Encode(new NativeProjectFile()), 60));
    }
}
