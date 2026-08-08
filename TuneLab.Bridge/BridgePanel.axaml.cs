using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using System;
using System.Runtime.Versioning;
using TuneLab.GUI;
using TuneLab.GUI.Components;
using TuneLab.I18N;
using TuneLab.Utils;
using Button = TuneLab.GUI.Components.Button;

namespace TuneLab.Bridge;

// 桥接面板：手动连接/断开 Bridge_VST3 插件会话，显示连接状态与错误。
// 单实例入口（与 SettingsWindow 同范式）：已开则置前。
[SupportedOSPlatform("windows")]
internal partial class BridgePanel : Window
{
    public const string DefaultSessionId = "default";

    public static void Open(Window? owner, uint hostAppVersion, IBridgeAudioProvider provider)
    {
        if (sInstance is { } opened)
        {
            // 窗口可能处于"关闭即隐藏"状态（桥接仍在运行）：重新显示并置前。
            opened.Show();
            opened.Activate();
            return;
        }

        var window = new BridgePanel(hostAppVersion, provider);
        sInstance = window;
        window.Closed += (_, _) => { if (ReferenceEquals(sInstance, window)) sInstance = null; };
        if (owner != null)
            window.Show(owner);
        else
            window.Show();
    }

    public BridgePanel(uint hostAppVersion, IBridgeAudioProvider provider)
    {
        mProvider = provider;

        InitializeComponent();
        Focusable = true;
        CanResize = false;
        WindowState = WindowState.Normal;
        WindowStartupLocation = WindowStartupLocation.CenterOwner;
        Topmost = true;
        mHostAppVersion = hostAppVersion;

        TitleLabel.Content = "Bridge".Tr(this);
        Title = "Bridge - TuneLab";

        this.Background = Style.BACK.ToBrush();
        TitleLabel.Foreground = Style.TEXT_LIGHT.ToBrush();

        var closeButton = new Button() { Width = 48, Height = 40 }
            .AddContent(new() { Item = new BorderItem() { CornerRadius = 0 }, ColorSet = new() { HoveredColor = Colors.White.Opacity(0.2), PressedColor = Colors.White.Opacity(0.2) } })
            .AddContent(new() { Item = new IconItem() { Icon = Assets.WindowClose }, ColorSet = new() { Color = Style.TEXT_LIGHT.Opacity(0.7) } });
        closeButton.Clicked += () => Close();
        WindowControl.Children.Add(closeButton);

        var titleBar = this.FindControl<Grid>("TitleBar") ?? throw new InvalidOperationException("TitleBar not found");
        bool useSystemTitle = System.Runtime.InteropServices.RuntimeInformation.IsOSPlatform(System.Runtime.InteropServices.OSPlatform.Linux);
        if (useSystemTitle)
        {
            titleBar.Height = 0;
            Height -= 40;
        }

        Content.Background = Style.INTERFACE.ToBrush();

        var root = new StackPanel { Margin = new Thickness(24, 16, 24, 16), Spacing = 12, Width = 340 };

        root.Children.Add(new Avalonia.Controls.TextBlock { Text = "Session Id".Tr(this), Foreground = Style.TEXT_NORMAL.ToBrush(), FontSize = 12 });

        mSessionIdInput = new TextInput { Width = 200, Height = 32, Background = Style.BACK.ToBrush(), Foreground = Style.WHITE.ToBrush() };
        mSessionIdInput.Display(DefaultSessionId);
        root.Children.Add(mSessionIdInput);

        mStatusText = new Avalonia.Controls.TextBlock { Foreground = Style.TEXT_NORMAL.ToBrush(), FontSize = 12, TextWrapping = TextWrapping.Wrap };
        root.Children.Add(mStatusText);

        mConnectButton = new Button { Width = 96, Height = 28 };
        mConnectButton.AddContent(new() { Item = new BorderItem() { CornerRadius = 6 }, ColorSet = new() { Color = Style.BUTTON_PRIMARY, HoveredColor = Style.BUTTON_PRIMARY_HOVER } });
        mConnectTextContent = new ButtonContent { Item = new TextItem() { Text = "Connect".Tr(this) }, ColorSet = new() { Color = Colors.White } };
        mConnectButton.AddContent(mConnectTextContent);
        mConnectButton.Clicked += OnConnectClicked;
        root.Children.Add(mConnectButton);

        ((ContentControl)Content).Content = root;

        mClient = new BridgeClient(DefaultSessionId) { HostAppVersion = mHostAppVersion };
        mClient.StateChanged += OnClientStateChanged;

        // 关闭窗口 ≠ 断开桥接：隐藏窗口，渲染线程与会话保持，音频继续推给 DAW。
        // 应用真正退出（ShutdownRequested）时放行关闭，走下方 Closed 清理。
        if (Application.Current?.ApplicationLifetime is IClassicDesktopStyleApplicationLifetime desktop)
            desktop.ShutdownRequested += (_, _) => mAllowClose = true;
        Closing += (_, e) =>
        {
            if (!mAllowClose)
            {
                e.Cancel = true;
                Hide();
            }
        };
        Closed += (_, _) =>
        {
            StopRenderer();
            if (mClient != null)
            {
                mClient.StateChanged -= OnClientStateChanged;
                mClient.Dispose();
                mClient = null;
            }
        };

        RefreshUi();
    }

