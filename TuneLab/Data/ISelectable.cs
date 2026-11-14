using Avalonia.Controls.Documents;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using TuneLab.Foundation;

namespace TuneLab.Data;

internal interface ISelectable
{
    IActionEvent SelectionChanged { get; }
    bool IsSelected { get; set; }
}

internal interface ISelectableCollection<T> : IEnumerable<T> where T : ISelectable
{
    IMergableEvent SelectionChanged { get; }
}

internal static class ISelectableExtension
{
    public static void Select(this ISelectable selectable, EditorSelectionState? selection = null)
    {
        if (selection == null)
            selectable.IsSelected = true;
        else
            selection.SetSelected(selectable, true);
    }

    public static void Deselect(this ISelectable selectable, EditorSelectionState? selection = null)
    {
        if (selection == null)
            selectable.IsSelected = false;
        else
            selection.SetSelected(selectable, false);
    }

    public static void Inselect(this ISelectable selectable, EditorSelectionState? selection = null)
    {
        if (selection == null)
            selectable.IsSelected = !selectable.IsSelected;
        else
            selection.SetSelected(selectable, !selection.IsSelected(selectable));
    }

    public static IReadOnlyCollection<T> AllSelectedItems<T>(this IEnumerable<T> selectableCollection,
        EditorSelectionState? selection = null) where T : ISelectable
    {
        List<T> results = new();
        foreach (var selectable in selectableCollection)
        {
            if (selection?.IsSelected(selectable) ?? selectable.IsSelected)
                results.Add(selectable);
        }
        return results;
    }

    public static void SelectAllItems<T>(this IEnumerable<T> selectableCollection,
        EditorSelectionState? selection = null) where T : ISelectable
    {
        bool canMerge = selectableCollection is ISelectableCollection<T>;
        if (canMerge) ((ISelectableCollection<T>)selectableCollection).SelectionChanged.BeginMerge();
        selection?.Changed.BeginMerge();
        foreach (var item in selectableCollection)
        {
            item.Select(selection);
        }
        if (canMerge) ((ISelectableCollection<T>)selectableCollection).SelectionChanged.EndMerge();
        selection?.Changed.EndMerge();
    }

    public static void DeselectAllItems<T>(this IEnumerable<T> selectableCollection,
        EditorSelectionState? selection = null) where T : ISelectable
    {
        bool canMerge = selectableCollection is ISelectableCollection<T>;
        if (canMerge) ((ISelectableCollection<T>)selectableCollection).SelectionChanged.BeginMerge();
        selection?.Changed.BeginMerge();
        foreach (var item in selectableCollection)
        {
            item.Deselect(selection);
        }
        if (canMerge) ((ISelectableCollection<T>)selectableCollection).SelectionChanged.EndMerge();
        selection?.Changed.EndMerge();
    }

    public static bool HasSelectedItem<T>(this IEnumerable<T> selectableCollection,
        EditorSelectionState? selection = null) where T : ISelectable
    {
        foreach (var item in selectableCollection)
        {
            if (selection?.IsSelected(item) ?? item.IsSelected)
                return true;
        }

        return false;
    }
}
