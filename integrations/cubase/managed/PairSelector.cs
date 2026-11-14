using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using Avalonia.Threading;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

// Pairing changes can replace this very session. Post button actions so native
// view disposal occurs after the current routed click has returned.
internal sealed class PairSelector : StackPanel, IDisposable
{
    readonly Func<Guid?> mGetEndpoint;
    readonly TextBlock mLabel = new() { VerticalAlignment = VerticalAlignment.Center };
    readonly TextBox mCode = new() { MinWidth = 290, MaxWidth = 350 };
    readonly Button mBind = new() { Content = "配对" };
    readonly Button mUnlink = new() { Content = "解除配对" };
    readonly TextBlock mStatus = new() { TextWrapping = Avalonia.Media.TextWrapping.Wrap };
    Guid? mEndpoint;
    CubasePairState? mState;
    bool mDisposed;

    internal PairSelector(Func<Guid?> getEndpoint)
    {
        mGetEndpoint = getEndpoint;
        Margin = new Thickness(8, 0, 8, 4);
        Spacing = 4;
        var controls = new WrapPanel { Orientation = Orientation.Horizontal };
        controls.Children.Add(mLabel);
        controls.Children.Add(mCode);
        controls.Children.Add(mBind);
        controls.Children.Add(mUnlink);
        Children.Add(controls);
        Children.Add(mStatus);
        mBind.Click += (_, _) =>
        {
            if (mEndpoint is not Guid endpoint)
                return;
            if (!Guid.TryParse(mCode.Text, out var bridge))
            {
                mStatus.Text = "请粘贴 ARA Bridge 中显示的完整配对码。";
                return;
            }
            Post(endpoint, () => CubasePairRegistry.Pair(endpoint, bridge));
        };
        mUnlink.Click += (_, _) =>
        {
            if (mEndpoint is Guid endpoint)
                Post(endpoint, () => CubasePairRegistry.Unpair(endpoint));
        };
        Refresh();
    }

    internal bool AllowsEditing { get; private set; } = true;
    internal bool FocusCode() => mCode.Focus();

    internal void Refresh()
    {
        if (mDisposed)
            return;
        var endpoint = mGetEndpoint();
        IsVisible = endpoint != null;
        if (endpoint == null)
        {
            AllowsEditing = true;
            return;
        }
        try
        {
            var state = CubasePairRegistry.GetState(endpoint.Value);
            if (mEndpoint == endpoint && mState == state)
                return;
            bool bridge = state.Role == CubasePairRole.Bridge;
            AllowsEditing = bridge || state.Status != CubasePairStatus.PeerMissing;
            if (mEndpoint != endpoint)
                mCode.Text = bridge ? endpoint.Value.ToString("D") : string.Empty;
            mEndpoint = endpoint;
            mState = state;
            mLabel.Text = bridge ? "ARA Bridge 配对码  " : "绑定 ARA Bridge  ";
            mCode.IsReadOnly = bridge;
            mCode.Watermark = "粘贴 ARA Bridge 配对码";
            mBind.IsVisible = !bridge;
            mBind.IsEnabled = state.Status != CubasePairStatus.Paired;
            mUnlink.IsEnabled = state.Status != CubasePairStatus.Independent;
            mStatus.Text = state.Status switch
            {
                CubasePairStatus.Paired => "已配对：共享 Bridge 编辑文档，由 Bridge 正式输出。",
                CubasePairStatus.PeerMissing => "配对实例已关闭；请解除配对或明确绑定新的实例。",
                _ => bridge ? "在 Instrument 中粘贴此码以共享当前文档。" : "独立模式：由 Instrument 输出。"
            };
        }
        catch (KeyNotFoundException)
        {
            AllowsEditing = false;
            mBind.IsEnabled = false;
            mUnlink.IsEnabled = false;
            mStatus.Text = "配对实例已关闭。";
        }
    }

    void Post(Guid endpoint, Action action)
    {
        Dispatcher.UIThread.Post(() =>
        {
            if (mDisposed || mGetEndpoint() != endpoint)
                return;
            try
            {
                action();
                mState = null;
                Refresh();
            }
            catch (KeyNotFoundException)
            {
                mStatus.Text = "配对码对应的实例已关闭，请重新复制。";
            }
            catch (ArgumentException)
            {
                mStatus.Text = "需要一个 Instrument 与一个 ARA Bridge 的配对码。";
            }
            catch (InvalidOperationException)
            {
                mStatus.Text = "实例已有配对，请先解除原配对。";
            }
        });
    }

    public void Dispose() => mDisposed = true;
}
