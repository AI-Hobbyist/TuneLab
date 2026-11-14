using System;
using System.Threading;
using TuneLab.Data;
using TuneLab.Extensions.Voices;
using TuneLab.Hosting;
using Xunit;

namespace TuneLab.Tests;

[Collection("DataThreadProject")]
public class CubaseVoiceSelectionTests
{
    sealed class DataContext : SynchronizationContext
    {
        public override void Post(SendOrPostCallback callback, object? state) => callback(state);
    }

    [Fact]
    public void ExistingEngineSelectionIsUndoableAndAffectsOnlyItsPart()
    {
        var previous = SynchronizationContext.Current;
        SynchronizationContext.SetSynchronizationContext(new DataContext());
        try
        {
            TestVoices.EnsureBuiltIn();
            const string type = "cubase-selection-test";
            VoicesManager.RegisterEngine("cubase-selection-test", type, "Test Engine",
                new EmptyVoiceSynthesisEngine());
            VoicesManager.InitEngine(type);
            using var first = new CubaseDocumentContext();
            using var second = new CubaseDocumentContext();
            first.Document.SetProject(new Project());
            second.Document.SetProject(new Project());
            var firstPart = first.GetOrCreateEditingPart();
            var secondPart = second.GetOrCreateEditingPart();
            var previousPipeline = ((MidiPart)firstPart).SynthesisPipeline;
            var secondPipeline = ((MidiPart)secondPart).SynthesisPipeline;

            Assert.Contains(CubaseVoiceSelection.Engines(), engine => engine.Type == type);
            Assert.Contains(CubaseVoiceSelection.Singers(type), singer => singer.Id == "");
            CubaseVoiceSelection.Select(firstPart, type, "");
            Assert.Equal(type, firstPart.SoundSource.Type);
            Assert.NotSame(previousPipeline, ((MidiPart)firstPart).SynthesisPipeline);
            Assert.Same(secondPipeline, ((MidiPart)secondPart).SynthesisPipeline);
            Assert.Equal("", secondPart.SoundSource.Type);
            using var restored = new CubaseDocumentContext();
            restored.RestoreState(first.SaveState());
            Assert.Equal(type, restored.GetOrCreateEditingPart().SoundSource.Type);
            Assert.True(first.Document.Undo());
            Assert.Equal("", firstPart.SoundSource.Type);
            Assert.True(first.Document.Redo());
            Assert.Equal(type, firstPart.SoundSource.Type);

            var head = first.Document.Head;
            Assert.Throws<InvalidOperationException>(() =>
                CubaseVoiceSelection.Select(firstPart, type, "missing-singer"));
            Assert.Equal(head, first.Document.Head);
            Assert.Equal(type, firstPart.SoundSource.Type);
        }
        finally
        {
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }
}
