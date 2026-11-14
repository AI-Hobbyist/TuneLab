using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Threading;
using TuneLab.Data;
using TuneLab.Hosting;
using System.Linq;

namespace TuneLab.Cubase.Managed;

// This assembly is private to the Cubase integration, outside the frozen SDK ABI.
public static partial class NativeEntry
{
    static readonly ConcurrentDictionary<ulong, CubaseDocumentContext> sDocuments = new();
    static long sNextHandle;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetAbiVersion()
    {
        return 1;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CreateDocument(ulong* handle)
    {
        if (handle == null)
        {
            return 1;
        }
        *handle = 0;
        CubaseDocumentContext? document = null;
        try
        {
            document = new CubaseDocumentContext();
            document.Document.SetProject(new Project());
            ulong id = checked((ulong)Interlocked.Increment(ref sNextHandle));
            if (!sDocuments.TryAdd(id, document))
            {
                document.Dispose();
                return 5;
            }
            *handle = id;
            return 0;
        }
        catch (Exception exception)
        {
            ReportCreationFailure(document, "document", exception);
            return 5;
        }
    }

    // Cleanup is also inside the ABI exception boundary: a failed Dispose must
    // not let a managed exception unwind through the native host.
    static void ReportCreationFailure(IDisposable? resource, string kind, Exception exception)
    {
        try
        {
            resource?.Dispose();
        }
        catch (Exception cleanupException)
        {
            exception = new AggregateException(exception, cleanupException);
        }
        Trace.TraceError("Cubase {0} creation failed: {1}", kind, exception);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyDocument(ulong handle)
    {
        lock (sLifecycleGate)
        {
            return DestroyDocumentCore(handle);
        }
    }

    static int DestroyDocumentCore(ulong handle)
    {
        if (sViews.Values.Any(view => view.Document == handle))
        {
            return 1;
        }
        if (!sDocuments.TryRemove(handle, out var document))
        {
            return 2;
        }
        try
        {
            document.Dispose();
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase document disposal failed: {0}", exception);
            return 5;
        }
    }
}
