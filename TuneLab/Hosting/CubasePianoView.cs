using System;
using Avalonia.Threading;
using Avalonia.Input;
using TuneLab.Data;
using TuneLab.Foundation;
using TuneLab.Input;
using TuneLab.UI;
using TuneLab.SDK;
using TuneLab.Scripting;

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
        Piano = new PianoWindow(this, mActions, persistViewState: false,
            objectSelection: new EditorSelectionState(isLocal: true));
        ScriptScope = new ScriptToolMenu.Scope(
            () => document.Project, () => Piano.Part, () => Piano.Quantization,
            () => null, () => Piano.PianoScrollView.CurrentRegionSelection is { } selection
                ? new ScriptPianoSelection(selection.StartTick, selection.EndTick) : null,
            new SelectionWriter(this)) { ObjectSelection = Piano.PianoScrollView.ObjectSelection };
        Piano.PianoScrollView.ScriptScope = ScriptScope;
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
        RegisterTool("tool.note", "Note Tool", Key.D1, UI.PianoTool.Note);
        RegisterTool("tool.pitch", "Pitch Pen", Key.D2, UI.PianoTool.Pitch);
        RegisterTool("tool.anchor", "Anchor Tool", Key.D3, UI.PianoTool.Anchor);
        RegisterTool("tool.lock", "Locking Brush", Key.D4, UI.PianoTool.Lock);
        RegisterTool("tool.vibrato", "Vibrato Tool", Key.D5, UI.PianoTool.Vibrato);
    }

    void RegisterTool(string id, string label, Key key, PianoTool tool)
    {
        Keymap.Register(new EditorAction
        {
            Id = id,
            DisplayName = () => label,
            Kind = ActionKind.AppState,
            Execute = () => PianoTool.Value = tool,
            Unavailable = () => !Piano.CanRunEditCommand ? "an editing operation is active" :
                tool == UI.PianoTool.Vibrato && Piano.Part?.SoundSource.Kind == SourceKind.Instrument
                    ? "instrument sound sources have no vibrato system" : null
        }, KeyScope.Editor, new TuneLab.GUI.Input.KeyBinding(key), mActions);
    }

    void Register(string id, string label, Action execute, Key key, Func<string?>? unavailable = null)
    {
        Keymap.Register(new EditorAction
        {
            Id = id,
            DisplayName = () => label,
            Kind = id is "edit.copy" or "edit.selectAll" ? ActionKind.AppState : ActionKind.ProjectEdit,
            Execute = execute,
            Unavailable = () => !Piano.CanRunEditCommand ? "an editing operation is active" : unavailable?.Invoke()
        }, KeyScope.Editor, new TuneLab.GUI.Input.KeyBinding(key,
            TuneLab.GUI.Input.KeyBinding.PrimaryModifier), mActions);
    }

    public PianoWindow Piano { get; }
    internal EditorActionCollection Actions => mActions;
    internal ScriptToolMenu.Scope ScriptScope { get; }

    sealed class SelectionWriter(CubasePianoView view) : IScriptSelectionWriter
    {
        public void SetTrackSelection(double startTick, double endTick, int startTrackNumber, int endTrackNumber)
            => throw new NotSupportedException("The embedded piano editor has no arrangement selection.");

        public void ClearTrackSelection()
            => throw new NotSupportedException("The embedded piano editor has no arrangement selection.");

        public void SetPianoSelection(double startTick, double endTick)
            => view.Piano.PianoScrollView.SetRegionSelection(startTick, endTick);

        public void ClearPianoSelection() => view.Piano.PianoScrollView.ClearRegionSelection();
    }
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
        ScriptScope.Deactivate();
        try
        {
            mStopPreview?.Invoke();
        }
        finally
        {
            Piano.SetKeyPreview(null, null);
            Piano.PianoScrollView.ScriptScope = null;
            try
            {
                Piano.Detach();
            }
            finally
            {
                mActions.Clear();
            }
        }
    }
}
