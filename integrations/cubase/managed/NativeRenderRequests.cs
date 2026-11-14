using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    sealed class RenderRequest
    {
        internal readonly CancellationTokenSource Cancellation = new();
        internal bool Active;
        internal bool Released;
    }

    static readonly ConcurrentDictionary<ulong, RenderRequest> sRenderRequests = new();
    static long sNextRenderRequest;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CreateRenderRequest(ulong* handle)
    {
        if (handle == null)
        {
            return 1;
        }
        *handle = 0;
        try
        {
            ulong id = checked((ulong)Interlocked.Increment(ref sNextRenderRequest));
            var request = new RenderRequest();
            if (!sRenderRequests.TryAdd(id, request))
            {
                request.Cancellation.Dispose();
                return 5;
            }
            *handle = id;
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase render request creation failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int CancelRenderRequest(ulong handle)
    {
        if (!sRenderRequests.TryGetValue(handle, out var request))
        {
            return 2;
        }
        lock (request)
        {
            if (request.Released)
            {
                return 2;
            }
            request.Cancellation.Cancel();
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ReleaseRenderRequest(ulong handle)
    {
        if (!sRenderRequests.TryGetValue(handle, out var request))
        {
            return 2;
        }
        lock (request)
        {
            if (request.Released)
            {
                return 2;
            }
            if (request.Active)
            {
                return 7;
            }
            request.Released = true;
            sRenderRequests.TryRemove(handle, out _);
            request.Cancellation.Dispose();
            return 0;
        }
    }
}
