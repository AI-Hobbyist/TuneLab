using System.Reflection;
using System.Runtime.Loader;
using TuneLab.Hosting;
using Xunit;

namespace TuneLab.Tests;

public class CubasePairRegistryTests
{
    [Fact]
    public void RestoredIntentDoesNotClaimLivePeerOrPublishTemporaryOutput()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var bridge = CubasePairRegistry.RegisterBridge();
        var copy = CubasePairRegistry.RegisterInstrument();
        var outputs = new List<bool>();
        try
        {
            CubasePairRegistry.Pair(instrument, bridge);
            CubasePairRegistry.SetOutputCallback(instrument, outputs.Add);
            outputs.Clear();
            CubasePairRegistry.RestoreInstrumentIntent(instrument, true);
            CubasePairRegistry.RestoreInstrumentIntent(copy, true);
            Assert.Equal(new[] { false }, outputs);
            Assert.Equal(CubasePairStatus.PeerMissing, CubasePairRegistry.GetState(instrument).Status);
            Assert.Null(CubasePairRegistry.GetState(instrument).LogicalOwner);
            Assert.False(CubasePairRegistry.GetState(copy).OwnsProjectOutput);
            Assert.NotEqual(CubasePairRegistry.GetState(instrument).Peer, CubasePairRegistry.GetState(copy).Peer);
            Assert.Equal(CubasePairStatus.Independent, CubasePairRegistry.GetState(bridge).Status);
            CubasePairRegistry.Pair(copy, bridge);
            Assert.Equal(CubasePairStatus.PeerMissing, CubasePairRegistry.GetState(instrument).Status);
            Assert.Equal(copy, CubasePairRegistry.GetState(bridge).Peer);
            CubasePairRegistry.RestoreInstrumentIntent(instrument, false);
            Assert.True(CubasePairRegistry.GetState(instrument).OwnsProjectOutput);
            Assert.Equal(new[] { false, true }, outputs);
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(copy);
            CubasePairRegistry.Unregister(bridge);
        }
    }

    [Fact]
    public void RegistrationDoesNotPairAndExplicitPairHasOneProjectOutput()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var bridge = CubasePairRegistry.RegisterBridge();
        try
        {
            Assert.Equal(CubasePairStatus.Independent, CubasePairRegistry.GetState(instrument).Status);
            Assert.Equal(CubasePairStatus.Independent, CubasePairRegistry.GetState(bridge).Status);
            CubasePairRegistry.Pair(instrument, bridge);
            CubasePairRegistry.Pair(instrument, bridge);
            var follower = CubasePairRegistry.GetState(instrument);
            var owner = CubasePairRegistry.GetState(bridge);
            Assert.Equal(CubasePairStatus.Paired, follower.Status);
            Assert.Equal(bridge, follower.LogicalOwner);
            Assert.Equal(bridge, owner.LogicalOwner);
            Assert.False(follower.OwnsProjectOutput);
            Assert.True(owner.OwnsProjectOutput);
            CubasePairRegistry.Unpair(bridge);
            Assert.Equal(CubasePairStatus.Independent, CubasePairRegistry.GetState(instrument).Status);
            Assert.True(CubasePairRegistry.GetState(instrument).OwnsProjectOutput);
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(bridge);
        }
    }

    [Fact]
    public void LivePairCannotBeStolenAndRemovalRequiresExplicitRebind()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var otherInstrument = CubasePairRegistry.RegisterInstrument();
        var bridge = CubasePairRegistry.RegisterBridge();
        var replacement = CubasePairRegistry.RegisterBridge();
        try
        {
            CubasePairRegistry.Pair(instrument, bridge);
            Assert.Throws<InvalidOperationException>(() => CubasePairRegistry.Pair(otherInstrument, bridge));
            Assert.Throws<ArgumentException>(() => CubasePairRegistry.Pair(bridge, instrument));
            Assert.Equal(bridge, CubasePairRegistry.GetState(instrument).Peer);
            CubasePairRegistry.Unregister(bridge);
            var missing = CubasePairRegistry.GetState(instrument);
            Assert.Equal(CubasePairStatus.PeerMissing, missing.Status);
            Assert.Null(missing.LogicalOwner);
            Assert.False(missing.OwnsProjectOutput);
            Assert.Equal(CubasePairStatus.Independent, CubasePairRegistry.GetState(replacement).Status);
            Assert.Throws<KeyNotFoundException>(() => CubasePairRegistry.Pair(instrument, bridge));
            CubasePairRegistry.Pair(instrument, replacement);
            Assert.Equal(replacement, CubasePairRegistry.GetState(instrument).LogicalOwner);
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(otherInstrument);
            CubasePairRegistry.Unregister(bridge);
            CubasePairRegistry.Unregister(replacement);
        }
    }

    [Fact]
    public void OutputCallbacksPublishPairRemovalAndExplicitUnlinkAndAreReleased()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var bridge = CubasePairRegistry.RegisterBridge();
        var output = new List<bool>();
        try
        {
            CubasePairRegistry.SetOutputCallback(instrument, output.Add);
            Assert.True(output.Last());
            CubasePairRegistry.Pair(instrument, bridge);
            Assert.False(output.Last());
            CubasePairRegistry.Unregister(bridge);
            Assert.False(output.Last());
            CubasePairRegistry.Unpair(instrument);
            Assert.True(output.Last());
            CubasePairRegistry.SetOutputCallback(instrument, null);
            int count = output.Count;
            CubasePairRegistry.Unpair(instrument);
            Assert.Equal(count, output.Count);
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(bridge);
        }
    }

    [Fact]
    public void SharedDocumentReadsAreCopiesAndWritesReachOnlyTheOwner()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var bridge = CubasePairRegistry.RegisterBridge();
        byte[] stored = [1, 2, 3];
        bool busy = false;
        try
        {
            CubasePairRegistry.SetDocumentAccess(bridge, () => Tuple.Create(0, stored), bytes =>
            {
                if (busy)
                    return 7;
                stored = bytes;
                return 0;
            });
            Assert.Equal(2, CubasePairRegistry.ReadDocumentState(instrument).Item1);
            CubasePairRegistry.Pair(instrument, bridge);
            var read = CubasePairRegistry.ReadDocumentState(instrument);
            Assert.Equal(0, read.Item1);
            read.Item2[0] = 99;
            Assert.Equal(new byte[] { 1, 2, 3 }, stored);
            byte[] update = [4, 5];
            Assert.Equal(0, CubasePairRegistry.ApplyDocumentState(instrument, update));
            update[0] = 99;
            Assert.Equal(new byte[] { 4, 5 }, stored);
            busy = true;
            Assert.Equal(7, CubasePairRegistry.ApplyDocumentState(instrument, [8]));
            Assert.Equal(new byte[] { 4, 5 }, stored);
            CubasePairRegistry.Unregister(bridge);
            Assert.Equal(2, CubasePairRegistry.ReadDocumentState(instrument).Item1);
            Assert.Equal(2, CubasePairRegistry.ApplyDocumentState(instrument, [9]));
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(bridge);
        }
    }

    [Fact]
    public void ViewLeasesKeepTheSameOwnerAndAllowCloseRetryAfterEndpointRemoval()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var bridge = CubasePairRegistry.RegisterBridge();
        int references = 1, creates = 0, destroys = 0;
        bool rejectDestroy = true;
        var resized = new List<ulong>();
        var scaled = new List<(ulong View, double Factor)>();
        try
        {
            CubasePairRegistry.SetViewProvider(bridge, (parent, width, height) =>
            {
                references++;
                return Tuple.Create(0, (ulong)++creates);
            }, view =>
            {
                if (rejectDestroy)
                    return 4;
                references--;
                destroys++;
                return 0;
            }, (view, width, height) => { resized.Add(view); return 0; }, view => 0, () => references--,
                (view, factor) => { scaled.Add((view, factor)); return 0; });
            CubasePairRegistry.Pair(instrument, bridge);
            var first = CubasePairRegistry.CreatePairView(instrument, 1, 900, 600);
            var second = CubasePairRegistry.CreatePairView(bridge, 1, 900, 600);
            Assert.Equal(0, first.Item1);
            Assert.Equal(0, second.Item1);
            Assert.NotEqual(first.Item2, second.Item2);
            Assert.Equal(3, references);
            CubasePairRegistry.Unregister(bridge);
            Assert.Equal(2, references);
            Assert.Equal(2, CubasePairRegistry.CreatePairView(instrument, 1, 900, 600).Item1);
            Assert.Equal(0, CubasePairRegistry.ResizePairView(first.Item2, 1000, 700));
            Assert.Equal(new ulong[] { 1 }, resized);
            Assert.Equal(0, CubasePairRegistry.FocusPairView(second.Item2));
            Assert.Equal(1, CubasePairRegistry.ScalePairView(first.Item2, double.NaN));
            Assert.Equal(1, CubasePairRegistry.ScalePairView(first.Item2, 0));
            Assert.Equal(0, CubasePairRegistry.ScalePairView(first.Item2, 1.5));
            Assert.Equal(0, CubasePairRegistry.ScalePairView(second.Item2, 2));
            Assert.Equal(new[] { (1UL, 1.5), (2UL, 2d) }, scaled);
            Assert.Equal(4, CubasePairRegistry.DestroyPairView(first.Item2));
            Assert.Equal(2, references);
            rejectDestroy = false;
            Assert.Equal(0, CubasePairRegistry.DestroyPairView(first.Item2));
            Assert.Equal(0, CubasePairRegistry.DestroyPairView(second.Item2));
            Assert.Equal(0, references);
            Assert.Equal(2, destroys);
            Assert.Equal(2, CubasePairRegistry.DestroyPairView(first.Item2));
            Assert.Equal(2, CubasePairRegistry.ScalePairView(first.Item2, 1));
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(bridge);
        }
    }

    [Fact]
    public void RemovingOneOfFourPairsLeavesOtherRelationsIntact()
    {
        var pairs = Enumerable.Range(0, 4).Select(_ =>
            (Instrument: CubasePairRegistry.RegisterInstrument(), Bridge: CubasePairRegistry.RegisterBridge())).ToArray();
        try
        {
            foreach (var pair in pairs)
                CubasePairRegistry.Pair(pair.Instrument, pair.Bridge);
            CubasePairRegistry.Unregister(pairs[1].Bridge);
            for (int index = 0; index < pairs.Length; index++)
            {
                var state = CubasePairRegistry.GetState(pairs[index].Instrument);
                Assert.Equal(index == 1 ? CubasePairStatus.PeerMissing : CubasePairStatus.Paired, state.Status);
                Assert.Equal(pairs[index].Bridge, state.Peer);
                Assert.False(state.OwnsProjectOutput);
                if (index != 1)
                    Assert.Equal(pairs[index].Bridge, state.LogicalOwner);
            }
        }
        finally
        {
            foreach (var pair in pairs)
            {
                CubasePairRegistry.Unregister(pair.Instrument);
                CubasePairRegistry.Unregister(pair.Bridge);
            }
        }
    }

    [Fact]
    public void SeparateComponentLoadContextsShareOnlyTheBclRegistryContract()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var context = new AssemblyLoadContext("CubasePairRegistryTest", isCollectible: true);
        Guid bridge = Guid.Empty;
        try
        {
            var assembly = context.LoadFromAssemblyPath(typeof(CubasePairRegistry).Assembly.Location);
            var foreign = assembly.GetType(typeof(CubasePairRegistry).FullName!, throwOnError: true)!;
            Assert.NotSame(typeof(CubasePairRegistry), foreign);
            bridge = (Guid)foreign.GetMethod("RegisterBridge", BindingFlags.Public | BindingFlags.Static)!
                .Invoke(null, null)!;
            CubasePairRegistry.Pair(instrument, bridge);
            Assert.Equal(bridge, CubasePairRegistry.GetState(instrument).LogicalOwner);
            foreign.GetMethod("Unpair", BindingFlags.Public | BindingFlags.Static)!.Invoke(null, [bridge]);
            Assert.Equal(CubasePairStatus.Independent, CubasePairRegistry.GetState(instrument).Status);
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(bridge);
            context.Unload();
        }
    }
}
