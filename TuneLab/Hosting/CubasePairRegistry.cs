using System;
using System.Collections.Generic;
using PairDocumentAccess = System.Tuple<
    System.Func<System.Tuple<int, byte[]>>,
    System.Func<byte[], int>,
    System.Func<double, int, string, System.Tuple<int, ulong, int>>?>;

namespace TuneLab.Hosting;

internal enum CubasePairRole : byte
{
    Instrument = 1,
    Bridge = 2
}

internal enum CubasePairStatus
{
    Independent,
    Paired,
    PeerMissing
}

internal readonly record struct CubasePairState(CubasePairRole Role, CubasePairStatus Status,
    Guid Peer, Guid? LogicalOwner, bool OwnsProjectOutput);

// Control-thread registry only. Separate plugin bundles have separate component
// load contexts. Store only BCL types in the AppDomain slot so both contexts
// operate on the same registry without exchanging TuneLab assembly types.
internal static partial class CubasePairRegistry
{
    const string RegistryKey = "org.tunelab.cubase.explicit-pairs.v1";
    static readonly Dictionary<Guid, Tuple<byte, Guid>> sEndpoints = SharedEndpoints();
    static readonly Dictionary<Guid, Action<bool>> sOutputCallbacks = SharedOutputCallbacks();
    static readonly Dictionary<Guid, PairDocumentAccess> sDocumentAccess = SharedDocumentAccess();

    static Dictionary<Guid, PairDocumentAccess> SharedDocumentAccess()
    {
        lock (string.Intern(RegistryKey))
        {
            const string key = RegistryKey + ".document-access.v2";
            if (AppDomain.CurrentDomain.GetData(key) is { } existing)
                return (Dictionary<Guid, PairDocumentAccess>)existing;
            var access = new Dictionary<Guid, PairDocumentAccess>();
            AppDomain.CurrentDomain.SetData(key, access);
            return access;
        }
    }

    public static void SetDocumentAccess(Guid bridge, Func<Tuple<int, byte[]>>? read, Func<byte[], int>? apply,
        Func<double, int, string, Tuple<int, ulong, int>>? lyrics = null)
    {
        lock (sEndpoints)
        {
            Require(bridge, CubasePairRole.Bridge);
            if (read == null && apply == null)
                sDocumentAccess.Remove(bridge);
            else if (read != null && apply != null)
                sDocumentAccess[bridge] = new(read, apply, lyrics);
            else
                throw new ArgumentException("Document access requires both callbacks.");
        }
    }

    public static Tuple<int, byte[]> ReadDocumentState(Guid endpoint)
    {
        lock (sEndpoints)
        {
            var owner = GetState(endpoint).LogicalOwner;
            if (owner == null || !sDocumentAccess.TryGetValue(owner.Value, out var access))
                return Tuple.Create(2, Array.Empty<byte>());
            // Keep the owner registered for the duration of the synchronous
            // control-thread call; never return its mutable backing array.
            var result = access.Item1();
            return Tuple.Create(result.Item1, (byte[])result.Item2.Clone());
        }
    }

    public static int ApplyDocumentState(Guid endpoint, byte[] state)
    {
        lock (sEndpoints)
        {
            var owner = GetState(endpoint).LogicalOwner;
            if (owner == null || !sDocumentAccess.TryGetValue(owner.Value, out var access))
                return 2;
            return access.Item2((byte[])state.Clone());
        }
    }

    public static Tuple<int, ulong, int> ApplyDocumentLyric(Guid endpoint, double quarter, int pitch, string text)
    {
        lock (sEndpoints)
        {
            var owner = GetState(endpoint).LogicalOwner;
            if (owner == null || !sDocumentAccess.TryGetValue(owner.Value, out var access) || access.Item3 == null)
                return Tuple.Create(2, 0UL, 3);
            return access.Item3(quarter, pitch, text);
        }
    }


    static Dictionary<Guid, Action<bool>> SharedOutputCallbacks()
    {
        lock (string.Intern(RegistryKey))
        {
            const string key = RegistryKey + ".output-callbacks";
            if (AppDomain.CurrentDomain.GetData(key) is { } existing)
                return (Dictionary<Guid, Action<bool>>)existing;
            var callbacks = new Dictionary<Guid, Action<bool>>();
            AppDomain.CurrentDomain.SetData(key, callbacks);
            return callbacks;
        }
    }

    public static void SetOutputCallback(Guid id, Action<bool>? callback)
    {
        lock (sEndpoints)
        {
            if (!sEndpoints.ContainsKey(id))
                throw new KeyNotFoundException("The pairing endpoint is no longer available.");
            if (callback == null)
                sOutputCallbacks.Remove(id);
            else
            {
                sOutputCallbacks[id] = callback;
                PublishOutput(id);
            }
        }
    }

    static void PublishOutput(Guid id)
    {
        if (sOutputCallbacks.TryGetValue(id, out var callback))
            callback(GetState(id).OwnsProjectOutput);
    }

    public static void NotifySourceAssociationChanged(Guid bridge)
    {
        lock (sEndpoints)
        {
            Require(bridge, CubasePairRole.Bridge);
            // Output ownership is unchanged. Native ingress advances the
            // revision through the existing synchronous notification channel.
            PublishOutput(bridge);
            var peer = sEndpoints[bridge].Item2;
            if (sEndpoints.ContainsKey(peer))
                PublishOutput(peer);
        }
    }


