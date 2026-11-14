using System;
using System.Threading;
using TuneLab.Audio;
using TuneLab.Data;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.Hosting;
using TuneLab.SDK;
using TuneLab.Scripting;
using Xunit;

namespace TuneLab.Tests;

[Collection("DataThreadProject")]
public class CubaseSnapshotRendererTests
{
    [Fact]
    public void SnapshotMixerExcludesTheStandaloneGraphsLongerTrack()
    {
        var previous = SynchronizationContext.Current;
        var dataContext = new PumpableSynchronizationContext();
        SynchronizationContext.SetSynchronizationContext(dataContext);
        try
        {
            TestVoices.EnsureBuiltIn();
            var unrelated = new NativeProjectFile();
            unrelated.Project.Tracks.Add(new TrackInfo
            {
                Parts = { new MidiPartInfo { EndOffset = 48000 } }
            });
            using var standalone = new Project(unrelated.Project);
            // Project construction closes a merged notification scope after
            // activating its parts. Run that queued rebuild on the owner
            // thread, just as the renderer's actual pump does.
            Assert.True(dataContext.DrainAll());
            double standaloneEnd = AudioEngine.EndTime;
            var snapshot = new NativeProjectFile();
            snapshot.Project.Tracks.Add(new TrackInfo
            {
                Parts = { new MidiPartInfo { EndOffset = 1920 } }
            });

            var result = CubaseSnapshotRenderer.Render(CubaseProjectState.Encode(snapshot),
                TimeSpan.FromSeconds(5));
            Assert.True(standaloneEnd > 3);
            Assert.Equal(AudioEngine.SampleRate.Value, result.SampleRate);
            Assert.Equal(result.SampleRate * 3 * 2, result.Interleaved.Length);
            Assert.Equal(standaloneEnd, AudioEngine.EndTime);
            Assert.All(result.Interleaved, sample => Assert.Equal(0, sample));
        }
        finally
        {
            dataContext.DrainAll();
            SynchronizationContext.SetSynchronizationContext(previous);
        }
    }

    [Fact]
    public void CancelledJobDoesNotConstructAProject()
    {
        using var cancellation = new CancellationTokenSource();
        cancellation.Cancel();
        Assert.Throws<OperationCanceledException>(() => CubaseSnapshotRenderer.Render(
            Array.Empty<byte>(), TimeSpan.FromSeconds(5), cancellation.Token));
    }
}
