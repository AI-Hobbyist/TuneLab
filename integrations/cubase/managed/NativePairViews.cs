using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int SetPairViewProvider(byte* endpoint, nint create, nint destroy,
        nint resize, nint focus, nint release, nint owner)
    {
        if (endpoint == null || create == 0 || destroy == 0 || resize == 0 || focus == 0 || release == 0)
            return 1;
        try
        {
            CubasePairRegistry.SetViewProvider(new Guid(new ReadOnlySpan<byte>(endpoint, 16)),
                (parent, width, height) =>
                {
                    ulong view = 0;
                    int result = ((delegate* unmanaged[Cdecl]<nint, nint, int, int, ulong*, int>)create)
                        (owner, parent, width, height, &view);
                    return Tuple.Create(result, view);
                },
                view => ((delegate* unmanaged[Cdecl]<nint, ulong, int>)destroy)(owner, view),
                (view, width, height) => ((delegate* unmanaged[Cdecl]<nint, ulong, int, int, int>)resize)(owner, view, width, height),
                view => ((delegate* unmanaged[Cdecl]<nint, ulong, int>)focus)(owner, view),
                () => ((delegate* unmanaged[Cdecl]<nint, void>)release)(owner));
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (ArgumentException) { return 1; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase shared view registration failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CreatePairView(byte* endpoint, nint parent, int width, int height, byte* lease)
    {
        if (endpoint == null || lease == null)
            return 1;
        try
        {
            var result = CubasePairRegistry.CreatePairView(new Guid(new ReadOnlySpan<byte>(endpoint, 16)), parent, width, height);
            if (result.Item1 == 0)
                result.Item2.TryWriteBytes(new Span<byte>(lease, 16));
            return result.Item1;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase shared view creation failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int DestroyPairView(byte* lease)
        => PairViewOperation(lease, CubasePairRegistry.DestroyPairView);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int FocusPairView(byte* lease)
        => PairViewOperation(lease, CubasePairRegistry.FocusPairView);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ResizePairView(byte* lease, int width, int height)
        => PairViewOperation(lease, id => CubasePairRegistry.ResizePairView(id, width, height));

    static unsafe int PairViewOperation(byte* lease, Func<Guid, int> operation)
    {
        if (lease == null)
            return 1;
        try
        {
            return operation(new Guid(new ReadOnlySpan<byte>(lease, 16)));
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase shared view operation failed: {0}", exception);
            return 5;
        }
    }
}
