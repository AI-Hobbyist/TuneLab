using Avalonia.Controls;
using Avalonia.Controls.Documents;
using Avalonia.Threading;
using Avalonia.Media;
using System;
using TuneLab.Audio;
using TuneLab.Foundation;
using TuneLab.GUI;
using TuneLab.GUI.Components;
using TuneLab.I18N;
using TuneLab.Utils;

namespace TuneLab.UI;

// 钢琴窗分离后留在主窗底部的最小走带：不承载量化与音符工具，保证多轨可占满其余高度。
internal sealed class TransportBar : LayerPanel
{
    public TransportBar(FunctionBar.IDependency dependency, Action gotoStart, Action gotoEnd)
    {
        Height = 60;
        // 原功能栏的 Mover 表面使用 INTERFACE；分离态底部走带沿用相同控制条底色。
        Background = Style.INTERFACE.ToBrush();

        var controls = new StackPanel
        {
            Orientation = Avalonia.Layout.Orientation.Horizontal,
            Spacing = 12,
            HorizontalAlignment = Avalonia.Layout.HorizontalAlignment.Center,
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
        };
        var hoverBack = Colors.White.Opacity(0.05);

        var playButtonIcon = new IconItem() { Icon = Assets.Play };
        var playButton = new Toggle() { Width = 36, Height = 36 }
            .AddContent(new() { Item = new BorderItem() { CornerRadius = 4 }, CheckedColorSet = new() { HoveredColor = hoverBack, PressedColor = hoverBack }, UncheckedColorSet = new() { HoveredColor = hoverBack, PressedColor = hoverBack } })
            .AddContent(new() { Item = playButtonIcon, CheckedColorSet = new() { Color = Colors.White }, UncheckedColorSet = new() { Color = Style.LIGHT_WHITE.Opacity(0.5) } });
        playButton.SetupToolTip("Play".Tr(this));
        playButton.Switched.Subscribe(() =>
        {
            if (playButton.IsChecked)
                AudioEngine.Play();
            else
                AudioEngine.Pause();
        });
        void UpdatePlayButton()
        {
            playButtonIcon.Icon = AudioEngine.IsPlaying ? Assets.Pause : Assets.Play;
            playButton.Display(AudioEngine.IsPlaying);
            playButton.SetupToolTip(AudioEngine.IsPlaying ? "Pause".Tr(this) : "Play".Tr(this));
        }
        AudioEngine.PlayStateChanged += () => Dispatcher.UIThread.Post(UpdatePlayButton);
        UpdatePlayButton();
        controls.Children.Add(playButton);

        var autoPageButton = new AutoPageButton(dependency.PlayScrollTarget) { Width = 36, Height = 36 };
        autoPageButton.SetupToolTip("Auto Scroll".Tr(this));
        controls.Children.Add(autoPageButton);
        Button(Assets.GotoStart, "Go to Start".Tr(this), gotoStart);
        Button(Assets.GotoEnd, "Go to End".Tr(this), gotoEnd);

        // 分离态主窗仍显示与钢琴窗工具栏同一播放头的绝对时间。
        var timecodeMainRun = new Run() { FontSize = 16 };
        var timecodeMsRun = new Run() { FontSize = 12, Foreground = Style.WHITE.Opacity(0.4).ToBrush() };
        var timecodeLabel = new TextBlock
        {
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
            FontFamily = Assets.NotoMono,
            Foreground = Style.TEXT_NORMAL.ToBrush(),
            Inlines = new InlineCollection { timecodeMainRun, timecodeMsRun },
        };
        void UpdateTimecode()
        {
            var project = dependency.ProjectHolder.Value;
            double seconds = project == null ? 0 : project.TempoManager.GetTime(dependency.Playhead.Pos);
            var time = TimeSpan.FromSeconds(Math.Max(0, seconds));
            timecodeMainRun.Text = time.ToString(@"hh\:mm\:ss");
            timecodeMsRun.Text = time.ToString(@"\.fff");
        }
        dependency.Playhead.PosChanged.Subscribe(UpdateTimecode, s);
        dependency.ProjectHolder.Modified.Subscribe(UpdateTimecode, s);
        dependency.ProjectHolder.When(project => project.TempoManager.Modified).Subscribe(UpdateTimecode, s);
        UpdateTimecode();
        controls.Children.Add(new Border
        {
            Child = timecodeLabel,
            Background = Style.DARK.ToBrush(),
            BorderBrush = Style.LINE.ToBrush(),
            BorderThickness = new(1),
            CornerRadius = new(4),
            Padding = new(12, 7),
            VerticalAlignment = Avalonia.Layout.VerticalAlignment.Center,
        });
        Children.Add(controls);

        void Button(SvgIcon icon, string toolTip, Action action)
        {
            var button = new GUI.Components.Button() { Width = 36, Height = 36 }
                .AddContent(new() { Item = new BorderItem() { CornerRadius = 4 }, ColorSet = new() { HoveredColor = hoverBack, PressedColor = hoverBack } })
                // 与原功能栏保持同一色表：起止按钮的图标始终使用浅色，只有底板在悬浮/按下时高亮。
                .AddContent(new() { Item = new IconItem() { Icon = icon }, ColorSet = new() { Color = Style.LIGHT_WHITE.Opacity(0.5) } });
            button.SetupToolTip(toolTip);
            button.Clicked += action;
            controls.Children.Add(button);
        }
    }

    ~TransportBar()
    {
        s.DisposeAll();
    }

    readonly DisposableManager s = new();
}
