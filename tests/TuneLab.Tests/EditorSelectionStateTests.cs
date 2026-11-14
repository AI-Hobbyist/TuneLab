using System;
using System.Linq;
using System.Threading;
using TuneLab.Data;
using TuneLab.Foundation;
using TuneLab.Hosting;
using TuneLab.Scripting;
using TuneLab.SDK;
using TuneLab.UI;
using TuneLab.Configs;
using Xunit;

namespace TuneLab.Tests;

[Collection("DataThreadProject")]
public class EditorSelectionStateTests
{
    [Fact]
    public void BulkLyricsUseTheRequestedNotesAndCommitOneUndoWhilePreservingContinuation()
    {
        var previous = SynchronizationContext.Current;
        var dataThread = new PumpableSynchronizationContext();
        SynchronizationContext.SetSynchronizationContext(dataThread);
        bool previousG2p = Settings.AutoGeneratePronunciation.Value;
        try
        {
            Settings.AutoGeneratePronunciation.Value = false;
            TestVoices.EnsureBuiltIn();
            using var context = new CubaseDocumentContext();
            context.Document.SetProject(new Project(new ProjectInfo(), registerWithStandaloneAudio: false));
            var part = context.GetOrCreateEditingPart();
            foreach (var (index, lyric) in new[] { "one", "-", "three", "untouched" }.Select((value, index) => (index, value)))
                part.InsertNote(part.CreateNote(new NoteInfo { Pos = index * 480, Dur = 240, Pitch = 60, Lyric = lyric }));
            context.Document.Commit();
            var notes = part.Notes.ToArray();
            var first = new EditorSelectionState(isLocal: true);
            var second = new EditorSelectionState(isLocal: true);
            notes.Take(3).SelectAllItems(first);
            notes[3].Select(second);

            LyricInput.ApplyLyrics(part.Notes.AllSelectedItems(first), "la - mi", skipTenuto: true);
            Assert.Equal(new[] { "la", "-", "mi", "untouched" }, part.Notes.Select(note => note.Lyric.Value));
            Assert.True(context.Document.Undo());
            Assert.Equal(new[] { "one", "-", "three", "untouched" }, part.Notes.Select(note => note.Lyric.Value));
            Assert.Same(notes[3], Assert.Single(part.Notes.AllSelectedItems(second)));
            Assert.All(part.Notes, note => Assert.False(note.IsSelected));
            dataThread.DrainAll();
        }
        finally
        {
            Settings.AutoGeneratePronunciation.Value = previousG2p;
            dataThread.DrainAll();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void SharedNotesCopyDeletePasteAndUndoUseOnlyTheOwningSelection()
    {
        var previous = SynchronizationContext.Current;
        var dataThread = new PumpableSynchronizationContext();
        SynchronizationContext.SetSynchronizationContext(dataThread);
        try
        {
            TestVoices.EnsureBuiltIn();
            using var context = new CubaseDocumentContext();
            context.Document.SetProject(new Project(new ProjectInfo(), registerWithStandaloneAudio: false));
            var part = context.GetOrCreateEditingPart();
            var firstNote = part.CreateNote(new NoteInfo { Pos = 0, Dur = 240, Pitch = 60, Lyric = "first" });
            var secondNote = part.CreateNote(new NoteInfo { Pos = 480, Dur = 240, Pitch = 64, Lyric = "second" });
            part.InsertNote(firstNote);
            part.InsertNote(secondNote);
            context.Document.Commit();
            var first = new EditorSelectionState(isLocal: true);
            var second = new EditorSelectionState(isLocal: true);
            firstNote.Select(first);
            secondNote.Select(second);
            Assert.Equal("first", Assert.Single(part.CopyNotes(first)).Lyric);
            Assert.Equal("second", Assert.Single(part.CopyNotes(second)).Lyric);
            Assert.All(part.Notes, note => Assert.False(note.IsSelected));

            part.DeleteAllSelectedNotes(first);
            context.Document.Commit();
            Assert.Same(secondNote, Assert.Single(part.Notes));
            Assert.Same(secondNote, Assert.Single(part.Notes.AllSelectedItems(second)));
            Assert.True(context.Document.Undo());
            Assert.Same(firstNote, Assert.Single(part.Notes.AllSelectedItems(first)));
            part.PasteAt(part.CopyNotes(first), 960, selection: first);
            context.Document.Commit();
            var pasted = Assert.Single(part.Notes.AllSelectedItems(first));
            Assert.NotSame(firstNote, pasted);
            Assert.Equal(960, pasted.Pos.Value);
            Assert.Equal("first", pasted.Lyric.Value);
            Assert.Same(secondNote, Assert.Single(part.Notes.AllSelectedItems(second)));
            Assert.All(part.Notes, note => Assert.False(note.IsSelected));

            var firstVibrato = part.CreateVibrato(new VibratoInfo { Pos = 0, Dur = 240, Amplitude = 1 });
            var secondVibrato = part.CreateVibrato(new VibratoInfo { Pos = 480, Dur = 240, Amplitude = 2 });
            part.InsertVibrato(firstVibrato);
            part.InsertVibrato(secondVibrato);
            context.Document.Commit();
            firstVibrato.Select(first);
            secondVibrato.Select(second);
            var vibratoCopy = part.CopyVibratos(first);
            Assert.Equal(1, Assert.Single(vibratoCopy).Amplitude);
            part.DeleteAllSelectedVibratos(first);
            context.Document.Commit();
            Assert.Same(secondVibrato, Assert.Single(part.Vibratos));
            Assert.True(context.Document.Undo());
            part.PasteAt(vibratoCopy, 960, selection: first);
            context.Document.Commit();
            Assert.Equal(960, Assert.Single(part.Vibratos.AllSelectedItems(first)).Pos.Value);
            Assert.Same(secondVibrato, Assert.Single(part.Vibratos.AllSelectedItems(second)));
            Assert.All(part.Vibratos, vibrato => Assert.False(vibrato.IsSelected));

            var result = ScriptRunner.Run(context.Document.Project!, () => part, null, null, null, null,
                ScriptLimits.Interactive, """
                const p = tl.currentProject().tracks()[0].parts()[0];
                print(p.selectedNotes()[0].lyric);
                p.selectNotes([p.notes()[0]]);
                throw new Error('expected rollback');
                """, CancellationToken.None, objectSelection: first);
            Assert.False(result.Ok);
            Assert.Contains("first", result.Output);
            Assert.Same(pasted, Assert.Single(part.Notes.AllSelectedItems(first)));
            Assert.Same(secondNote, Assert.Single(part.Notes.AllSelectedItems(second)));
            Assert.All(part.Notes, note => Assert.False(note.IsSelected));
            dataThread.DrainAll();
        }
        finally
        {
            dataThread.DrainAll();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void ContinuousAnchorReconstructionKeepsLocalSelectionWithNonzeroDefault()
    {
        var automation = new Automation(null!, new AutomationInfo { DefaultValue = 7.5 });
        var first = new EditorSelectionState(isLocal: true);
        var second = new EditorSelectionState(isLocal: true);
        var firstInput = new AnchorPoint(0, 9);
        firstInput.Select(first);
        automation.InsertPoint(firstInput, first);
        var secondInput = new AnchorPoint(480, 12);
        secondInput.Select(second);
        automation.InsertPoint(secondInput, second);
        var secondStored = Assert.Single(automation.Points.AllSelectedItems(second));

        automation.MoveSelectedPoints(120, 2, first);
        var moved = Assert.Single(automation.Points.AllSelectedItems(first));
        Assert.Equal(120, moved.Pos);
        Assert.Equal(3.5, moved.Value);
        Assert.Same(secondStored, Assert.Single(automation.Points.AllSelectedItems(second)));
        Assert.Equal(480, secondStored.Pos);
        Assert.All(automation.Points, point => Assert.False(point.IsSelected));
    }

    [Theory]
    [InlineData(0, 2)]
    [InlineData(120, 0)]
    [InlineData(-120, 2)]
    public void PiecewiseMoveAndDeleteRespectTheLocalAnchorSelection(double offsetPos, double offsetValue)
    {
        var automation = new PiecewiseAutomation();
        automation.AddLine([new AnchorPoint(0, 60), new AnchorPoint(480, 64)], 0);
        var first = new EditorSelectionState(isLocal: true);
        var second = new EditorSelectionState(isLocal: true);
        automation.AnchorGroups[0][0].Select(first);
        var other = automation.AnchorGroups[0][1];
        other.Select(second);
        automation.MoveSelectedPoints(offsetPos, offsetValue, first);
        var moved = Assert.Single(automation.AnchorGroups.SelectMany(group => group).AllSelectedItems(first));
        Assert.Equal(offsetPos, moved.Pos);
        Assert.Equal(60 + offsetValue, moved.Value);
        Assert.True(second.IsSelected(other));
        automation.DeleteAllSelectedAnchors(first);
        Assert.Same(other, Assert.Single(automation.AnchorGroups.SelectMany(group => group)));
        Assert.True(second.IsSelected(other));
        Assert.False(other.IsSelected);
    }

    [Fact]
    public void LocalBatchesNotifyOnceAndDefaultSelectionStillUsesModelFlags()
    {
        AnchorPoint[] points = [new(0, 0), new(480, 1)];
        var local = new EditorSelectionState(isLocal: true);
        var other = new EditorSelectionState(isLocal: true);
        int notifications = 0;
        local.Changed.Subscribe(() => notifications++);
        points.SelectAllItems(local);
        Assert.Equal(1, notifications);
        Assert.Equal(2, points.AllSelectedItems(local).Count);
        Assert.Empty(points.AllSelectedItems(other));
        Assert.Empty(points.AllSelectedItems());
        var standalone = new EditorSelectionState(isLocal: false);
        points[0].Select(standalone);
        Assert.True(points[0].IsSelected);
        Assert.Same(points[0], Assert.Single(points.AllSelectedItems()));
        local.Clear();
        Assert.Empty(points.AllSelectedItems(local));
        Assert.True(points[0].IsSelected);
    }
}
