using System;
using System.Linq;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using TuneLab.Data;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

internal sealed class VoiceSelector : StackPanel, IDisposable
{
    readonly ComboBox mEngines = new() { MinWidth = 160 };
    readonly ComboBox mSingers = new() { MinWidth = 180 };
    readonly TextBlock mStatus = new() { VerticalAlignment = VerticalAlignment.Center };
    IMidiPart? mPart;
    bool mRefreshing;

    internal VoiceSelector()
    {
        Orientation = Orientation.Horizontal;
        Spacing = 8;
        Margin = new Thickness(8);
        Children.Add(new TextBlock { Text = "引擎", VerticalAlignment = VerticalAlignment.Center });
        Children.Add(mEngines);
        Children.Add(new TextBlock { Text = "歌手", VerticalAlignment = VerticalAlignment.Center });
        Children.Add(mSingers);
        Children.Add(mStatus);
        mEngines.SelectionChanged += (_, _) =>
        {
            if (!mRefreshing && mEngines.SelectedItem is CubaseEngineChoice engine)
            {
                mRefreshing = true;
                try
                {
                    mSingers.SelectedItem = null;
                    mSingers.ItemsSource = CubaseVoiceSelection.Singers(engine.Type);
                    mStatus.Text = "请选择歌手";
                }
                finally
                {
                    mRefreshing = false;
                }
            }
        };
        mSingers.SelectionChanged += (_, _) =>
        {
            if (mRefreshing || mPart == null ||
                mEngines.SelectedItem is not CubaseEngineChoice engine ||
                mSingers.SelectedItem is not CubaseSingerChoice singer)
            {
                return;
            }
            try
            {
                CubaseVoiceSelection.Select(mPart, engine.Type, singer.Id);
                mStatus.Text = "";
            }
            catch (Exception exception)
            {
                mStatus.Text = exception.Message;
            }
        };
    }

    internal void SetPart(IMidiPart? part)
    {
        mPart?.SoundSource.Modified.Unsubscribe(Refresh);
        mPart = part;
        mPart?.SoundSource.Modified.Subscribe(Refresh);
        Refresh();
    }

    void Refresh()
    {
        mRefreshing = true;
        try
        {
            var engines = CubaseVoiceSelection.Engines();
            mEngines.ItemsSource = engines;
            var source = mPart?.SoundSource;
            mEngines.SelectedItem = engines.FirstOrDefault(engine => engine.Type == source?.Type);
            var singers = source == null ? Array.Empty<CubaseSingerChoice>() :
                CubaseVoiceSelection.Singers(source.Type);
            mSingers.ItemsSource = singers;
            mSingers.SelectedItem = singers.FirstOrDefault(singer => singer.Id == source?.ID);
            mStatus.Text = source == null ? "" : CubaseVoiceSelection.Unavailable(source.Type, source.ID) ?? "";
            if (string.IsNullOrEmpty(mStatus.Text))
            {
                mStatus.Text = string.Join("; ", PluginServices.InitializeEngines());
            }
            IsEnabled = mPart != null;
        }
        finally
        {
            mRefreshing = false;
        }
    }

    public void Dispose() => SetPart(null);
}
