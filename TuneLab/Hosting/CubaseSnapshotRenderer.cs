using System;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using TuneLab.Audio;
using TuneLab.Data;
using TuneLab.Data.Synthesis;
using TuneLab.Scripting;

namespace TuneLab.Hosting;

internal sealed record CubaseRenderedAudio(int SampleRate, float[] Interleaved);

// Called by the render worker with a serialized snapshot, never a live UI
// document. Its private mixer cannot include tracks from another instance.
internal static class CubaseSnapshotRenderer
{
    public static CubaseRenderedAudio Render(byte[] state, TimeSpan timeout,
        CancellationToken cancellation = default)
        => RenderCore(state, timeout, cancellation, previewPitch: null);

    public static CubaseRenderedAudio RenderPreview(byte[] state, int pitch, TimeSpan timeout,
        CancellationToken cancellation = default)
        => RenderCore(state, timeout, cancellation, pitch);

    static CubaseRenderedAudio RenderCore(byte[] state, TimeSpan timeout,
        CancellationToken cancellation, int? previewPitch)
    {
        cancellation.ThrowIfCancellationRequested();
        var file = previewPitch.HasValue ? CubaseMidiPreview.CreateScore(state, previewPitch.Value)
            : CubaseProjectState.Decode(state);
        var previousContext = SynchronizationContext.Current;
        var pump = new PumpableSynchronizationContext();
        SynchronizationContext.SetSynchronizationContext(pump);
        Project? project = null;
        try
        {
            project = new Project(file.Project, registerWithStandaloneAudio: false);
            if (previewPitch.HasValue)
            {
                var part = project.AllMidiParts().First();
                var note = part.Notes.First!;
                if (string.IsNullOrEmpty(note.Lyric.Value))
                {
                    note.Lyric.Set(part.SoundSource.DefaultLyric);
                }
            }
            var clock = Stopwatch.StartNew();
            while (true)
            {
                cancellation.ThrowIfCancellationRequested();
                pump.DrainAll();
                if (SynthesisCompletion.DriveOnce(project, SynthesisScope.All).Done)
                {
                    break;
                }
                if (clock.Elapsed >= timeout)
                {
                    throw new TimeoutException("Cubase snapshot synthesis timed out.");
                }
                pump.WaitForWork(TimeSpan.FromMilliseconds(20));
            }
            var facts = SynthesisCompletion.Inspect(project, SynthesisScope.All);
            if (facts.Silent.Count > 0 || facts.Degraded.Count > 0)
            {
                throw new InvalidOperationException("Cubase snapshot synthesis has failed or degraded ranges.");
            }
            var graph = new AudioGraph { SampleRate = AudioEngine.SampleRate.Value };
            foreach (var track in project.Tracks)
            {
                graph.AddTrack(track);
            }
            int frames = checked((int)Math.Ceiling(graph.EndTime * graph.SampleRate));
            var samples = new float[checked(frames * 2)];
            const int blockSize = 4096;
            for (int position = 0; position < frames; position += blockSize)
            {
                cancellation.ThrowIfCancellationRequested();
                int end = Math.Min(frames, position + blockSize);
                graph.MixData(position, end, true, samples, position * 2);
            }
            return new CubaseRenderedAudio(graph.SampleRate, samples);
        }
        finally
        {
            // Dispose requests cancellation. Keep pumping until in-flight
            // session continuations have returned to this data thread.
            var pipelines = project?.AllMidiParts().OfType<MidiPart>()
                .Select(part => part.SynthesisPipeline).Where(pipeline => pipeline != null).ToArray();
            project?.Dispose();
            if (pipelines != null)
            {
                while (pipelines.Any(pipeline => pipeline!.IsBusy))
                {
                    pump.DrainAll();
                    pump.WaitForWork(TimeSpan.FromMilliseconds(20));
                }
            }
            pump.DrainAll();
            SynchronizationContext.SetSynchronizationContext(previousContext);
        }
    }
}
