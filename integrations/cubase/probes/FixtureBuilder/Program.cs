using System;
using System.Buffers.Binary;
using System.Collections.Generic;
using System.IO;
using System.Linq;
using System.Text;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.Hosting;
using TuneLab.SDK;

if (args.Length != 1)
{
    Console.Error.WriteLine("Usage: FixtureBuilder <output-directory>");
    return 1;
}
Directory.CreateDirectory(args[0]);
var file = new NativeProjectFile();
file.Project.Tempos.Clear();
file.Project.Tempos.Add(new TempoInfo { Pos = 0, Bpm = 120 });
file.Project.Tempos.Add(new TempoInfo { Pos = 3840, Bpm = 90 });
file.Project.TimeSignatures.Clear();
file.Project.TimeSignatures.Add(new TimeSignatureInfo { BarIndex = 0, Numerator = 4, Denominator = 4 });
file.Project.TimeSignatures.Add(new TimeSignatureInfo { BarIndex = 2, Numerator = 3, Denominator = 4 });
var part = new MidiPartInfo
{
    Name = "Choose an installed engine and singer",
    EndOffset = 5760,
    Notes =
    {
        new NoteInfo { Pos = 480, Dur = 480, Pitch = 60, Lyric = "啊" },
        new NoteInfo { Pos = 3840, Dur = 480, Pitch = 62, Lyric = "好" },
        new NoteInfo { Pos = 4320, Dur = 480, Pitch = 62, Lyric = "-" }
    }
};
file.Project.Tracks.Add(new TrackInfo { Name = "Cubase delivery fixture", Parts = { part } });
using (var output = File.Create(Path.Combine(args[0], "tempo-lyrics.tlpx")))
{
    new TuneLabProjectCbor().SerializeNative(output, file);
}
using (var input = File.OpenRead(Path.Combine(args[0], "tempo-lyrics.tlpx")))
{
    var restored = new TuneLabProjectCbor().DeserializeNative(input);
    if (!((MidiPartInfo)restored.Project.Tracks[0].Parts[0]).Notes.Select(note => note.Lyric)
        .SequenceEqual(new[] { "啊", "好", "-" }))
    {
        throw new Exception("Native delivery fixture did not round-trip its lyrics.");
    }
}
var metadata = new CubaseAraMetadata
{
    SourceId = "delivery-fixture",
    BaselineState = CubaseProjectState.Encode(file),
    BaselineNoteIds = [11, 22, 33],
    CurrentNoteIds = [11, 22, 33]
};
var state = CubaseProjectState.Encode(file, metadata);
if (!CubaseProjectState.DecodeEnvelope(state).Ara!.CurrentNoteIds.SequenceEqual(new ulong[] { 11, 22, 33 }))
{
    throw new Exception("Associated delivery fixture did not round-trip its identities.");
}
File.WriteAllBytes(Path.Combine(args[0], "tempo-lyrics.state"), state);

var events = new List<(int Tick, byte[] Data)>
{
    (0, [0xff, 0x51, 3, 0x07, 0xa1, 0x20]),
    (0, [0xff, 0x58, 4, 4, 2, 24, 8]),
    (3840, [0xff, 0x51, 3, 0x0a, 0x2c, 0x2b]),
    (3840, [0xff, 0x58, 4, 3, 2, 24, 8])
};
foreach (var note in part.Notes)
{
    byte[] text = Encoding.UTF8.GetBytes(note.Lyric);
    using var lyric = new MemoryStream();
    lyric.Write([0xff, 0x05]);
    WriteVariable(lyric, text.Length);
    lyric.Write(text);
    events.Add(((int)note.Pos, lyric.ToArray()));
    events.Add(((int)note.Pos, [0x90, (byte)note.Pitch, 100]));
    events.Add(((int)(note.Pos + note.Dur), [0x80, (byte)note.Pitch, 0]));
}
using var track = new MemoryStream();
int previous = 0;
foreach (var entry in events.OrderBy(entry => entry.Tick))
{
    WriteVariable(track, entry.Tick - previous);
    track.Write(entry.Data);
    previous = entry.Tick;
}
WriteVariable(track, 5760 - previous);
track.Write([0xff, 0x2f, 0]);
using (var midi = File.Create(Path.Combine(args[0], "tempo-lyrics.mid")))
{
    WriteChunk(midi, "MThd", [0, 0, 0, 1, 1, 0xe0]);
    WriteChunk(midi, "MTrk", track.ToArray());
}
Console.WriteLine("PASS: native project and associated state round-trip; SMF0 fixture generated without an engine or model.");
return 0;

static void WriteVariable(Stream output, int value)
{
    Span<byte> bytes = stackalloc byte[4];
    int index = 3;
    bytes[index] = (byte)(value & 0x7f);
    while ((value >>= 7) != 0)
    {
        bytes[--index] = (byte)((value & 0x7f) | 0x80);
    }
    output.Write(bytes[index..]);
}

static void WriteChunk(Stream output, string name, byte[] payload)
{
    output.Write(Encoding.ASCII.GetBytes(name));
    Span<byte> length = stackalloc byte[4];
    BinaryPrimitives.WriteInt32BigEndian(length, payload.Length);
    output.Write(length);
    output.Write(payload);
}
