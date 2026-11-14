using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    // Local handles identify forwarding resources, never another ProjectDocument.
    // Ordinary document/view counters are positive signed longs, so the high bit
    // separates these namespaces without exposing foreign component handles.
    const ulong PairProxyBit = 1UL << 63;
    static long sNextPairProxy;
    static readonly Dictionary<ulong, Guid> sPairDocuments = new();
    sealed record PairViewRegistration(ulong Document, Guid Lease);
    static readonly Dictionary<ulong, PairViewRegistration> sPairViews = new();

    static ulong NextPairProxyHandle()
        => PairProxyBit | checked((ulong)Interlocked.Increment(ref sNextPairProxy));

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CreatePairDocumentProxy(byte* endpoint, ulong* document)
    {
        if (endpoint == null || document == null)
            return 1;
        *document = 0;
        try
        {
            lock (sLifecycleGate)
            {
                var id = new Guid(new ReadOnlySpan<byte>(endpoint, 16));
                var state = CubasePairRegistry.GetState(id);
                if (state.Status != CubasePairStatus.Paired || state.LogicalOwner == null)
                    return 2;
                ulong handle = NextPairProxyHandle();
                sPairDocuments.Add(handle, id);
                *document = handle;
                return 0;
            }
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase paired document proxy creation failed: {0}", exception);
            return 5;
        }
    }

    static unsafe int CreatePairProxyView(ulong document, Guid endpoint, nint parent,
        int width, int height, ulong* view)
    {
        try
        {
            var result = CubasePairRegistry.CreatePairView(endpoint, parent, width, height);
            if (result.Item1 != 0)
                return result.Item1;
            try
            {
                ulong handle = NextPairProxyHandle();
                sPairViews.Add(handle, new PairViewRegistration(document, result.Item2));
                *view = handle;
                return 0;
            }
            catch
            {
                CubasePairRegistry.DestroyPairView(result.Item2);
                throw;
            }
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase paired view proxy creation failed: {0}", exception);
            return 5;
        }
    }

    static int PairProxyViewOperation(ulong view, Func<Guid, int> operation, bool remove = false)
    {
        try
        {
            int result = operation(sPairViews[view].Lease);
            if (result == 0 && remove)
                sPairViews.Remove(view);
            return result;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase paired view proxy operation failed: {0}", exception);
            return 5;
        }
    }
}
