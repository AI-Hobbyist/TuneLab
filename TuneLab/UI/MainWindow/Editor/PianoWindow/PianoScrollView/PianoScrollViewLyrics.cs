using System;
using System.Linq;
using TuneLab.Data;
using TuneLab.Foundation;
using TuneLab.Utils;
using TuneLab.I18N;
using TuneLab.SDK;

namespace TuneLab.UI;

internal partial class PianoScrollView
{
    async void EnterLyrics()
    {
        var part = Part;
        if (part == null)
            return;
        var notes = part.Notes.AllSelectedItems(ObjectSelection).ToArray();
        if (notes.Length == 0)
            return;
        var scope = ScriptScope;
        if (scope == null)
        {
            LyricInput.EnterInput(notes, this.Window());
            return;
        }

        string title = "Input Lyrics".Tr(TC.Dialog);
        string text = string.Join(' ', notes.Select(note => note.Lyric.Value));
        var schema = ObjectConfig.Create(new OrderedMap<PropertyKey, IControllerConfig>
        {
            { ("lyrics", title), TextBoxConfig.Create(text).WithMultiline(8) },
            { ("skipTenuto", "Skip Tenuto".Tr(TC.Dialog)), CheckBoxConfig.Create() }
        });
        try
        {
            var picked = await scope.PickInputsAsync(new ScriptToolMenu.InputRequest(
                title, schema, _ => (schema, null), PropertyObject.Empty));
            if (picked == null)
                return;
            if (!ReferenceEquals(Part, part) || notes.Any(note => !part.Notes.Contains(note)))
            {
                scope.ReportError?.Invoke(title, "The selected notes changed while the form was open.");
                return;
            }
            if (!part.Pushable())
            {
                scope.ReportError?.Invoke(title, "An editing operation is active.");
                return;
            }
            LyricInput.ApplyLyrics(notes, picked.GetValue("lyrics", text), picked.GetValue("skipTenuto", false));
        }
        catch (Exception exception)
        {
            if (scope.IsActive)
                scope.ReportError?.Invoke(title, exception.Message);
        }
    }
}
