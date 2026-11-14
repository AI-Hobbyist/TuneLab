using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    sealed record RenderSessionRegistration(int Thread, CubaseRenderSession Session);
    static readonly ConcurrentDictionary<ulong, RenderSessionRegistration> sRenderSessions = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CreateRenderSession(ulong* handle)
    {
        if (handle == null)
            return 1;
        *handle = 0;
        try
        {
            var session = new CubaseRenderSession();
            ulong id = checked((ulong)Interlocked.Increment(ref sNextHandle));
            if (!sRenderSessions.TryAdd(id, new(Environment.CurrentManagedThreadId, session)))
            {
                session.Dispose();
                return 5;
            }
            *handle = id;
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase render session creation failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyRenderSession(ulong handle)
    {
        if (!sRenderSessions.TryGetValue(handle, out var registration))
            return 2;
        if (registration.Thread != Environment.CurrentManagedThreadId)
            return 4;
        try
        {
            registration.Session.Dispose();
            sRenderSessions.TryRemove(handle, out _);
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase render session destruction failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int RenderSessionSnapshot(ulong handle, byte* state, int length,
        int sampleRate, ulong request, ulong* audio)
    {
        if (audio == null)
            return 1;
        *audio = 0;
        if (!sRenderSessions.TryGetValue(handle, out var registration))
            return 2;
        if (registration.Thread != Environment.CurrentManagedThreadId)
            return 4;
        return RenderAudio(state, length, sampleRate, request, audio, null, registration.Session);
    }
}
