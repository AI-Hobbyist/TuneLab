using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Threading;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    static readonly Dictionary<ulong, (nint Callback, nint Owner)> sKeyPreviewCallbacks = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetDocumentKeyPreviewCallback(ulong document, nint callback, nint owner)
    {
        lock (sLifecycleGate)
        {
            if (!sDocuments.ContainsKey(document))
                return 2;
            if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
                return 4;
            if (callback == 0)
                sKeyPreviewCallbacks.Remove(document);
            else
                sKeyPreviewCallbacks[document] = (callback, owner);
            return 0;
        }
    }

    static unsafe void RequestDocumentKeyPreview(ulong document, int pitch, bool pressed)
    {
        lock (sLifecycleGate)
        {
            if (pressed && (pitch < 0 || pitch > 127))
                return;
            if (!sKeyPreviewCallbacks.TryGetValue(document, out var entry))
                return;
            try
            {
                ((delegate* unmanaged[Cdecl]<nint, int, int, void>)entry.Callback)(entry.Owner, pitch, pressed ? 1 : 0);
            }
            catch (Exception exception)
            {
                Trace.TraceError("Cubase key preview callback failed: {0}", exception);
            }
        }
    }
}
