using System;
using System.Linq;
using TuneLab.Data;
using TuneLab.Input;
using TuneLab.SDK;
using TuneLab.Foundation;

namespace TuneLab.Hosting;

// Each plug-in document owns its editing history and project. It does not
// install a global command provider or start the standalone audio device.
internal sealed class CubaseDocumentContext : IDisposable
{
    readonly ProjectDocument mDocument = new();
    bool mDisposed;

    public EditorActionCollection Actions { get; } = new();

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
        Actions.Clear();
        mDocument.Project?.Detach();
        mDocument.Project?.Dispose();
        mDocument.Clear();
    }
}
