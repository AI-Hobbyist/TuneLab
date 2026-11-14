using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Threading;
using TuneLab.Audio;
using TuneLab.Data;
using TuneLab.Data.Synthesis;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.Foundation;
using TuneLab.Scripting;
using TuneLab.SDK;

namespace TuneLab.Hosting;

// Retaining this data-thread project retains the engine's own range/cache
// decisions. It never shares the live UI document or standalone audio graph.
internal sealed class CubaseRenderSession : IDisposable
{
    readonly int mOwnerThread = Environment.CurrentManagedThreadId;
    readonly PumpableSynchronizationContext mPump = new();
    DataDocument mDocument = new();
    Project? mProject;
    byte[]? mStructure;
    double mMeterOrigin;
    bool mDisposed;

    internal Project? Project => mProject;

    public CubaseRenderedAudio Render(byte[] state, TimeSpan timeout,
        CancellationToken cancellation = default, int? previewPitch = null)
    {
        VerifyThread();
        ObjectDisposedException.ThrowIf(mDisposed, this);
        cancellation.ThrowIfCancellationRequested();
        var previousContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(mPump);
        try
        {
            var envelope = previewPitch.HasValue ? null : CubaseProjectState.DecodeEnvelope(state);
            var file = previewPitch.HasValue ? CubaseMidiPreview.CreateScore(state, previewPitch.Value) : envelope!.Project;
            double origin = envelope?.Ara?.MeterOriginTick ?? 0;
            var structure = Structure(file);
            if (previewPitch.HasValue || mProject == null || mMeterOrigin != origin ||
                mStructure == null || !mStructure.AsSpan().SequenceEqual(structure) || !PatchLyrics(file))
            {
                ReleaseProject();
                mProject = new Project(file.Project, registerWithStandaloneAudio: false, meterOriginTick: origin);
                mDocument = new DataDocument();
                mProject.Attach(mDocument);
                mStructure = structure;
                mMeterOrigin = origin;
            }
            var project = mProject;
            if (previewPitch.HasValue)
            {
                var part = project.AllMidiParts().First();
                var note = part.Notes.First!;
                if (string.IsNullOrEmpty(note.Lyric.Value))
                    note.Lyric.Set(part.SoundSource.DefaultLyric);
                mDocument.Commit();
                mDocument.Clear();
            }
            var clock = Stopwatch.StartNew();
            while (true)
            {
                cancellation.ThrowIfCancellationRequested();
                mPump.DrainAll();
                if (SynthesisCompletion.DriveOnce(project, SynthesisScope.All).Done)
                    break;
                if (clock.Elapsed >= timeout)
                    throw new TimeoutException("Cubase snapshot synthesis timed out.");
                mPump.WaitForWork(TimeSpan.FromMilliseconds(20));
            }
            var facts = SynthesisCompletion.Inspect(project, SynthesisScope.All);
            if (facts.Silent.Count > 0 || facts.Degraded.Count > 0)
                throw new InvalidOperationException("Cubase snapshot synthesis has failed or degraded ranges.");
            var graph = new AudioGraph { SampleRate = AudioEngine.SampleRate.Value };
            foreach (var track in project.Tracks)
                graph.AddTrack(track);
            int frames = checked((int)Math.Ceiling(graph.EndTime * graph.SampleRate));
            var samples = new float[checked(frames * 2)];
            const int blockSize = 4096;
            for (int position = 0; position < frames; position += blockSize)
            {
                cancellation.ThrowIfCancellationRequested();
                int end = Math.Min(frames, position + blockSize);
                graph.MixData(position, end, true, samples, position * 2);
            }
            return new(graph.SampleRate, samples);
        }
        catch
        {
            // A failed revision cannot leave a half-applied project reusable.
            ReleaseProject();
            throw;
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previousContext);
        }
    }

    static byte[] Structure(NativeProjectFile file)
    {
        var saved = new List<(NoteInfo Note, string Lyric, string Pronunciation)>();
        foreach (var note in file.Project.Tracks.SelectMany(track => track.Parts)
            .OfType<MidiPartInfo>().SelectMany(part => part.Notes))
        {
            saved.Add((note, note.Lyric, note.Pronunciation));
            note.Lyric = string.Empty;
            note.Pronunciation = string.Empty;
        }
        try
        {
            // The existing serializer covers every other field, including
            // engine properties, expressions, effects, fixed phonemes and audio.
            return CubaseProjectState.Encode(file);
        }
        finally
        {
            foreach (var entry in saved)
            {
                entry.Note.Lyric = entry.Lyric;
                entry.Note.Pronunciation = entry.Pronunciation;
            }
        }
    }

    bool PatchLyrics(NativeProjectFile file)
    {
        var incoming = file.Project.Tracks.SelectMany(track => track.Parts).OfType<MidiPartInfo>()
            .SelectMany(part => part.Notes).ToArray();
        var current = mProject!.AllMidiParts().SelectMany(part => part.Notes).ToArray();
        // Equal structural bytes guarantee matching order/geometry. A lyric
        // setter clears fixed phonemes, so that case requires a full restore.
        for (int index = 0; index < current.Length; index++)
        {
            var target = incoming[index];
            if ((current[index].Lyric.Value != target.Lyric || current[index].Pronunciation.Value != target.Pronunciation) &&
                (target.LeadingPhonemes.Count != 0 || target.BodyPhonemes.Count != 0))
                return false;
        }
        for (int index = 0; index < current.Length; index++)
        {
            var note = current[index];
            var target = incoming[index];
            if (note.Lyric.Value != target.Lyric)
                note.Lyric.Set(target.Lyric);
            if (note.Pronunciation.Value != target.Pronunciation)
                note.Pronunciation.Set(target.Pronunciation);
        }
        mDocument.Commit();
        // Render workers expose no undo. Retain caches, not render history.
        mDocument.Clear();
        return true;
    }

    void ReleaseProject()
    {
        var project = mProject;
        mProject = null;
        mStructure = null;
        if (project == null)
            return;
        var pipelines = project.AllMidiParts().OfType<MidiPart>()
            .Select(part => part.SynthesisPipeline).Where(pipeline => pipeline != null).ToArray();
        project.Detach();
        project.Dispose();
        mDocument.Clear();
        while (pipelines.Any(pipeline => pipeline!.IsBusy))
        {
            mPump.DrainAll();
            mPump.WaitForWork(TimeSpan.FromMilliseconds(20));
        }
        mPump.DrainAll();
    }

    void VerifyThread()
    {
        if (Environment.CurrentManagedThreadId != mOwnerThread)
            throw new InvalidOperationException("A Cubase render session must stay on its owner thread.");
    }

    public void Dispose()
    {
        VerifyThread();
        if (mDisposed)
            return;
        var previousContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(mPump);
        try
        {
            ReleaseProject();
            mDisposed = true;
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previousContext);
        }
    }
}
