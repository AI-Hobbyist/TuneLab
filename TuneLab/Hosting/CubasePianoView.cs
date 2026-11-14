using System;
using Avalonia.Threading;
using Avalonia.Input;
using TuneLab.Data;
using TuneLab.Foundation;
using TuneLab.Input;
using TuneLab.UI;

namespace TuneLab.Hosting;

// View lifetime is independent of the document; callers may close and reopen it.
internal sealed class CubasePianoView : PianoWindow.IDependency, IDisposable
{
    bool mDisposed;
    readonly EditorActionCollection mActions = new();
    readonly Action? mStopPreview;

    public CubasePianoView(ProjectDocument document, IPlayhead playhead,
        Action<int>? previewKey = null, Action? stopPreview = null)
    {
        Dispatcher.UIThread.VerifyAccess();
        Playhead = playhead;
        Piano = new PianoWindow(this, mActions, persistViewState: false);
        mStopPreview = stopPreview;
        Piano.SetKeyPreview(previewKey, stopPreview);
        Register("edit.undo", "Undo", () => { document.Undo(); }, Key.Z,
            () => document.Undoable() ? null : "there is nothing to undo");
        Register("edit.redo", "Redo", () => { document.Redo(); }, Key.Y,
            () => document.Redoable() ? null : "there is nothing to redo");
        Register("edit.copy", "Copy", () => Piano.CopySelection(), Key.C);
        Register("edit.cut", "Cut", () => Piano.CutSelection(), Key.X);
        Register("edit.paste", "Paste", () => Piano.PasteSelection(), Key.V);
        Register("edit.selectAll", "Select All", () => Piano.SelectAllInPiano(), Key.A);
        Keymap.Register(new EditorAction
        {
            Id = "edit.delete",
            DisplayName = () => "Delete",
            Kind = ActionKind.ProjectEdit,
            Execute = () => Piano.DeleteSelection()
        }, KeyScope.Editor, new TuneLab.GUI.Input.KeyBinding(Key.Delete), mActions);
    }

    void Register(string id, string label, Action execute, Key key, Func<string?>? unavailable = null)
    {
        Keymap.Register(new EditorAction
        {
            Id = id,
            DisplayName = () => label,
            Kind = id is "edit.copy" or "edit.selectAll" ? ActionKind.AppState : ActionKind.ProjectEdit,
            Execute = execute,
            Unavailable = unavailable
        }, KeyScope.Editor, new TuneLab.GUI.Input.KeyBinding(key,
            TuneLab.GUI.Input.KeyBinding.PrimaryModifier), mActions);
    }

    public PianoWindow Piano { get; }
    public IPlayhead Playhead { get; }
    public INotifiableProperty<PianoTool> PianoTool { get; } =
        new NotifiableProperty<PianoTool>(UI.PianoTool.Note);
    public INotifiableProperty<PlayScrollTarget> PlayScrollTarget { get; } =
        new NotifiableProperty<PlayScrollTarget>(UI.PlayScrollTarget.None);

    public void Dispose()
    {
        Dispatcher.UIThread.VerifyAccess();
        if (mDisposed)
        {
            return;
        }
        mDisposed = true;
        mStopPreview?.Invoke();
        Piano.SetKeyPreview(null, null);
        Piano.Detach();
        mActions.Clear();
    }
}
