using System.Runtime.CompilerServices;
using TuneLab.Foundation;

namespace TuneLab.Data;

// Views can own selection independently while editing the same model objects.
// Weak keys let deleted objects leave the view without extending their lifetime.
internal sealed class EditorSelectionState(bool isLocal)
{
    sealed class Selected
    {
    }

    readonly ConditionalWeakTable<ISelectable, Selected> mSelected = new();
    readonly MergableEvent mChanged = new();

    internal bool IsLocal { get; } = isLocal;
    internal IMergableEvent Changed => mChanged;

    internal bool IsSelected(ISelectable item)
        => IsLocal ? mSelected.TryGetValue(item, out _) : item.IsSelected;

    internal void SetSelected(ISelectable item, bool selected)
    {
        if (IsSelected(item) == selected)
            return;
        if (!IsLocal)
            item.IsSelected = selected;
        else if (selected)
            mSelected.Add(item, new Selected());
        else
            mSelected.Remove(item);
        mChanged.Invoke();
    }

    internal void Clear()
    {
        if (!IsLocal)
            return;
        mSelected.Clear();
        mChanged.Invoke();
    }
}
