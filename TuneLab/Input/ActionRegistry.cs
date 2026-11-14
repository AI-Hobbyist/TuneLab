using System.Collections.Generic;

namespace TuneLab.Input;

// Standalone editors retain the process-wide default. Embedded editors own a
// separate collection so identical action IDs cannot overwrite other instances.
internal static class ActionRegistry
{
    internal static EditorActionCollection Default { get; } = new();

    public static void Register(EditorAction action) => Default.Register(action);
    public static void Unregister(string id) => Default.Unregister(id);
    public static IReadOnlyCollection<EditorAction> Actions => Default.Actions;
    public static bool TryGet(string id, out EditorAction action) => Default.TryGet(id, out action);
    public static int OrderOf(string id) => Default.OrderOf(id);
    public static string LabelOf(string id) => Default.LabelOf(id);
    public static string? Execute(string id, string argument = "") => Default.Execute(id, argument);
}
