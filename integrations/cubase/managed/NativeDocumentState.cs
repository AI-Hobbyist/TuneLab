using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Threading;
using TuneLab.Hosting;
using TuneLab.Data;
using TuneLab.Extensions.Formats.TLP;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    // Access is serialized by the lifecycle gate. A project loaded before the
    // first editor is decoded here and activated only on its UI/data thread.
    static readonly Dictionary<ulong, byte[]> sPendingDocumentStates = new();
    static readonly Dictionary<ulong, Action<byte[]>> sDocumentCallbacks = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int SetDocumentStateCallback(ulong document, nint callback, nint userData)
    {
        lock (sLifecycleGate)
        {
            if (!sDocuments.TryGetValue(document, out var context))
            {
                return 2;
            }
            if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
            {
                return 4;
            }
            if (sDocumentCallbacks.Remove(document, out var previous))
            {
                context.StateChanged -= previous;
            }
            if (callback == 0)
            {
                return 0;
            }
            Action<byte[]> handler = bytes =>
            {
                try
                {
                    fixed (byte* pointer = bytes)
                    {
                        ((delegate* unmanaged[Cdecl]<nint, byte*, int, void>)callback)(userData, pointer, bytes.Length);
                    }
                }
                catch (Exception exception)
                {
                    Trace.TraceError("Cubase document state notification failed: {0}", exception);
                }
            };
            sDocumentCallbacks[document] = handler;
            context.StateChanged += handler;
            return 0;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ValidateProjectState(byte* state, int length)
    {
        if (state == null || length <= 0)
        {
            return 1;
        }
        try
        {
            CubaseProjectState.Decode(new ReadOnlySpan<byte>(state, length));
            return 0;
        }
        catch (NotSupportedException)
        {
            return 3;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase project state validation failed: {0}", exception);
            return 1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CopyDocumentState(ulong document, byte* destination, int capacity, int* required)
    {
        if (required == null || capacity < 0 || (destination == null && capacity != 0))
        {
            return 1;
        }
        *required = 0;
        lock (sLifecycleGate)
        {
            if (!sDocuments.TryGetValue(document, out var context))
            {
                return 2;
            }
            if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
            {
                return 4;
            }
            try
            {
                var bytes = sPendingDocumentStates.TryGetValue(document, out var pending)
                    ? pending : context.SaveState();
                *required = bytes.Length;
                if (destination == null)
                {
                    return 0;
                }
                if (capacity < bytes.Length)
                {
                    return 1;
                }
                bytes.CopyTo(new Span<byte>(destination, capacity));
                return 0;
            }
            catch (Exception exception)
            {
                Trace.TraceError("Cubase document state copy failed: {0}", exception);
                return 5;
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int RestoreDocumentState(ulong document, byte* state, int length)
    {
        if (length < 0 || (state == null && length != 0))
        {
            return 1;
        }
        lock (sLifecycleGate)
        {
            if (!sDocuments.TryGetValue(document, out var context))
            {
                return 2;
            }
            try
            {
                var bytes = length == 0 ? CubaseProjectState.Encode(new NativeProjectFile())
                    : new ReadOnlySpan<byte>(state, length).ToArray();
                CubaseProjectState.Decode(bytes);
                var views = sViews.Values.Where(view => view.Document == document).ToArray();
                if (views.Length == 0)
                {
                    sPendingDocumentStates[document] = bytes;
                    return 0;
                }
                if (!Dispatcher.UIThread.CheckAccess())
                {
                    return 4;
                }
                foreach (var view in views)
                {
                    view.Session.SetPart(null);
                }
                try
                {
                    context.RestoreState(bytes);
                    sPendingDocumentStates.Remove(document);
                }
                finally
                {
                    var part = context.Document.Project?.AllMidiParts().FirstOrDefault();
                    foreach (var view in views)
                    {
                        view.Session.SetPart(part);
                    }
                }
                return 0;
            }
            catch (NotSupportedException)
            {
                return 3;
            }
            catch (Exception exception)
            {
                Trace.TraceError("Cubase document state restore failed: {0}", exception);
                return 1;
            }
        }
    }
}
