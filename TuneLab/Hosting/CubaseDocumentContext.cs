using System;
using System.Linq;
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
        return CubaseProjectState.Encode(new NativeProjectFile
        {
            Project = project.GetInfo(),
            Editor = EditorMetadata,
            Export = project.GetExportConfig()
        });
    }

    public void RestoreState(ReadOnlySpan<byte> state)
    {
        // Reject an invalid payload before touching the current document.
        ObjectDisposedException.ThrowIf(mDisposed, this);
        var file = CubaseProjectState.Decode(state);
        var project = new Project(file.Project, registerWithStandaloneAudio: false);
        project.SetExportConfig(file.Export);
        mRestoring = true;
        try
        {
            Document.SetProject(project);
            EditorMetadata = file.Editor;
        }
        finally
        {
            mRestoring = false;
        }
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
