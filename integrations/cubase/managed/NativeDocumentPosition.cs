using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using Avalonia;
using Avalonia.Threading;
using TuneLab.Data;
using TuneLab.Data.Timing;
using TuneLab.Foundation;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int MapDocumentSourcePosition(ulong document, double seconds, double* quarter)
    {
        if (quarter == null || !double.IsFinite(seconds))
            return 1;
        *quarter = double.NaN;
        lock (sLifecycleGate)
        {
            if (!sDocuments.TryGetValue(document, out var context))
                return 2;
            if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
                return 4;
            try
            {
                if (sPendingDocumentStates.TryGetValue(document, out var state))
                {
                    var tempos = CubaseProjectState.Decode(state).Project.Tempos;
                    TempoMark[] marks = tempos.Count == 0 ? [new TempoMark(0, TempoManager.DefaultBpm)] :
                        tempos.Select(tempo => new TempoMark(tempo.Pos, tempo.Bpm)).ToArray();
                    *quarter = new TempoSnapshot(marks, MusicTheory.RESOLUTION).ToTick(seconds) / MusicTheory.RESOLUTION;
                }
                else if (context.Document.Project is { } project)
                    *quarter = project.TempoManager.GetTick(seconds) / MusicTheory.RESOLUTION;
                else
                    return 2;
                return 0;
            }
            catch (Exception exception)
            {
                Trace.TraceError("Cubase source position mapping failed: {0}", exception);
                return 5;
            }
        }
    }
}
