using System;
using System.IO;
using System.Linq;
using TuneLab.Audio;
using TuneLab.Audio.NAudio;
using TuneLab.Extensions;
using TuneLab.Data;
using TuneLab.Data.Synthesis;
using TuneLab.Hosting;
using TuneLab.SDK;

if (args.Length != 1)
{
    Console.Error.WriteLine("Usage: RenderCacheProbe <local-project-state-with-installed-voice>");
    return 1;
}

// Initialize the same core and engine services as PluginServices, without a GUI.
TuneLab.Program.InitCoreServices();
AudioUtils.Init(new NAudioCodec());
LegacyCompatLoader.Wire(Path.GetDirectoryName(typeof(TuneLab.Cubase.Managed.NativeEntry).Assembly.Location));
ExtensionManager.LoadExtensions();
SoundSourceEngines.InitAll();
var file = CubaseProjectState.Decode(File.ReadAllBytes(args[0]));
var partInfo = file.Project.Tracks.SelectMany(t => t.Parts).OfType<MidiPartInfo>().First();
partInfo.Notes.Clear();
partInfo.Notes.Add(new NoteInfo { Pos = 480, Dur = 480, Pitch = 60, Lyric = "啊" });
partInfo.Notes.Add(new NoteInfo { Pos = 3840, Dur = 480, Pitch = 62, Lyric = "好" });
partInfo.EndOffset = 4800;
using var session = new CubaseRenderSession();
var before = session.Render(CubaseProjectState.Encode(file), TimeSpan.FromMinutes(3));
var project = session.Project!;
var part = (MidiPart)project.AllMidiParts().First();
var pipeline = (VoiceSynthesisPipeline)part.SynthesisPipeline!;
var segments = pipeline.Context.AudioSegments.OrderBy(s => s.SampleOffset).ToArray();
if (segments.Length < 2)
{
    throw new Exception($"Expected separate real synthesis segments, got {segments.Length}");
}
var untouched = segments.Last();
var version = untouched.CommitVersion;
var samples = untouched.Samples;
var output = part.SynthesizedSegments.OrderBy(s => s.Audio.StartTime).Last();
partInfo.Notes[0].Lyric = "你";
var after = session.Render(CubaseProjectState.Encode(file), TimeSpan.FromMinutes(3));
if (!ReferenceEquals(project, session.Project) || !ReferenceEquals(pipeline, part.SynthesisPipeline))
{
    throw new Exception("Retained rendering rebuilt project or pipeline");
}
if (!pipeline.Context.AudioSegments.Contains(untouched) || untouched.CommitVersion != version || !ReferenceEquals(samples, untouched.Samples))
{
    throw new Exception("Unchanged real synthesis segment was replaced or recommitted");
}
var outputAfter = part.SynthesizedSegments.OrderBy(s => s.Audio.StartTime).Last();
if (!ReferenceEquals(output.Audio.Samples, outputAfter.Audio.Samples) || !ReferenceEquals(output.Waveform, outputAfter.Waveform))
{
    throw new Exception("Unchanged output audio or waveform cache was replaced");
}
if (!after.Interleaved.All(float.IsFinite) || !after.Interleaved.Any(s => s != 0))
{
    throw new Exception("Real revised audio is silent or nonfinite");
}
var change = before.Interleaved.Zip(after.Interleaved, (a, b) => Math.Abs(a - b)).Average();
if (change <= 0)
{
    throw new Exception("Changed lyric produced identical complete audio");
}
Console.WriteLine($"PASS: real singer segments={segments.Length}; untouched offset={untouched.SampleOffset}, commitVersion={version} unchanged; segment/buffer/output/waveform references retained; changed audio mean={change}");

var cold = CubaseSnapshotRenderer.Render(CubaseProjectState.Encode(file), TimeSpan.FromMinutes(3));
if (cold.SampleRate != after.SampleRate || cold.Interleaved.Length != after.Interleaved.Length)
{
    throw new Exception("Cold and retained rendering geometry differs");
}
var maximumError = cold.Interleaved.Zip(after.Interleaved, (a, b) => Math.Abs(a - b)).Max();
if (maximumError > 1e-5f)
{
    throw new Exception($"Cold and retained rendering differ: maximum error {maximumError}");
}
Console.WriteLine($"PASS: retained real synthesis matches cold render, maximum error={maximumError}");
return 0;
