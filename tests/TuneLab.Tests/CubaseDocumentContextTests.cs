using System;
using System.Threading;
using System.Linq;
using TuneLab.Data;
using TuneLab.Hosting;
using TuneLab.SDK;
using TuneLab.Input;
using Xunit;

namespace TuneLab.Tests;

[Collection("DataThreadProject")]
public class CubaseDocumentContextTests
{
    [Fact]
    public void CopiedNoteDoesNotInheritSourceIdentityAndDeletionUndoRestoresAssociation()
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DataThreadContext());
        try
        {
            TestVoices.EnsureBuiltIn();
            var projection = CubaseAraScore.Create([new(0, 0), new(4, 8)],
                [new(9, 0.5, 0.5, 60)], new(0, 4, 0, 4), new SoundSourceInfo());
            using var context = new CubaseDocumentContext();
            context.RestoreState(CubaseAraMerge.UpdateState([], projection));
            context.ApplyHostLyrics("", [new(9, "你")]);
            var part = context.GetOrCreateEditingPart();
            var original = part.Notes.First!;
            var copyInfo = ((Note)original).GetInfo();
            copyInfo.Pos += 960;
            var copy = part.CreateNote(copyInfo);
            part.InsertNote(copy);
            context.Document.Commit();
            Assert.Equal(new ulong[] { 9, 0 }, CubaseProjectState.DecodeEnvelope(context.SaveState()).Ara!.CurrentNoteIds);
            context.ApplyHostLyrics("", [new(9, "好")]);
            Assert.Equal("好", original.Lyric.Value);
            Assert.Equal("你", copy.Lyric.Value);
            part.RemoveNote(original);
            context.Document.Commit();
            Assert.True(context.Document.Undo());
            Assert.Same(original, part.Notes.First);
            Assert.Equal(new ulong[] { 9 }, context.ApplyHostLyrics("", [new(9, "再")]).ChangedNotes);
            Assert.Equal("再", original.Lyric.Value);
            Assert.Equal("你", copy.Lyric.Value);
            using var reopened = new CubaseDocumentContext();
            reopened.RestoreState(context.SaveState());
            reopened.ApplyHostLyrics("", [new(9, "见")]);
            Assert.Equal(new[] { "见", "你" }, reopened.GetOrCreateEditingPart().Notes.Select(note => note.Lyric.Value));
            var before = reopened.SaveState();
            var invalid = before.ToArray();
            System.Buffers.Binary.BinaryPrimitives.WriteUInt32LittleEndian(invalid.AsSpan(8), 999);
            Assert.Throws<NotSupportedException>(() => reopened.RestoreState(invalid));
            Assert.Equal(before, reopened.SaveState());
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void HostLyricsKeepLiveObjectsAndUndoAndPublishOneSettledUpdate()
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DataThreadContext());
        try
        {
            TestVoices.EnsureBuiltIn();
            var projection = CubaseAraScore.Create([new(0, 0), new(2, 4)],
                [new(9, 0.5, 0.5, 60)], new(0, 2, 0, 2), new SoundSourceInfo());
            using var context = new CubaseDocumentContext();
            context.RestoreState(CubaseAraMerge.UpdateState([], projection));
            var part = context.GetOrCreateEditingPart();
            var note = part.Notes.First!;
            note.IsSelected = true;
            var snapshots = new System.Collections.Generic.List<byte[]>();
            context.StateChanged += snapshots.Add;
            var result = context.ApplyHostLyrics("", [new(9, "你")]);
            Assert.Equal(new ulong[] { 9 }, result.ChangedNotes);
            Assert.Same(part, context.GetOrCreateEditingPart());
            Assert.Same(note, part.Notes.First);
            Assert.True(note.IsSelected);
            Assert.Equal("你", note.Lyric.Value);
            Assert.Single(snapshots);
            Assert.Equal(result.State, snapshots[0]);
            context.ApplyHostLyrics("", [new(9, "你")]);
            Assert.Single(snapshots);
            context.ApplyHostLyrics("", [new(9, "好")]);
            Assert.Equal("好", note.Lyric.Value);
            Assert.Equal(2, snapshots.Count);
            Assert.True(context.Document.Undo());
            Assert.Equal("你", note.Lyric.Value);
            Assert.True(context.Document.Redo());
            Assert.Equal("好", note.Lyric.Value);
            note.Lyric.Set("local");
            Assert.Throws<InvalidOperationException>(() => context.ApplyHostLyrics("", [new(9, "host")]));
            Assert.Equal("local", note.Lyric.Value);
            context.Document.Commit();
            int notifications = snapshots.Count;
            var conflict = context.ApplyHostLyrics("", [new(9, "host")]);
            Assert.Equal(new ulong[] { 9 }, conflict.Conflicts);
            Assert.Equal("local", note.Lyric.Value);
            Assert.Equal(notifications + 1, snapshots.Count);
            using var reopened = new CubaseDocumentContext();
            reopened.RestoreState(context.SaveState());
            Assert.Equal("local", reopened.GetOrCreateEditingPart().Notes.First!.Lyric.Value);
            Assert.Empty(reopened.ApplyHostLyrics("", [new(9, "host")]).ChangedNotes);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void AraAssociationSurvivesEditsDeletionUndoAndDocumentReopen()
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DataThreadContext());
        try
        {
            TestVoices.EnsureBuiltIn();
            var projection = CubaseAraScore.Create([new(0, 0), new(2, 4)],
                [new(9, 0.5, 0.5, 60)], new(0, 2, 0, 2), new SoundSourceInfo(),
                [new(-1, 4, 4), new(7, 3, 4)]);
            var metadata = new CubaseAraMetadata
            {
                SourceId = "source-a",
                BaselineState = CubaseProjectState.Encode(projection.File),
                BaselineNoteIds = [9],
                CurrentNoteIds = [9],
                MeterOriginTick = projection.MeterOriginTick
            };
            using var context = new CubaseDocumentContext();
            context.RestoreState(CubaseProjectState.Encode(projection.File, metadata));
            Assert.Equal(-480, context.Document.Project!.TimeSignatureManager.TimeSignatures[0].Pos);
            var part = context.GetOrCreateEditingPart();
            var imported = part.Notes.First!;
            imported.Lyric.Set("你");
            part.InsertNote(part.CreateNote(new NoteInfo { Pos = 1200, Dur = 120, Pitch = 65, Lyric = "手工" }));
            context.Document.Commit();
            var saved = context.SaveState();
            Assert.Equal(new ulong[] { 9, 0 }, CubaseProjectState.DecodeEnvelope(saved).Ara!.CurrentNoteIds);
            part.RemoveNote(imported);
            context.Document.Commit();
            Assert.Equal(new ulong[] { 0 }, CubaseProjectState.DecodeEnvelope(context.SaveState()).Ara!.CurrentNoteIds);
            Assert.True(context.Document.Undo());
            Assert.Equal(new ulong[] { 9, 0 }, CubaseProjectState.DecodeEnvelope(context.SaveState()).Ara!.CurrentNoteIds);
            using var reopened = new CubaseDocumentContext();
            reopened.RestoreState(saved);
            var restored = CubaseProjectState.DecodeEnvelope(reopened.SaveState());
            Assert.Equal("source-a", restored.Ara!.SourceId);
            Assert.Equal(-480, restored.Ara.MeterOriginTick);
            Assert.Equal(-480, reopened.Document.Project!.TimeSignatureManager.TimeSignatures[0].Pos);
            Assert.Equal(new ulong[] { 9, 0 }, restored.Ara.CurrentNoteIds);
            Assert.Equal("你", reopened.GetOrCreateEditingPart().Notes.First!.Lyric.Value);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void StateNotificationsContainOnlySettledEditsAndDoNotEchoRestores()
    {
        using var context = new CubaseDocumentContext();
        context.Document.SetProject(new Project());
        var snapshots = new System.Collections.Generic.List<byte[]>();
        context.StateChanged += snapshots.Add;
        context.Document.Project!.AddTrack(new TrackInfo { Name = "Changed" });
        Assert.Empty(snapshots);
        context.Document.Commit();
        Assert.Single(snapshots);
        Assert.Equal("Changed", Assert.Single(CubaseProjectState.Decode(snapshots[0]).Project.Tracks).Name);
        context.Document.Undo();
        Assert.Equal(2, snapshots.Count);
        Assert.Empty(CubaseProjectState.Decode(snapshots[1]).Project.Tracks);
        context.Document.Redo();
        Assert.Equal(3, snapshots.Count);
        context.RestoreState(snapshots[0]);
        Assert.Equal(3, snapshots.Count);
        context.Dispose();
        Assert.Equal(3, snapshots.Count);
    }

    sealed class DataThreadContext : SynchronizationContext
    {
        public override void Post(SendOrPostCallback callback, object? state) => callback(state);
        public override void Send(SendOrPostCallback callback, object? state) => callback(state);
    }

    [Fact]
    public void StateRestoresAnIndependentDocumentAndInvalidStatePreservesIt()
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DataThreadContext());
        try
        {
            TestVoices.EnsureBuiltIn();
            using var source = new CubaseDocumentContext();
            using var target = new CubaseDocumentContext();
            source.Document.SetProject(new Project());
            target.Document.SetProject(new Project());
            var sourcePart = source.GetOrCreateEditingPart();
            sourcePart.Name.Set("Saved phrase");
            source.Document.Commit();
            source.Document.Project!.ExportSampleRate = 48000;
            source.EditorMetadata.PlayheadPos = 720;

            var state = source.SaveState();
            target.RestoreState(state);
            var targetPart = target.GetOrCreateEditingPart();
            Assert.NotSame(sourcePart, targetPart);
            Assert.Equal("Saved phrase", targetPart.Name.Value);
            Assert.Equal(48000, target.Document.Project!.ExportSampleRate);
            Assert.Equal(720, target.EditorMetadata.PlayheadPos);
            var project = target.Document.Project;
            state[0] = 0;
            Assert.Throws<System.IO.InvalidDataException>(() => target.RestoreState(state));
            Assert.Same(project, target.Document.Project);
            Assert.Same(targetPart, target.GetOrCreateEditingPart());
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void EditingPartAndHistorySurviveRepeatedLookupAndRemainInstanceLocal()
    {
        var previousContext = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DataThreadContext());
        try
        {
            TestVoices.EnsureBuiltIn();
            using var first = new CubaseDocumentContext();
            using var second = new CubaseDocumentContext();
            first.Document.SetProject(new Project());
            second.Document.SetProject(new Project());

            var firstPart = first.GetOrCreateEditingPart();
            var secondPart = second.GetOrCreateEditingPart();
            firstPart.Name.Set("Edited part");
            first.Document.Commit();

            Assert.Same(firstPart, first.GetOrCreateEditingPart());
            Assert.Equal("Edited part", firstPart.Name.Value);
            Assert.NotSame(firstPart, secondPart);
            Assert.Equal("TuneLab", secondPart.Name.Value);
            Assert.Single(first.Document.Project!.Tracks);
            Assert.Single(second.Document.Project!.Tracks);

            Assert.True(first.Document.Undo());
            Assert.Equal("TuneLab", firstPart.Name.Value);
            first.Dispose();
            Assert.Same(secondPart, second.GetOrCreateEditingPart());
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previousContext);
        }
    }

    [Fact]
    public void UndoHistoryIsIndependentBetweenDocuments()
    {
        using var first = new CubaseDocumentContext();
        using var second = new CubaseDocumentContext();
        first.Document.SetProject(new Project());
        second.Document.SetProject(new Project());

        first.Document.Project!.AddTrack(new TrackInfo { Name = "First" });
        first.Document.Commit();
        second.Document.Project!.AddTrack(new TrackInfo { Name = "Second" });
        second.Document.Commit();

        Assert.True(first.Document.Undo());
        Assert.Empty(first.Document.Project.Tracks);
        Assert.Single(second.Document.Project.Tracks);
        Assert.True(second.Document.Undoable());

        first.Dispose();
        Assert.True(second.Document.Undo());
        Assert.Empty(second.Document.Project.Tracks);
    }

    [Fact]
    public void DocumentsAndProjectsBelongToTheirOwnInstances()
    {
        using var first = new CubaseDocumentContext();
        using var second = new CubaseDocumentContext();
        first.Document.SetProject(new Project());
        second.Document.SetProject(new Project());

        Assert.NotSame(first.Document, second.Document);
        Assert.NotSame(first.Document.Project, second.Document.Project);
        Assert.Same(first.Document, first.Document.Project!.Document);
        Assert.Same(second.Document, second.Document.Project!.Document);

        first.Dispose();

        Assert.Throws<ObjectDisposedException>(() => first.Document);
        Assert.NotNull(second.Document.Project);
        Assert.Same(second.Document, second.Document.Project!.Document);
    }

    [Fact]
    public void DisposalDetachesProjectAndCanBeRepeated()
    {
        var context = new CubaseDocumentContext();
        var project = new Project();
        context.Document.SetProject(project);
        context.Actions.Register(new EditorAction
        {
            Id = "test.dispose",
            DisplayName = () => "Dispose probe",
            Kind = ActionKind.AppState,
            Execute = () => throw new InvalidOperationException("Disposed callback ran")
        });

        context.Dispose();
        context.Dispose();

        Assert.Null(project.Document);
        Assert.Empty(context.Actions.Actions);
        Assert.Throws<ObjectDisposedException>(() => context.Document);
    }
}
