using System;
using System.Threading;
using System.Linq;
using TuneLab.Data;
using TuneLab.Hosting;
using TuneLab.Scripting;
using TuneLab.UI;
using TuneLab.SDK;
using Xunit;
using System.Threading.Tasks;
using TuneLab.Foundation;

namespace TuneLab.Tests;

[Collection("DataThreadProject")]
public class CubaseScriptScopeTests
{
    [Theory]
    [InlineData("confirm")]
    [InlineData("cancel")]
    [InlineData("closed")]
    [InlineData("project-changed")]
    public async Task PromptWaitsForConfirmationAndRejectsCancelledOrReplacedTargets(string outcome)
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new ImmediateContext());
        var original = new Project(new ProjectInfo(), registerWithStandaloneAudio: false);
        var replacement = new Project(new ProjectInfo(), registerWithStandaloneAudio: false);
        try
        {
            IProject? current = original;
            var scope = new ScriptToolMenu.Scope(() => current, () => null, () => null,
                () => null, () => null, new SelectionWriter());
            var completion = new TaskCompletionSource<PropertyObject?>();
            var schema = ObjectConfig.Create([]);
            var request = new ScriptToolMenu.InputRequest("Test", schema, _ => (schema, null), new PropertyObject([]));
            scope.PromptInputs = value =>
            {
                Assert.Same(request, value);
                return completion.Task;
            };

            var pending = scope.PickInputsAsync(request);
            Assert.False(pending.IsCompleted);
            Assert.Empty(original.Tracks);
            if (outcome == "closed")
                scope.Deactivate();
            if (outcome == "project-changed")
                current = replacement;
            var picked = new PropertyObject([]);
            completion.SetResult(outcome == "cancel" ? null : picked);
            var result = await pending;
            if (outcome == "confirm")
                Assert.Same(picked, result);
            else
                Assert.Null(result);
            Assert.Empty(original.Tracks);
            Assert.Empty(replacement.Tracks);
            if (outcome == "closed")
                Assert.Throws<InvalidOperationException>(() => scope.RunScript("tl.currentProject().addTrack({ name: 'late' });"));
        }
        finally
        {
            original.Dispose();
            replacement.Dispose();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void MenuScriptEditsSelectionAndUndoStayInTheirOwningDocument()
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new ImmediateContext());
        try
        {
            TestVoices.EnsureBuiltIn();
            using var first = new CubaseDocumentContext();
            using var second = new CubaseDocumentContext();
            first.Document.SetProject(new Project(new ProjectInfo(), registerWithStandaloneAudio: false));
            second.Document.SetProject(new Project(new ProjectInfo(), registerWithStandaloneAudio: false));
            first.GetOrCreateEditingPart();
            second.GetOrCreateEditingPart();
            var firstSelection = new SelectionWriter();
            var secondSelection = new SelectionWriter();
            var firstScope = CreateScope(first, firstSelection);
            var secondScope = CreateScope(second, secondSelection);

            var result = firstScope.RunScript("""
                tl.currentProject().addTrack({ name: 'first-only' });
                tl.setPianoSelection(120, 480);
                """);
            Assert.True(result.Ok, result.Error);
            Assert.Equal("first-only", first.Document.Project!.Tracks.Last().Name.Value);
            Assert.Single(second.Document.Project!.Tracks);
            Assert.Equal((120d, 480d), firstSelection.Range);
            Assert.Null(secondSelection.Range);

            result = secondScope.RunScript("tl.currentProject().addTrack({ name: 'second-only' });");
            Assert.True(result.Ok, result.Error);
            Assert.True(first.Document.Undo());
            Assert.Single(first.Document.Project.Tracks);
            Assert.Equal("second-only", second.Document.Project.Tracks.Last().Name.Value);
            Assert.True(first.Document.Redo());
            Assert.Equal("first-only", first.Document.Project.Tracks.Last().Name.Value);

            result = firstScope.RunScript("""
                tl.currentProject().addTrack({ name: 'rolled-back' });
                throw new Error('expected failure');
                """);
            Assert.False(result.Ok);
            Assert.Equal(2, first.Document.Project.Tracks.Count);
            Assert.Equal("first-only", first.Document.Project.Tracks.Last().Name.Value);
            Assert.Equal("second-only", second.Document.Project.Tracks.Last().Name.Value);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    static ScriptToolMenu.Scope CreateScope(CubaseDocumentContext context, SelectionWriter writer)
        => new(() => context.Document.Project, context.GetOrCreateEditingPart, () => null, () => null, () => null, writer);

    sealed class SelectionWriter : IScriptSelectionWriter
    {
        internal (double Start, double End)? Range { get; private set; }
        public void SetPianoSelection(double startTick, double endTick) => Range = (startTick, endTick);
        public void ClearPianoSelection() => Range = null;
        public void SetTrackSelection(double startTick, double endTick, int startTrackNumber, int endTrackNumber)
            => throw new NotSupportedException();
        public void ClearTrackSelection() => throw new NotSupportedException();
    }

    sealed class ImmediateContext : SynchronizationContext
    {
        public override void Post(SendOrPostCallback callback, object? state) => callback(state);
        public override void Send(SendOrPostCallback callback, object? state) => callback(state);
    }
}