    static Dictionary<Guid, Tuple<byte, Guid>> SharedEndpoints()
    {
        lock (string.Intern(RegistryKey))
        {
            var domain = AppDomain.CurrentDomain;
            if (domain.GetData(RegistryKey) is { } existing)
                return (Dictionary<Guid, Tuple<byte, Guid>>)existing;
            var endpoints = new Dictionary<Guid, Tuple<byte, Guid>>();
            domain.SetData(RegistryKey, endpoints);
            return endpoints;
        }
    }

    public static Guid RegisterInstrument() => Register(CubasePairRole.Instrument);
    public static Guid RegisterBridge() => Register(CubasePairRole.Bridge);

    static Guid Register(CubasePairRole role)
    {
        lock (sEndpoints)
        {
            var id = Guid.NewGuid();
            sEndpoints.Add(id, Tuple.Create((byte)role, Guid.Empty));
            return id;
        }
    }

    // Runtime tokens must be explicitly selected. Registration, restoration or
    // discovery never chooses a peer by name, persistent ID or load order.
    public static void Pair(Guid instrument, Guid bridge)
    {
        lock (sEndpoints)
        {
            var instrumentEntry = Require(instrument, CubasePairRole.Instrument);
            var bridgeEntry = Require(bridge, CubasePairRole.Bridge);
            RequireAvailable(instrumentEntry, bridge);
            RequireAvailable(bridgeEntry, instrument);
            sEndpoints[instrument] = Tuple.Create(instrumentEntry.Item1, bridge);
            sEndpoints[bridge] = Tuple.Create(bridgeEntry.Item1, instrument);
            PublishOutput(instrument);
            PublishOutput(bridge);
        }
    }

    static Tuple<byte, Guid> Require(Guid id, CubasePairRole role)
    {
        if (!sEndpoints.TryGetValue(id, out var entry))
            throw new KeyNotFoundException("The pairing endpoint is no longer available.");
        if (entry.Item1 != (byte)role)
            throw new ArgumentException("Pairing requires one instrument and one bridge.");
        return entry;
    }

    static void RequireAvailable(Tuple<byte, Guid> entry, Guid requestedPeer)
    {
        if (entry.Item2 != Guid.Empty && entry.Item2 != requestedPeer && sEndpoints.ContainsKey(entry.Item2))
            throw new InvalidOperationException("The pairing endpoint already has a live peer.");
    }

    public static CubasePairState GetState(Guid id)
    {
        lock (sEndpoints)
        {
            if (!sEndpoints.TryGetValue(id, out var entry))
                throw new KeyNotFoundException("The pairing endpoint is no longer available.");
            var role = (CubasePairRole)entry.Item1;
            var peer = entry.Item2;
            bool connected = peer != Guid.Empty && sEndpoints.TryGetValue(peer, out var other) && other.Item2 == id;
            var status = peer == Guid.Empty ? CubasePairStatus.Independent :
                connected ? CubasePairStatus.Paired : CubasePairStatus.PeerMissing;
            Guid? owner = role == CubasePairRole.Bridge || status == CubasePairStatus.Independent ? id :
                connected ? peer : null;
            // The bridge owns the region geometry and is the formal output in
            // paired mode. Losing it requires explicit unlink/rebind, not an
            // automatic switch to a possibly stale instrument score.
            return new(role, status, peer, owner,
                role == CubasePairRole.Bridge || status == CubasePairStatus.Independent);
        }
    }

    // Restored state preserves output ownership intent, not a process token.
    // Without a host project identity, even a live former peer must be selected
    // explicitly again. Publish only the final state, never a temporary owner.
    public static void RestoreInstrumentIntent(Guid id, bool requiresPair)
    {
        lock (sEndpoints)
        {
            var entry = Require(id, CubasePairRole.Instrument);
            bool detachPeer = sEndpoints.TryGetValue(entry.Item2, out var peer) && peer.Item2 == id;
            if (detachPeer)
                sEndpoints[entry.Item2] = Tuple.Create(peer!.Item1, Guid.Empty);
            sEndpoints[id] = Tuple.Create(entry.Item1, requiresPair ? Guid.NewGuid() : Guid.Empty);
            PublishOutput(id);
            if (detachPeer)
                PublishOutput(entry.Item2);
        }
    }

    public static void Unpair(Guid id)
    {
        lock (sEndpoints)
        {
            if (!sEndpoints.TryGetValue(id, out var entry))
                throw new KeyNotFoundException("The pairing endpoint is no longer available.");
            if (sEndpoints.TryGetValue(entry.Item2, out var peer) && peer.Item2 == id)
            {
                sEndpoints[entry.Item2] = Tuple.Create(peer.Item1, Guid.Empty);
                PublishOutput(entry.Item2);
            }
            sEndpoints[id] = Tuple.Create(entry.Item1, Guid.Empty);
            PublishOutput(id);
        }
    }

    public static void Unregister(Guid id)
    {
        lock (sEndpoints)
        {
            // Keep the surviving endpoint's explicit relation as PeerMissing.
            // A new instance always has a new token, even after copying a track.
            if (!sEndpoints.Remove(id, out var entry))
                return;
            sOutputCallbacks.Remove(id);
            sDocumentAccess.Remove(id);
            sLyricStatuses.Remove(id);
            RemoveViewProvider(id);
            if (sEndpoints.ContainsKey(entry.Item2))
                PublishOutput(entry.Item2);
        }
    }
}