    void OnConnectClicked()
    {
        if (mClient == null)
            return;

        switch (mClient.CurrentState)
        {
            case BridgeClient.State.Connected:
            case BridgeClient.State.WaitingForPlugin:
                mClient.Disconnect();
                break;
            default:
                EnsureClient();
                mClient.Connect();
                break;
        }
    }

    // 会话 id 变更时（仅未连接状态）重建客户端。
    void EnsureClient()
    {
        var sessionId = string.IsNullOrWhiteSpace(mSessionIdInput.Text) ? DefaultSessionId : mSessionIdInput.Text.Trim();
        if (mClient != null && mClient.SessionId == sessionId)
            return;

        if (mClient != null)
        {
            mClient.StateChanged -= OnClientStateChanged;
            mClient.Dispose();
        }
        mClient = new BridgeClient(sessionId) { HostAppVersion = mHostAppVersion };
        mClient.StateChanged += OnClientStateChanged;
    }

    void OnClientStateChanged()
    {
        Dispatcher.UIThread.Post(RefreshUi);
    }

    void RefreshUi()
    {
        if (mClient == null)
            return;

        switch (mClient.CurrentState)
        {
            case BridgeClient.State.Disconnected:
                mStatusText.Text = "Disconnected".Tr(this);
                mConnectTextContent.Item = new TextItem() { Text = "Connect".Tr(this) };
                mSessionIdInput.IsEnabled = true;
                StopRenderer();
                break;
            case BridgeClient.State.WaitingForPlugin:
                mStatusText.Text = "Waiting for plugin...".Tr(this);
                mConnectTextContent.Item = new TextItem() { Text = "Cancel".Tr(this) };
                mSessionIdInput.IsEnabled = false;
                break;
            case BridgeClient.State.Connected:
                mStatusText.Text = ("Connected".Tr(this) + "  " + mClient.SessionId);
                mConnectTextContent.Item = new TextItem() { Text = "Disconnect".Tr(this) };
                mSessionIdInput.IsEnabled = false;
                StartRenderer();
                break;
            case BridgeClient.State.Error:
                mStatusText.Text = "Error".Tr(this) + ": " + (mClient.ErrorMessage ?? string.Empty);
                mConnectTextContent.Item = new TextItem() { Text = "Connect".Tr(this) };
                mSessionIdInput.IsEnabled = true;
                StopRenderer();
                break;
        }
    }

    // M1：连接后启动渲染线程（把 TuneLab 音轨推入共享环），断开/出错时停止。
    void StartRenderer()
    {
        if (mRenderer != null)
            return;
        // 先激活桥接（SDL 静音 + 采样率变更跳过设备重开），再启动渲染线程，
        // 确保渲染线程首个迭代的采样率请求在 BridgeMode 已置位时处理。
        mProvider.SetBridgeActive(true);
        mRenderer = new BridgeRenderer(mClient!, mProvider);
        mRenderer.Start();
    }

    void StopRenderer()
    {
        if (mRenderer == null)
            return;
        mRenderer.Stop();
        mRenderer = null;
        mProvider.SetBridgeActive(false);
        // 退出桥接（断开/出错/关闭）：DAW 不再为 master——还原时基覆盖并暂停，
        // 让 TuneLab 回到本地播放/本地曲速表（避免覆盖残留导致曲速持续锁定 DAW 值）。
        mProvider.SetTransportTempo(null);
        mProvider.SetTransportPlaying(false);
    }

    BridgeClient? mClient;
    BridgeRenderer? mRenderer;
    readonly uint mHostAppVersion;
    readonly IBridgeAudioProvider mProvider;
    TextInput mSessionIdInput;
    Avalonia.Controls.TextBlock mStatusText;
    Button mConnectButton;
    ButtonContent mConnectTextContent;
    bool mAllowClose;

    static BridgePanel? sInstance;
}
