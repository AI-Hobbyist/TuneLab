using System;
using System.Buffers.Binary;
using System.IO;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.Hosting;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

public class CubaseProjectStateTests
{
    [Fact]
    public void NativeProjectMusicSourceAndEditorMetadataRoundTrip()
    {
        var file = new NativeProjectFile();
        file.Project.Tempos.Add(new TempoInfo { Pos = 0, Bpm = 126 });
        var track = new TrackInfo { Name = "Voice track" };
        var part = new MidiPartInfo
        {
            Name = "Phrase", Pos = 960, StartOffset = 120, EndOffset = 2040,
            SoundSource = new SoundSourceInfo { Kind = SourceKind.Voice, Type = "DiffSinger", Id = "fu2_ning2_na4" }
        };
        part.Notes.Add(new NoteInfo { Pos = 240, Dur = 480, Pitch = 62, Lyric = "啦", Pronunciation = "la" });
        track.Parts.Add(part);
        file.Project.Tracks.Add(track);
        file.Editor.PlayheadPos = 1320;
        file.Editor.ParameterPanels = new() { new ParameterPanelInfo { Height = 220, Collapsed = true } };
        file.Export.SampleRate = 48000;

        var restored = CubaseProjectState.Decode(CubaseProjectState.Encode(file));
        var restoredTrack = Assert.Single(restored.Project.Tracks);
        var restoredPart = Assert.IsType<MidiPartInfo>(Assert.Single(restoredTrack.Parts));
        Assert.Equal("Voice track", restoredTrack.Name);
        Assert.Equal(126, Assert.Single(restored.Project.Tempos).Bpm);
        Assert.Equal(960, restoredPart.Pos);
        Assert.Equal(120, restoredPart.StartOffset);
        Assert.Equal(2040, restoredPart.EndOffset);
        Assert.Equal("DiffSinger", restoredPart.SoundSource.Type);
        Assert.Equal("fu2_ning2_na4", restoredPart.SoundSource.Id);
        var note = Assert.Single(restoredPart.Notes);
        Assert.Equal("啦", note.Lyric);
        Assert.Equal("la", note.Pronunciation);
        Assert.Equal(62, note.Pitch);
        Assert.Equal(480, note.Dur);
        Assert.Equal(1320, restored.Editor.PlayheadPos);
        Assert.True(Assert.Single(restored.Editor.ParameterPanels!).Collapsed);
        Assert.Equal(48000, restored.Export.SampleRate);
    }

    [Fact]
    public void UnknownVersionIsRejected()
    {
        var bytes = CubaseProjectState.Encode(new NativeProjectFile());
        BinaryPrimitives.WriteUInt32LittleEndian(bytes.AsSpan(8), 2);
        Assert.Throws<NotSupportedException>(() => CubaseProjectState.Decode(bytes));
    }

    [Fact]
    public void CorruptionTruncationAndExtraDataAreRejected()
    {
        var bytes = CubaseProjectState.Encode(new NativeProjectFile());
        Assert.Throws<InvalidDataException>(() => CubaseProjectState.Decode(bytes.AsSpan(0, 7)));
        Assert.Throws<InvalidDataException>(() => CubaseProjectState.Decode(bytes.AsSpan(0, bytes.Length - 1)));
        var extra = new byte[bytes.Length + 1];
        bytes.CopyTo(extra, 0);
        Assert.Throws<InvalidDataException>(() => CubaseProjectState.Decode(extra));
        BinaryPrimitives.WriteInt32LittleEndian(extra.AsSpan(12), extra.Length - 16);
        Assert.Throws<InvalidDataException>(() => CubaseProjectState.Decode(extra));
        bytes[0] = 0;
        Assert.Throws<InvalidDataException>(() => CubaseProjectState.Decode(bytes));
    }
}
