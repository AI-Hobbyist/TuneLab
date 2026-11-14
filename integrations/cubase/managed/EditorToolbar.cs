using System;
using System.Collections.Generic;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Layout;
using TuneLab.Hosting;
using TuneLab.SDK;
using TuneLab.UI;
using TuneLab.Foundation;

namespace TuneLab.Cubase.Managed;

// Both containers operate on the session's local view and action collection.
internal sealed class EditorToolbar : WrapPanel
{
    readonly CubasePianoView mView;
    readonly List<(Button Button, PianoTool Tool)> mTools = new();
    readonly List<(Button Button, string Id)> mActions = new();
    readonly ComboBox mDivision = new() { MinWidth = 90 };
    readonly ComboBox mTuplet = new() { MinWidth = 90 };

    internal EditorToolbar(CubasePianoView view)
    {
        mView = view;
        Orientation = Orientation.Horizontal;
        Margin = new Thickness(8, 0, 8, 4);
        AddAction("撤销", "edit.undo");
        AddAction("重做", "edit.redo");
        AddAction("剪切", "edit.cut");
        AddAction("复制", "edit.copy");
        AddAction("粘贴", "edit.paste");
        AddAction("删除", "edit.delete");
        AddAction("全选", "edit.selectAll");
        AddTool("音符", PianoTool.Note);
        AddTool("音高", PianoTool.Pitch);
        AddTool("锚点", PianoTool.Anchor);
        AddTool("锁定", PianoTool.Lock);
        AddTool("颤音", PianoTool.Vibrato);
        AddQuantization();
        Refresh();
    }

    void AddQuantization()
    {
        var divisions = Enum.GetValues<MusicTheory.QuantizationDivision>();
        mDivision.ItemsSource = Array.ConvertAll(divisions, value => $"1/{4 * (int)value}");
        mDivision.SelectedIndex = Array.IndexOf(divisions, mView.Piano.Quantization.Division);
        mTuplet.ItemsSource = new[] { "普通", "三连音", "五连音" };
        var bases = Enum.GetValues<MusicTheory.QuantizationBase>();
        mTuplet.SelectedIndex = Array.IndexOf(bases, mView.Piano.Quantization.Base);
        mDivision.SelectionChanged += (_, _) =>
        {
            if (mDivision.SelectedIndex >= 0 && mView.Piano.CanRunEditCommand)
                mView.Piano.Quantization.Division = divisions[mDivision.SelectedIndex];
        };
        mTuplet.SelectionChanged += (_, _) =>
        {
            if (mTuplet.SelectedIndex >= 0 && mView.Piano.CanRunEditCommand)
                mView.Piano.Quantization.Base = bases[mTuplet.SelectedIndex];
        };
        Children.Add(new TextBlock { Text = "量化", VerticalAlignment = VerticalAlignment.Center });
        Children.Add(mDivision);
        Children.Add(mTuplet);
    }

    void AddAction(string label, string id)
    {
        var button = CreateButton(label);
        button.Click += (_, _) =>
        {
            mView.Actions.Execute(id);
            Refresh();
            mView.Piano.PianoScrollView.Focus();
        };
        mActions.Add((button, id));
    }

    void AddTool(string label, PianoTool tool)
    {
        var button = CreateButton(label);
        button.Click += (_, _) =>
        {
            string id = tool switch
            {
                PianoTool.Note => "tool.note",
                PianoTool.Pitch => "tool.pitch",
                PianoTool.Anchor => "tool.anchor",
                PianoTool.Lock => "tool.lock",
                _ => "tool.vibrato"
            };
            mView.Actions.Execute(id);
            Refresh();
            mView.Piano.PianoScrollView.Focus();
        };
        mTools.Add((button, tool));
    }

    Button CreateButton(string label)
    {
        var button = new Button
        {
            Content = label,
            Margin = new Thickness(0, 0, 4, 4),
            Padding = new Thickness(8, 4),
            Focusable = false
        };
        Children.Add(button);
        return button;
    }

    internal void Refresh()
    {
        bool instrument = mView.Piano.Part?.SoundSource.Kind == SourceKind.Instrument;
        if (instrument && mView.PianoTool.Value == PianoTool.Vibrato)
            mView.PianoTool.Value = PianoTool.Note;

        foreach (var (button, tool) in mTools)
        {
            button.IsEnabled = mView.Piano.CanRunEditCommand && (tool != PianoTool.Vibrato || !instrument);
            button.Opacity = mView.PianoTool.Value == tool ? 1 : 0.65;
        }
        mDivision.IsEnabled = mView.Piano.CanRunEditCommand;
        mTuplet.IsEnabled = mView.Piano.CanRunEditCommand;
        foreach (var (button, id) in mActions)
        {
            button.IsEnabled = mView.Actions.TryGet(id, out var action) &&
                action.Unavailable?.Invoke() == null;
        }
    }
}
