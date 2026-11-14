using System;
using System.Linq;
using System.Threading;
using TuneLab.Data;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.Hosting;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

[Collection("DataThreadProject")]
public class CubaseRenderSessionTests
{
    static NativeProjectFile Score()
    {
        var file = new NativeProjectFile();
        file.Project.Tracks.Add(new TrackInfo
        {
            Parts = { new MidiPartInfo
            {
                EndOffset = 960,
                Notes =
                {
                    new NoteInfo { Pos = 0, Dur = 240, Pitch = 60, Lyric = "啊" },
                    new NoteInfo { Pos = 480, Dur = 240, Pitch = 62, Lyric = "好" }
                }
            } }
        });
        return file;
    }

    [Fact]
    public void LyricRevisionRetainsProjectAndNotesAndMatchesColdRender()
    {
        TestVoices.EnsureBuiltIn();
        using var session = new CubaseRenderSession();
        var file = Score();
        session.Render(CubaseProjectState.Encode(file), TimeSpan.FromSeconds(5));
        var project = session.Project!;
        var part = project.AllMidiParts().First();
        var first = part.Notes.First!;
        var second = part.Notes.Last!;
        var incoming = (MidiPartInfo)file.Project.Tracks[0].Parts[0];
        incoming.Notes[0].Lyric = "你";
        incoming.Notes[0].Pronunciation = "ni";
        var state = CubaseProjectState.Encode(file);
        var warm = session.Render(state, TimeSpan.FromSeconds(5));
        Assert.Same(project, session.Project);
        Assert.Same(first, part.Notes.First);
        Assert.Same(second, part.Notes.Last);
        Assert.Equal("你", first.Lyric.Value);
        Assert.Equal("ni", first.Pronunciation.Value);
        Assert.Equal("好", second.Lyric.Value);
        Assert.False(project.Document!.Undoable());
        var cold = CubaseSnapshotRenderer.Render(state, TimeSpan.FromSeconds(5));
        Assert.Equal(cold.SampleRate, warm.SampleRate);
        Assert.Equal(cold.Interleaved, warm.Interleaved);
    }

    [Fact]
    public void StructuralChangeRebuildsInsteadOfReusingIncompatibleNotes()
    {
        TestVoices.EnsureBuiltIn();
        using var session = new CubaseRenderSession();
        var file = Score();
        session.Render(CubaseProjectState.Encode(file), TimeSpan.FromSeconds(5));
        var previous = session.Project;
        ((MidiPartInfo)file.Project.Tracks[0].Parts[0]).Notes[0].Pitch = 65;
        session.Render(CubaseProjectState.Encode(file), TimeSpan.FromSeconds(5));
        Assert.NotSame(previous, session.Project);
        Assert.Equal(65, session.Project!.AllMidiParts().First().Notes.First!.Pitch.Value);
    }

    [Fact]
    public void FixedPhonemesSurviveLyricChangesThroughFullRestore()
    {
        TestVoices.EnsureBuiltIn();
        using var session = new CubaseRenderSession();
        var file = Score();
        var note = ((MidiPartInfo)file.Project.Tracks[0].Parts[0]).Notes[0];
        note.BodyPhonemes.Add(new PhonemeInfo { Symbol = "a", Duration = 0.25 });
        session.Render(CubaseProjectState.Encode(file), TimeSpan.FromSeconds(5));
        var previous = session.Project;
        note.Lyric = "你";
        session.Render(CubaseProjectState.Encode(file), TimeSpan.FromSeconds(5));
        Assert.NotSame(previous, session.Project);
        var restored = session.Project!.AllMidiParts().First().Notes.First!;
        Assert.Equal("你", restored.Lyric.Value);
        Assert.Equal("a", Assert.Single(restored.BodyPhonemes).Symbol.Value);
    }

    [Fact]
    public void PreCanceledRequestPreservesCacheAndInvalidStateReleasesIt()
    {
        TestVoices.EnsureBuiltIn();
        using var session = new CubaseRenderSession();
        var state = CubaseProjectState.Encode(Score());
        session.Render(state, TimeSpan.FromSeconds(5));
        var previous = session.Project;
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Assert.Throws<OperationCanceledException>(() => session.Render(state, TimeSpan.FromSeconds(5), cancellation.Token));
        Assert.Same(previous, session.Project);
        Assert.ThrowsAny<Exception>(() => session.Render([0], TimeSpan.FromSeconds(5)));
        Assert.Null(session.Project);
        session.Render(state, TimeSpan.FromSeconds(5));
        Assert.NotSame(previous, session.Project);
    }
}
