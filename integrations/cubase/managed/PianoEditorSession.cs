using System;
using Avalonia.Controls.Embedding;
using Avalonia.Threading;
using TuneLab.Data;
using TuneLab.Hosting;
using System.ComponentModel;
using System.Runtime.InteropServices;

namespace TuneLab.Cubase.Managed;

// A native view owns this session. Its document remains owned by the plug-in
// instance, so removing a view never disposes the underlying editing history.
internal sealed class PianoEditorSession : IDisposable
{
    readonly CubasePianoView mView;
    bool mDisposed;

    internal PianoEditorSession(ProjectDocument document, IPlayhead playhead)
    {
        Dispatcher.UIThread.VerifyAccess();
        mView = new CubasePianoView(document, playhead);
        Root = EmbeddedEditorRoot.Create(mView.Piano);
    }

    internal EmbeddableControlRoot Root { get; }

    internal void Attach(nint parent, int width, int height)
    {
        Dispatcher.UIThread.VerifyAccess();
        ObjectDisposedException.ThrowIf(mDisposed, this);
        if (parent == 0 || width <= 0 || height <= 0)
        {
            throw new ArgumentException("A valid parent HWND and positive client dimensions are required.");
        }
        var hwnd = Root.TryGetPlatformHandle()?.Handle
            ?? throw new InvalidOperationException("The embedded root has no native window handle.");
        Marshal.SetLastPInvokeError(0);
        if (SetParent(hwnd, parent) == 0 && Marshal.GetLastPInvokeError() != 0)
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
        if (!SetWindowPos(hwnd, 0, 0, 0, width, height, 0x0040 | 0x0004))
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
        Root.Prepare();
        Root.StartRendering();
    }

    internal void Resize(int width, int height)
    {
        Dispatcher.UIThread.VerifyAccess();
        ObjectDisposedException.ThrowIf(mDisposed, this);
        var hwnd = Root.TryGetPlatformHandle()?.Handle
            ?? throw new InvalidOperationException("The embedded root has no native window handle.");
        if (!SetWindowPos(hwnd, 0, 0, 0, width, height, 0x0010 | 0x0004))
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
    }

    internal bool Focus()
    {
        Dispatcher.UIThread.VerifyAccess();
        ObjectDisposedException.ThrowIf(mDisposed, this);
        var hwnd = Root.TryGetPlatformHandle()?.Handle
            ?? throw new InvalidOperationException("The embedded root has no native window handle.");
        Marshal.SetLastPInvokeError(0);
        if (SetFocus(hwnd) == 0 && Marshal.GetLastPInvokeError() != 0)
        {
            throw new Win32Exception(Marshal.GetLastPInvokeError());
        }
        return mView.Piano.PianoScrollView.Focus();
    }

    [DllImport("user32.dll", SetLastError = true)]
    static extern nint SetFocus(nint hwnd);

    [DllImport("user32.dll", SetLastError = true)]
    static extern nint SetParent(nint child, nint parent);

    [DllImport("user32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    static extern bool SetWindowPos(nint hwnd, nint insertAfter, int x, int y,
        int width, int height, uint flags);

    internal void SetPart(IMidiPart? part)
    {
        Dispatcher.UIThread.VerifyAccess();
        ObjectDisposedException.ThrowIf(mDisposed, this);
        mView.Piano.Part = part;
    }

    public void Dispose()
    {
        Dispatcher.UIThread.VerifyAccess();
        if (mDisposed)
        {
            return;
        }
        try
        {
            Root.StopRendering();
            Root.Content = null;
        }
        finally
        {
            try
            {
                mView.Dispose();
            }
            finally
            {
                Root.Dispose();
            }
        }
        mDisposed = true;
    }
}
