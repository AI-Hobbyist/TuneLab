using System;
using Avalonia.Controls.Embedding;
using Avalonia.Controls;
using Avalonia.Threading;
using TuneLab.Data;
using TuneLab.Hosting;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Avalonia.Media;
using System.Threading.Tasks;
using TuneLab.SDK;
using TuneLab.UI;
using TuneLab.Foundation;

namespace TuneLab.Cubase.Managed;

// A native view owns this session. Its document remains owned by the plug-in
// instance, so removing a view never disposes the underlying editing history.
internal sealed class PianoEditorSession : IDisposable
{
    readonly CubasePianoView mView;
    readonly VoiceSelector mVoiceSelector;
    readonly PairSelector mPairSelector;
    readonly EditorToolbar mToolbar;
    readonly DispatcherTimer mTransportTimer;
    readonly Action mPollTransport;
    readonly Func<CubaseRenderStatus?> mGetRenderStatus;
    readonly LayoutTransformControl mScaledContent;
    readonly EmbeddedScriptInputs mInputs = new();
    readonly DockPanel mEditorContent;
    readonly TextBox mScriptError = new()
    {
        IsReadOnly = true,
        AcceptsReturn = true,
        TextWrapping = TextWrapping.Wrap,
        MaxHeight = 120,
        IsVisible = false,
        Margin = new Avalonia.Thickness(8, 0, 8, 4)
    };
    double? mContentScale;
    readonly TextBlock mRenderStatus = new()
    {
        Margin = new Avalonia.Thickness(8, 0, 8, 4),
        TextWrapping = Avalonia.Media.TextWrapping.Wrap,
        Text = "等待宿主状态"
    };
    bool mDisposed;
    bool mClosing;

    internal PianoEditorSession(ProjectDocument document, IPlayhead playhead, Action pollTransport,
        Func<CubaseRenderStatus?> getRenderStatus, Action<int> previewKey, Action stopPreview, Func<Guid?> pairEndpoint)
    {
        Dispatcher.UIThread.VerifyAccess();
        mView = new CubasePianoView(document, playhead, previewKey, stopPreview);
        mVoiceSelector = new VoiceSelector();
        mPairSelector = new PairSelector(pairEndpoint);
        mToolbar = new EditorToolbar(mView);
        mPollTransport = pollTransport;
        mGetRenderStatus = getRenderStatus;
        mTransportTimer = new DispatcherTimer { Interval = TimeSpan.FromMilliseconds(33) };
        mTransportTimer.Tick += OnTransportTick;
        var container = mEditorContent = new DockPanel();
        DockPanel.SetDock(mPairSelector, Dock.Top);
        container.Children.Add(mPairSelector);
        DockPanel.SetDock(mVoiceSelector, Dock.Top);
        container.Children.Add(mVoiceSelector);
        DockPanel.SetDock(mToolbar, Dock.Top);
        container.Children.Add(mToolbar);
        DockPanel.SetDock(mRenderStatus, Dock.Top);
        container.Children.Add(mRenderStatus);
        DockPanel.SetDock(mScriptError, Dock.Top);
        container.Children.Add(mScriptError);
        container.Children.Add(mView.Piano);
        RefreshPairing();
        var layers = new Grid();
        layers.Children.Add(container);
        layers.Children.Add(mInputs);
        mScaledContent = new LayoutTransformControl { Child = layers };
        Root = EmbeddedEditorRoot.Create(mScaledContent);
        Root.ScalingChanged += OnScalingChanged;
        mView.ScriptScope.PromptInputs = ShowInputsAsync;
        mView.ScriptScope.ReportError = (title, message) =>
        {
            mScriptError.Text = title + ": " + message;
            mScriptError.IsVisible = true;
        };
    }

    internal EmbeddableControlRoot Root { get; }

    async Task<PropertyObject?> ShowInputsAsync(ScriptToolMenu.InputRequest request)
    {
        if (mClosing)
            return null;
        mEditorContent.IsEnabled = false;
        mScriptError.IsVisible = false;
        try
        {
            return await mInputs.ShowAsync(request);
        }
        finally
        {
            if (!mClosing)
            {
                mEditorContent.IsEnabled = true;
                mView.Piano.PianoScrollView.Focus();
            }
        }
    }

    internal void SetScale(double factor)
    {
        Dispatcher.UIThread.VerifyAccess();
        ObjectDisposedException.ThrowIf(mDisposed, this);
        if (!double.IsFinite(factor) || factor <= 0)
            throw new ArgumentOutOfRangeException(nameof(factor));
        mContentScale = factor;
        UpdateContentScale();
    }

    void OnScalingChanged(object? sender, EventArgs args) => UpdateContentScale();

    void UpdateContentScale()
    {
        if (mContentScale is not { } factor)
            return;
        // VST3 supplies physical pixels per content unit. Avalonia already
        // applies the HWND's DPI, so compensate rather than multiplying it twice.
        double relativeScale = factor / Root.RenderScaling;
        mScaledContent.LayoutTransform = new ScaleTransform(relativeScale, relativeScale);
    }

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
        mToolbar.Refresh();
        mRenderStatus.Text = mGetRenderStatus()?.DisplayText() ?? "宿主状态未连接";
    }

    void RefreshPairing()
    {
        mPairSelector.Refresh();
        mView.Piano.IsVisible = mPairSelector.AllowsEditing;
        mVoiceSelector.IsVisible = mPairSelector.AllowsEditing;
        mToolbar.IsVisible = mPairSelector.AllowsEditing;
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
        if (mInputs.IsVisible)
            return mInputs.FocusForm();
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
        if (!ReferenceEquals(mView.Piano.Part, part))
            mInputs.Cancel();
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
        mClosing = true;
        mView.ScriptScope.Deactivate();
        try
        {
            mTransportTimer.Stop();
            mTransportTimer.Tick -= OnTransportTick;
            Root.ScalingChanged -= OnScalingChanged;
            Root.StopRendering();
            Root.Content = null;
        }
        finally
        {
            try
            {
                try
                {
                    mPairSelector.Dispose();
                }
                finally
                {
                    try
                    {
                        mVoiceSelector.Dispose();
                    }
                    finally
                    {
                        mView.Dispose();
                    }
                }
            }
            finally
            {
                try
                {
                    mInputs.Dispose();
                }
                finally
                {
                    Root.Dispose();
                }
            }
        }
        mDisposed = true;
    }
}
