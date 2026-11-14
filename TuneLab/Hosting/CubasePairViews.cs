using System;
using System.Collections.Generic;
using PairViewProvider = System.Tuple<System.Func<nint, int, int, System.Tuple<int, ulong>>,
    System.Func<ulong, int>, System.Func<ulong, int, int, int>, System.Func<ulong, int>, System.Action,
    System.Func<ulong, double, int>>;

namespace TuneLab.Hosting;

internal static partial class CubasePairRegistry
{
    // Providers own one native registration reference. Each successful create
    // owns another reference until destroy succeeds. Endpoint removal prevents
    // new views but existing leases can still close safely through the owner.
    static readonly Tuple<Dictionary<Guid, PairViewProvider>, Dictionary<Guid, Tuple<object, ulong>>>
        sPairViews = SharedViewStore();

    static Tuple<Dictionary<Guid, PairViewProvider>, Dictionary<Guid, Tuple<object, ulong>>> SharedViewStore()
    {
        lock (string.Intern(RegistryKey))
        {
            const string key = RegistryKey + ".view-leases.v2";
            if (AppDomain.CurrentDomain.GetData(key) is { } existing)
                return (Tuple<Dictionary<Guid, PairViewProvider>, Dictionary<Guid, Tuple<object, ulong>>>)existing;
            var store = Tuple.Create(new Dictionary<Guid, PairViewProvider>(),
                new Dictionary<Guid, Tuple<object, ulong>>());
            AppDomain.CurrentDomain.SetData(key, store);
            return store;
        }
    }

    public static void SetViewProvider(Guid bridge, Func<nint, int, int, Tuple<int, ulong>> create,
        Func<ulong, int> destroy, Func<ulong, int, int, int> resize, Func<ulong, int> focus, Action release,
        Func<ulong, double, int> scale)
    {
        lock (sEndpoints)
        {
            Require(bridge, CubasePairRole.Bridge);
            RemoveViewProvider(bridge);
            sPairViews.Item1.Add(bridge, Tuple.Create(create, destroy, resize, focus, release, scale));
        }
    }

    static void RemoveViewProvider(Guid bridge)
    {
        if (sPairViews.Item1.Remove(bridge, out var provider))
            provider.Item5();
    }

    public static Tuple<int, Guid> CreatePairView(Guid endpoint, nint parent, int width, int height)
    {
        if (parent == 0 || width <= 0 || height <= 0)
            return Tuple.Create(1, Guid.Empty);
        lock (sEndpoints)
        {
            var owner = GetState(endpoint).LogicalOwner;
            if (owner == null || !sPairViews.Item1.TryGetValue(owner.Value, out var provider))
                return Tuple.Create(2, Guid.Empty);
            var result = provider.Item1(parent, width, height);
            if (result.Item1 != 0)
                return Tuple.Create(result.Item1, Guid.Empty);
            if (result.Item2 == 0)
                return Tuple.Create(5, Guid.Empty);
            var lease = Guid.NewGuid();
            try
            {
                sPairViews.Item2.Add(lease, Tuple.Create((object)provider, result.Item2));
                return Tuple.Create(0, lease);
            }
            catch
            {
                provider.Item2(result.Item2);
                throw;
            }
        }
    }

    public static int DestroyPairView(Guid lease)
    {
        lock (sEndpoints)
        {
            if (!sPairViews.Item2.TryGetValue(lease, out var entry))
                return 2;
            var result = ((PairViewProvider)entry.Item1).Item2(entry.Item2);
            if (result == 0)
                sPairViews.Item2.Remove(lease);
            return result;
        }
    }

    public static int ResizePairView(Guid lease, int width, int height)
    {
        if (width <= 0 || height <= 0)
            return 1;
        lock (sEndpoints)
        {
            if (!sPairViews.Item2.TryGetValue(lease, out var entry))
                return 2;
            return ((PairViewProvider)entry.Item1).Item3(entry.Item2, width, height);
        }
    }

    public static int FocusPairView(Guid lease)
    {
        lock (sEndpoints)
        {
            if (!sPairViews.Item2.TryGetValue(lease, out var entry))
                return 2;
            return ((PairViewProvider)entry.Item1).Item4(entry.Item2);
        }
    }

    public static int ScalePairView(Guid lease, double factor)
    {
        if (!double.IsFinite(factor) || factor <= 0)
            return 1;
        lock (sEndpoints)
        {
            if (!sPairViews.Item2.TryGetValue(lease, out var entry))
                return 2;
            return ((PairViewProvider)entry.Item1).Item6(entry.Item2, factor);
        }
    }
}
