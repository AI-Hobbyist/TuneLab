using System;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Layout;
using Avalonia.Media;
using Avalonia.Threading;
using TuneLab.Data;
using TuneLab.Foundation;
using TuneLab.GUI.Controllers;
using TuneLab.SDK;
using TuneLab.UI;

namespace TuneLab.Hosting;

// A form inside the plug-in HWND: no synthetic owner Window or nested event loop.
internal sealed class EmbeddedScriptInputs : Border, IDisposable
{
    readonly PropertyObjectController mController = new();
    readonly TextBlock mTitle = new() { Foreground = Brushes.White };
    readonly TextBlock mError = new() { TextWrapping = TextWrapping.Wrap, Foreground = Brushes.OrangeRed };
    readonly Button mConfirm = new() { Content = "确定" };
    DataPropertyObject? mData;
    ScriptToolMenu.InputRequest? mRequest;
    TaskCompletionSource<PropertyObject?>? mCompletion;
    bool mReconcilePending;
    bool mDisposed;

    internal EmbeddedScriptInputs()
    {
        IsVisible = false;
        Background = new SolidColorBrush(Color.FromArgb(160, 0, 0, 0));
        var cancel = new Button { Content = "取消" };
        cancel.Click += (_, _) => Finish(false);
        mConfirm.Click += (_, _) => Finish(true);
        var reset = new Button { Content = "恢复默认值" };
        reset.Click += (_, _) =>
        {
            if (mData == null)
                return;
            foreach (var key in mData.GetInfo().Map.Select(pair => pair.Key).ToArray())
                mData.RemoveValue(key);
            mData.Commit();
        };
        var actions = new StackPanel { Orientation = Orientation.Horizontal, Spacing = 8 };
        actions.Children.Add(reset);
        actions.Children.Add(cancel);
        actions.Children.Add(mConfirm);
        var body = new DockPanel();
        DockPanel.SetDock(mTitle, Dock.Top);
        body.Children.Add(mTitle);
        DockPanel.SetDock(actions, Dock.Bottom);
        body.Children.Add(actions);
        DockPanel.SetDock(mError, Dock.Bottom);
        body.Children.Add(mError);
        body.Children.Add(new ScrollViewer { Content = mController });
        Child = new Border
        {
            Background = new SolidColorBrush(Color.FromRgb(35, 35, 35)),
            Padding = new Thickness(16),
            Margin = new Thickness(24),
            MaxWidth = 580,
            MaxHeight = 540,
            Child = body
        };
        KeyboardNavigation.SetTabNavigation(this, KeyboardNavigationMode.Cycle);
        KeyDown += (_, args) =>
        {
            if (args.Key == Key.Escape)
            {
                args.Handled = true;
                Finish(false);
            }
        };
    }

    internal Task<PropertyObject?> ShowAsync(ScriptToolMenu.InputRequest request)
    {
        Dispatcher.UIThread.VerifyAccess();
        ObjectDisposedException.ThrowIf(mDisposed, this);
        if (mCompletion != null)
            throw new InvalidOperationException("An input form is already open in this editor.");
        mCompletion = new TaskCompletionSource<PropertyObject?>(TaskCreationOptions.RunContinuationsAsynchronously);
        var task = mCompletion.Task;
        try
        {
            mRequest = request;
            mData = new DataPropertyObject(new DataDocument());
            foreach (var pair in request.InitialValues.Map)
                mData.SetValue(pair.Key, pair.Value);
            mData.Commit();
            mController.SetConfig(request.Schema, mData);
            mData.Modified.Subscribe(ScheduleReconcile);
            mController.ButtonActionCompleted += ScheduleReconcile;
            mTitle.Text = request.Title;
            mError.Text = "";
            mConfirm.IsEnabled = true;
            IsVisible = true;
            mConfirm.Focus();
            return task;
        }
        catch
        {
            Finish(false);
            throw;
        }
    }

    internal bool FocusForm() => mConfirm.Focus();
    internal void Cancel() => Finish(false);

    void ScheduleReconcile()
    {
        if (mReconcilePending || mData == null)
            return;
        mReconcilePending = true;
        var data = mData;
        Dispatcher.UIThread.Post(() =>
        {
            if (mDisposed || !ReferenceEquals(data, mData) || mRequest == null)
                return;
            mReconcilePending = false;
            var (schema, error) = mRequest.Compute(data.GetInfo());
            mError.Text = error ?? "";
            mConfirm.IsEnabled = error == null;
            if (schema != null)
                mController.Reconcile(schema);
        });
    }

    void Finish(bool accepted)
    {
        var completion = mCompletion;
        if (completion == null)
            return;
        PropertyObject? result = null;
        mCompletion = null;
        try
        {
            if (accepted)
                result = mData!.GetInfo();
            mData?.Modified.Unsubscribe(ScheduleReconcile);
            mController.ButtonActionCompleted -= ScheduleReconcile;
            mData = null;
            mRequest = null;
            mReconcilePending = false;
            IsVisible = false;
            mController.ResetConfig();
        }
        finally
        {
            completion.TrySetResult(result);
        }
    }

    public void Dispose()
    {
        Dispatcher.UIThread.VerifyAccess();
        if (mDisposed)
            return;
        mDisposed = true;
        Finish(false);
    }
}
