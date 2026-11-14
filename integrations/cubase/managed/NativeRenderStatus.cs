using System.Collections.Generic;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Threading;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    static readonly Dictionary<ulong, (nint Callback, nint UserData)> sStatusCallbacks = new();

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int SetDocumentStatusCallback(ulong document, nint callback, nint userData)
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
                sStatusCallbacks.Remove(document);
            }
            else
            {
                sStatusCallbacks[document] = (callback, userData);
            }
            return 0;
        }
    }

    static unsafe CubaseRenderStatus? PollDocumentStatus(ulong document)
    {
        lock (sLifecycleGate)
        {
            if (!sStatusCallbacks.TryGetValue(document, out var registration))
            {
                return null;
            }
            int song = -1, preview = -1;
            long elapsed = 0;
            int result = ((delegate* unmanaged[Cdecl]<nint, int*, int*, long*, int>)registration.Callback)(
                registration.UserData, &song, &preview, &elapsed);
            return result == 0 ? new CubaseRenderStatus(song, preview, elapsed) : null;
        }
    }
}
