using Avalonia;
using Avalonia.Controls;
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

    public static void Open(Window? owner, uint hostAppVersion)
    {
        if (sInstance is { } opened)
        {
            opened.Activate();
            return;
        }

        var window = new BridgePanel(hostAppVersion);
        sInstance = window;
        window.Closed += (_, _) => { if (ReferenceEquals(sInstance, window)) sInstance = null; };
        if (owner != null)
            window.Show(owner);
        else
            window.Show();
    }

    public BridgePanel(uint hostAppVersion)
    {
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

        Closed += (_, _) =>
        {
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
                break;
            case BridgeClient.State.Error:
                mStatusText.Text = "Error".Tr(this) + ": " + (mClient.ErrorMessage ?? string.Empty);
                mConnectTextContent.Item = new TextItem() { Text = "Connect".Tr(this) };
                mSessionIdInput.IsEnabled = true;
                break;
        }
    }

    BridgeClient? mClient;
    readonly uint mHostAppVersion;
    TextInput mSessionIdInput;
    Avalonia.Controls.TextBlock mStatusText;
    Button mConnectButton;
    ButtonContent mConnectTextContent;

    static BridgePanel? sInstance;
}
