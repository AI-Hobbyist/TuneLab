using Avalonia.Controls;
using Avalonia.Controls.Embedding;

namespace TuneLab.Cubase.Managed;

// Compile-time embedding seam. Callers must initialize Avalonia and invoke this
// on its UI thread; real HWND attachment is validated separately in M1/M4.
internal static class EmbeddedEditorRoot
{
    internal static EmbeddableControlRoot Create(Control editor)
    {
        return new EmbeddableControlRoot
        {
            Content = editor
        };
    }
}
