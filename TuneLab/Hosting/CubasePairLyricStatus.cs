using System;
using System.Collections.Generic;

namespace TuneLab.Hosting;

internal static partial class CubasePairRegistry
{
    // Only BCL types cross the two plugin AssemblyLoadContexts.
    static readonly Dictionary<Guid, Tuple<int, ulong>> sLyricStatuses = SharedLyricStatuses();

    static Dictionary<Guid, Tuple<int, ulong>> SharedLyricStatuses()
    {
        lock (string.Intern(RegistryKey))
        {
            const string key = RegistryKey + ".lyric-status.v1";
            if (AppDomain.CurrentDomain.GetData(key) is { } existing)
                return (Dictionary<Guid, Tuple<int, ulong>>)existing;
            var statuses = new Dictionary<Guid, Tuple<int, ulong>>();
            AppDomain.CurrentDomain.SetData(key, statuses);
            return statuses;
        }
    }

    public static bool SetLyricStatus(Guid endpoint, CubaseLyricStatus status)
    {
        lock (sEndpoints)
        {
            if (!sEndpoints.ContainsKey(endpoint))
                return false;
            var owner = GetState(endpoint).LogicalOwner;
            if (owner == null)
                return false;
            sLyricStatuses[owner.Value] = Tuple.Create(status.Kind, status.SourceNoteId);
            return true;
        }
    }

    public static CubaseLyricStatus? GetLyricStatus(Guid endpoint)
    {
        lock (sEndpoints)
        {
            if (!sEndpoints.ContainsKey(endpoint))
                return null;
            var owner = GetState(endpoint).LogicalOwner;
            return owner != null && sLyricStatuses.TryGetValue(owner.Value, out var status)
                ? new CubaseLyricStatus(status.Item1, status.Item2) : null;
        }
    }
}
