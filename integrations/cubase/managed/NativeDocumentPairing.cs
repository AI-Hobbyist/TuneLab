using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    static readonly Dictionary<ulong, Guid> sDocumentPairEndpoints = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int SetDocumentPairEndpoint(ulong document, byte* endpoint)
    {
        if (endpoint == null)
            return 1;
        try
        {
            lock (sLifecycleGate)
            {
                if (!sDocuments.ContainsKey(document))
                    return 2;
                var id = new Guid(new ReadOnlySpan<byte>(endpoint, 16));
                CubasePairRegistry.GetState(id);
                sDocumentPairEndpoints[document] = id;
                if (CubasePairRegistry.GetState(id).LogicalOwner == id &&
                    sDocumentLyricStatuses.Remove(document, out var lyricStatus))
                    CubasePairRegistry.SetLyricStatus(id, lyricStatus);
                return 0;
            }
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase document pairing configuration failed: {0}", exception);
            return 5;
        }
    }

    static Guid? DocumentPairEndpoint(ulong document)
    {
        lock (sLifecycleGate)
            return sDocumentPairEndpoints.TryGetValue(document, out var endpoint) ? endpoint : null;
    }
}
