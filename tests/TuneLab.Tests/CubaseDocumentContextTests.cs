using System;
using System.Threading;
using TuneLab.Data;
using TuneLab.Hosting;
using TuneLab.SDK;
using TuneLab.Input;
using Xunit;

namespace TuneLab.Tests;

[Collection("DataThreadProject")]
public class CubaseDocumentContextTests
{
    sealed class DataThreadContext : SynchronizationContext
    {
        public override void Post(SendOrPostCallback callback, object? state) => callback(state);
        public override void Send(SendOrPostCallback callback, object? state) => callback(state);
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
