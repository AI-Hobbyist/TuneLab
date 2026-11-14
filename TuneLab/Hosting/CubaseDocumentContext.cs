using System;
using System.Linq;
using System.Collections.Generic;
using TuneLab.Data;
using TuneLab.Input;
using TuneLab.SDK;
using TuneLab.Foundation;
using TuneLab.Extensions.Formats.TLP;

namespace TuneLab.Hosting;

// Each plug-in document owns its editing history and project. It does not
// install a global command provider or start the standalone audio device.
internal sealed class CubaseDocumentContext : IDisposable
{
    readonly ProjectDocument mDocument = new();
    bool mDisposed;
    bool mRestoring;
    CubaseAraMetadata? mAraMetadata;
    readonly Dictionary<INote, ulong> mAraNoteIds = new();

    public CubaseDocumentContext()
    {
        mDocument.StatusChanged += OnDocumentStatusChanged;
    }

    public event Action<byte[]>? StateChanged;

    void OnDocumentStatusChanged()
    {
        if (!mRestoring && !mDocument.HasUncommittedChanges && mDocument.Project != null)
        {
            StateChanged?.Invoke(SaveState());
        }
    }

    public EditorActionCollection Actions { get; } = new();
    public EditorInfo EditorMetadata { get; private set; } = new();

    public byte[] SaveState()
    {
        var project = Document.Project
            ?? throw new InvalidOperationException("The plug-in document has no project.");
        if (mAraMetadata != null)
        {
            var part = project.AllMidiParts().FirstOrDefault();
            mAraMetadata.CurrentNoteIds = part?.Notes.Select(note =>
                mAraNoteIds.TryGetValue(note, out var id) ? id : 0UL).ToArray() ?? [];
        }
        return CubaseProjectState.Encode(new NativeProjectFile
        {
            Project = project.GetInfo(),
            Editor = EditorMetadata,
            Export = project.GetExportConfig()
        }, mAraMetadata);
    }

    public void RestoreState(ReadOnlySpan<byte> state)
    {
        // Reject an invalid payload before touching the current document.
        ObjectDisposedException.ThrowIf(mDisposed, this);
        var envelope = CubaseProjectState.DecodeEnvelope(state);
        var file = envelope.Project;
        var project = new Project(file.Project, registerWithStandaloneAudio: false,
            meterOriginTick: envelope.Ara?.MeterOriginTick ?? 0);
        project.SetExportConfig(file.Export);
        mRestoring = true;
        try
        {
            Document.SetProject(project);
            EditorMetadata = file.Editor;
            mAraMetadata = envelope.Ara;
            mAraNoteIds.Clear();
            if (mAraMetadata != null)
            {
                var notes = project.AllMidiParts().First().Notes.ToArray();
                for (int index = 0; index < notes.Length; index++)
                    mAraNoteIds.Add(notes[index], mAraMetadata.CurrentNoteIds[index]);
            }
        }
        finally
        {
            mRestoring = false;
        }
    }

    // Called by host ingress on the document thread. Keep existing objects,
    // selections and undo history; publish one settled snapshot to the normal
    // native state/render observers, including baseline-only conflict updates.
    public CubaseAraLyricsResult ApplyHostLyrics(string sourceId, IReadOnlyList<CubaseAraLyricUpdate> updates)
    {
        ObjectDisposedException.ThrowIf(mDisposed, this);
        if (Document.HasUncommittedChanges)
            throw new InvalidOperationException("A document edit is already active.");
        var before = SaveState();
        var result = CubaseAraLyrics.UpdateState(before, sourceId, updates);
        if (before.AsSpan().SequenceEqual(result.State))
            return result;
        var updated = CubaseProjectState.DecodeEnvelope(result.State);
        var targetNotes = CubaseAraLyrics.SourcePart(updated.Project).Notes;
        var part = Document.Project!.AllMidiParts().First();
        var notes = part.Notes.ToArray();
        var changed = result.ChangedNotes.ToHashSet();
        mRestoring = true;
        try
        {
            for (int index = 0; index < notes.Length; index++)
            {
                if (!mAraNoteIds.TryGetValue(notes[index], out ulong id) || !changed.Contains(id))
                    continue;
                notes[index].Lyric.Set(targetNotes[index].Lyric);
                // Keep an empty explicit override. FinalPronunciation applies
                // the current G2P setting; generated text is not a user edit.
                notes[index].Pronunciation.Set(targetNotes[index].Pronunciation);
            }
            mAraMetadata = updated.Ara;
            if (changed.Count != 0)
                Document.Commit();
        }
        finally
        {
            mRestoring = false;
        }
        var saved = SaveState();
        StateChanged?.Invoke(saved);
        return result with { State = saved };
    }

    // Called on the editing/data thread. Reopening a view reuses the same part
    // and history rather than constructing a replacement project.
    public IMidiPart GetOrCreateEditingPart()
    {
        var project = Document.Project
            ?? throw new InvalidOperationException("The plug-in document has no project.");
        var existing = project.Tracks.SelectMany(track => track.Parts).OfType<IMidiPart>().FirstOrDefault();
        if (existing != null)
        {
            return existing;
        }

        var trackInfo = new NativeTrackInfo { Name = "TuneLab" };
        trackInfo.Parts.Add(new MidiPartInfo
        {
            Name = "TuneLab",
            Pos = 0,
            StartOffset = 0,
            EndOffset = 4 * MusicTheory.RESOLUTION
        });
        project.AddTrack(trackInfo);
        Document.Commit();
        return (IMidiPart)project.Tracks.Last().Parts.First();
    }

    public ProjectDocument Document
    {
        get
        {
            ObjectDisposedException.ThrowIf(mDisposed, this);
            return mDocument;
        }
    }

    public void Dispose()
    {
        if (mDisposed)
        {
            return;
        }

        mDisposed = true;
        mDocument.StatusChanged -= OnDocumentStatusChanged;
        StateChanged = null;
        Actions.Clear();
        mDocument.Project?.Detach();
        mDocument.Project?.Dispose();
        mDocument.Clear();
    }
}
