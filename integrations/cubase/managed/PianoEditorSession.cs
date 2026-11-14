using System;
using Avalonia.Controls.Embedding;
using Avalonia.Controls;
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
    readonly VoiceSelector mVoiceSelector;
    readonly PairSelector mPairSelector;
    readonly DispatcherTimer mTransportTimer;
    readonly Action mPollTransport;
    readonly Func<CubaseRenderStatus?> mGetRenderStatus;
    readonly TextBlock mRenderStatus = new()
    {
        Margin = new Avalonia.Thickness(8, 0, 8, 4),
        TextWrapping = Avalonia.Media.TextWrapping.Wrap,
        Text = "等待宿主状态"
    };
    bool mDisposed;

    internal PianoEditorSession(ProjectDocument document, IPlayhead playhead, Action pollTransport,
        Func<CubaseRenderStatus?> getRenderStatus, Action<int> previewKey, Action stopPreview, Func<Guid?> pairEndpoint)
    {
        Dispatcher.UIThread.VerifyAccess();
        mView = new CubasePianoView(document, playhead, previewKey, stopPreview);
        mVoiceSelector = new VoiceSelector();
        mPairSelector = new PairSelector(pairEndpoint);
        mPollTransport = pollTransport;
        mGetRenderStatus = getRenderStatus;
        mTransportTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(33) };
        mTransportTimer.Tick += OnTransportTick;
        var container = new DockPanel();
        DockPanel.SetDock(mPairSelector, Dock.Top);
        container.Children.Add(mPairSelector);
        DockPanel.SetDock(mVoiceSelector, Dock.Top);
        container.Children.Add(mVoiceSelector);
        DockPanel.SetDock(mRenderStatus, Dock.Top);
        container.Children.Add(mRenderStatus);
        container.Children.Add(mView.Piano);
        RefreshPairing();
        Root = EmbeddedEditorRoot.Create(container);
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
        mTransportTimer.Start();
    }

    void OnTransportTick(object? sender, EventArgs args)
    {
        RefreshPairing();
        mPollTransport();
        mRenderStatus.Text = mGetRenderStatus()?.DisplayText() ?? "宿主状态未连接";
    }

    void RefreshPairing()
    {
        mPairSelector.Refresh();
        mView.Piano.IsVisible = mPairSelector.AllowsEditing;
        mVoiceSelector.IsVisible = mPairSelector.AllowsEditing;
        mRenderStatus.IsVisible = mPairSelector.AllowsEditing;
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
        return mView.Piano.IsVisible ? mView.Piano.PianoScrollView.Focus() : mPairSelector.FocusCode();
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
        mVoiceSelector.SetPart(part);
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
            mTransportTimer.Stop();
            mTransportTimer.Tick -= OnTransportTick;
            Root.StopRendering();
            Root.Content = null;
        }
        finally
        {
            try
            {
                mPairSelector.Dispose();
                mVoiceSelector.Dispose();
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
