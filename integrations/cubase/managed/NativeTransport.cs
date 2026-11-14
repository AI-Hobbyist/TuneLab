using System;
using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Threading;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    static readonly Dictionary<ulong, (nint Callback, nint UserData)> sPositionCallbacks = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetDocumentPositionCallback(ulong document, nint callback, nint userData)
    {
        lock (sLifecycleGate)
        {
            if (!sDocuments.ContainsKey(document))
            {
                return 2;
            }
            if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
            {
                return 4;
            }
            if (callback == 0)
            {
                sPositionCallbacks.Remove(document);
            }
            else
            {
                sPositionCallbacks[document] = (callback, userData);
            }
            return 0;
        }
    }

    static unsafe double PollDocumentPosition(ulong document)
    {
        lock (sLifecycleGate)
        {
            if (!sPositionCallbacks.TryGetValue(document, out var registration))
            {
                return double.NaN;
            }
            return ((delegate* unmanaged[Cdecl]<nint, double>)registration.Callback)(registration.UserData);
        }
    }
}
