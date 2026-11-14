using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    // Private ABI: GUID buffers are exactly 16 bytes in Guid.TryWriteBytes order.
    // These control-thread calls never run from the VST3 process callback.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int RegisterPairEndpoint(int role, byte* endpoint)
    {
        if (endpoint == null || (role != 1 && role != 2))
            return 1;
        try
        {
            var id = role == 1 ? CubasePairRegistry.RegisterInstrument() : CubasePairRegistry.RegisterBridge();
            id.TryWriteBytes(new Span<byte>(endpoint, 16));
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase pair registration failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int SetPairOutputCallback(byte* endpoint, nint callback, nint owner)
    {
        if (endpoint == null)
            return 1;
        try
        {
            Action<bool>? handler = callback == 0 ? null : output =>
                ((delegate* unmanaged[Cdecl]<nint, int, void>)callback)(owner, output ? 1 : 0);
            CubasePairRegistry.SetOutputCallback(new Guid(new ReadOnlySpan<byte>(endpoint, 16)), handler);
            return 0;
        }
        catch (KeyNotFoundException)
        {
            return 2;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase pair output callback registration failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int SetPairDocumentAccess(byte* endpoint, nint copy, nint apply, nint owner)
    {
        if (endpoint == null || ((copy == 0) != (apply == 0)))
            return 1;
        try
        {
            Func<Tuple<int, byte[]>>? read = copy == 0 ? null : () =>
            {
                int required = 0;
                int result = ((delegate* unmanaged[Cdecl]<nint, byte*, int, int*, int>)copy)(owner, null, 0, &required);
                if (result != 0 || required <= 0)
                    return Tuple.Create(result == 0 ? 1 : result, Array.Empty<byte>());
                var bytes = new byte[required];
                fixed (byte* destination = bytes)
                    result = ((delegate* unmanaged[Cdecl]<nint, byte*, int, int*, int>)copy)(owner, destination, bytes.Length, &required);
                return Tuple.Create(result, bytes);
            };
            Func<byte[], int>? write = apply == 0 ? null : bytes =>
            {
                fixed (byte* source = bytes)
                    return ((delegate* unmanaged[Cdecl]<nint, byte*, int, int>)apply)(owner, source, bytes.Length);
            };
            CubasePairRegistry.SetDocumentAccess(new Guid(new ReadOnlySpan<byte>(endpoint, 16)), read, write);
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (ArgumentException) { return 1; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase pair document registration failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyPairDocumentState(byte* endpoint, byte* destination, int capacity, int* required)
    {
        if (endpoint == null || required == null || capacity < 0 || (destination == null && capacity != 0))
            return 1;
        *required = 0;
        try
        {
            var result = CubasePairRegistry.ReadDocumentState(new Guid(new ReadOnlySpan<byte>(endpoint, 16)));
            if (result.Item1 != 0)
                return result.Item1;
            *required = result.Item2.Length;
            if (destination == null)
                return 0;
            if (capacity < result.Item2.Length)
                return 1;
            result.Item2.CopyTo(new Span<byte>(destination, capacity));
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase shared document read failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ApplyPairDocumentState(byte* endpoint, byte* state, int length)
    {
        if (endpoint == null || state == null || length <= 0)
            return 1;
        try
        {
            return CubasePairRegistry.ApplyDocumentState(new Guid(new ReadOnlySpan<byte>(endpoint, 16)),
                new ReadOnlySpan<byte>(state, length).ToArray());
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase shared document write failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int PairEndpoints(byte* instrument, byte* bridge)
    {
        if (instrument == null || bridge == null)
            return 1;
        try
        {
            CubasePairRegistry.Pair(new Guid(new ReadOnlySpan<byte>(instrument, 16)),
                new Guid(new ReadOnlySpan<byte>(bridge, 16)));
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (ArgumentException) { return 1; }
        catch (InvalidOperationException) { return 7; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase explicit pairing failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetPairEndpointState(byte* endpoint, byte* owner, int* status, int* projectOutput)
    {
        if (endpoint == null || owner == null || status == null || projectOutput == null)
            return 1;
        try
        {
            var state = CubasePairRegistry.GetState(new Guid(new ReadOnlySpan<byte>(endpoint, 16)));
            (state.LogicalOwner ?? Guid.Empty).TryWriteBytes(new Span<byte>(owner, 16));
            *status = (int)state.Status;
            *projectOutput = state.OwnsProjectOutput ? 1 : 0;
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase pair state query failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int RestorePairIntent(byte* endpoint, int requiresPair)
    {
        if (endpoint == null || requiresPair < 0 || requiresPair > 1)
            return 1;
        try
        {
            CubasePairRegistry.RestoreInstrumentIntent(new Guid(new ReadOnlySpan<byte>(endpoint, 16)), requiresPair != 0);
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (ArgumentException) { return 1; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase pairing intent restore failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int UnpairEndpoint(byte* endpoint)
    {
        if (endpoint == null)
            return 1;
        try
        {
            CubasePairRegistry.Unpair(new Guid(new ReadOnlySpan<byte>(endpoint, 16)));
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase unlink failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ReleasePairEndpoint(byte* endpoint)
    {
        if (endpoint == null)
            return 1;
        try
        {
            CubasePairRegistry.Unregister(new Guid(new ReadOnlySpan<byte>(endpoint, 16)));
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase pair endpoint release failed: {0}", exception);
            return 5;
        }
    }
}
