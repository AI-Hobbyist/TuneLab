using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using Avalonia.Threading;
using TuneLab.Data;
using TuneLab.Foundation;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    sealed record ViewRegistration(ulong Document, PianoEditorSession Session);
    static readonly ConcurrentDictionary<ulong, ViewRegistration> sViews = new();
    static readonly object sLifecycleGate = new();
    static long sNextView;

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int CreateView(ulong document, nint parent, int width, int height, ulong* view)
    {
        lock (sLifecycleGate)
        {
            return CreateViewCore(document, parent, width, height, view);
        }
    }

    static unsafe int CreateViewCore(ulong document, nint parent, int width, int height, ulong* view)
    {
        if (view == null || parent == 0 || width <= 0 || height <= 0)
        {
            return 1;
        }
        *view = 0;
        if (sPairDocuments.TryGetValue(document, out var endpoint))
            return CreatePairProxyView(document, endpoint, parent, width, height, view);
        if (!sDocuments.TryGetValue(document, out var context))
        {
            return 2;
        }
        PianoEditorSession? session = null;
        try
        {
            PluginUiRuntime.Initialize();
            if (sPendingDocumentStates.TryGetValue(document, out var pendingState))
            {
                context.RestoreState(pendingState);
                sPendingDocumentStates.Remove(document);
            }
            var playhead = new EmbeddedPlayhead();
            session = new PianoEditorSession(context.Document, playhead, () =>
            {
                double quarterNotes = PollDocumentPosition(document);
                if (double.IsFinite(quarterNotes))
                {
                    playhead.Pos = quarterNotes * MusicTheory.RESOLUTION;
                }
            }, () => PollDocumentStatus(document),
                pitch => RequestDocumentKeyPreview(document, pitch, true),
                () => RequestDocumentKeyPreview(document, -1, false),
                () => DocumentPairEndpoint(document), () => ReadDocumentLyricStatus(document));
            session.SetPart(context.GetOrCreateEditingPart());
            session.Attach(parent, width, height);
            ulong id = checked((ulong)Interlocked.Increment(ref sNextView));
            if (!sViews.TryAdd(id, new ViewRegistration(document, session)))
            {
                session.Dispose();
                return 5;
            }
            *view = id;
            return 0;
        }
        catch (Exception exception)
        {
            ReportCreationFailure(session, "view", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int DestroyView(ulong view)
    {
        lock (sLifecycleGate)
        {
            return DestroyViewCore(view);
        }
    }

    static int DestroyViewCore(ulong view)
    {
        if (sPairViews.ContainsKey(view))
            return PairProxyViewOperation(view, CubasePairRegistry.DestroyPairView, remove: true);
        if (!Dispatcher.UIThread.CheckAccess())
        {
            return 4;
        }
        if (!sViews.TryGetValue(view, out var registration))
        {
            return 2;
        }
        try
        {
            registration.Session.Dispose();
            sViews.TryRemove(view, out _);
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase view disposal failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ResizeView(ulong view, int width, int height)
    {
        if (width <= 0 || height <= 0)
        {
            return 1;
        }
        lock (sLifecycleGate)
        {
            if (sPairViews.ContainsKey(view))
                return PairProxyViewOperation(view, lease => CubasePairRegistry.ResizePairView(lease, width, height));
            if (!Dispatcher.UIThread.CheckAccess())
                return 4;
            if (!sViews.TryGetValue(view, out var registration))
            {
                return 2;
            }
            try
            {
                registration.Session.Resize(width, height);
                return 0;
            }
            catch (Exception exception)
            {
                Trace.TraceError("Cubase view resize failed: {0}", exception);
                return 5;
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int FocusView(ulong view)
    {
        lock (sLifecycleGate)
        {
            if (sPairViews.ContainsKey(view))
                return PairProxyViewOperation(view, CubasePairRegistry.FocusPairView);
            if (!Dispatcher.UIThread.CheckAccess())
                return 4;
            if (!sViews.TryGetValue(view, out var registration))
            {
                return 2;
            }
            try
            {
                return registration.Session.Focus() ? 0 : 5;
            }
            catch (Exception exception)
            {
                Trace.TraceError("Cubase view focus failed: {0}", exception);
                return 5;
            }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ScaleView(ulong view, double factor)
    {
        if (!double.IsFinite(factor) || factor <= 0)
            return 1;
        lock (sLifecycleGate)
        {
            if (sPairViews.ContainsKey(view))
                return PairProxyViewOperation(view, lease => CubasePairRegistry.ScalePairView(lease, factor));
            if (!Dispatcher.UIThread.CheckAccess())
                return 4;
            if (!sViews.TryGetValue(view, out var registration))
                return 2;
            try
            {
                registration.Session.SetScale(factor);
                return 0;
            }
            catch (Exception exception)
            {
                Trace.TraceError("Cubase view scaling failed: {0}", exception);
                return 5;
            }
        }
    }

    sealed class EmbeddedPlayhead : IPlayhead
    {
        readonly ActionEvent mChanged = new();
        double mPos;
        public IActionEvent PosChanged => mChanged;
        public double Pos
        {
            get => mPos;
            set
            {
                if (mPos == value)
                {
                    return;
                }
                mPos = value;
                mChanged.Invoke();
            }
        }
    }
}
