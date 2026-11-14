using System;
using System.Collections.Generic;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Text;
using Avalonia;
using Avalonia.Threading;
using TuneLab.Hosting;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int NotifyPairSourceChange(byte* endpoint, byte* before, int beforeLength,
        byte* after, int afterLength)
    {
        if (endpoint == null || beforeLength < 0 || afterLength < 0 ||
            (before == null && beforeLength != 0) || (after == null && afterLength != 0))
            return 1;
        try
        {
            var previous = beforeLength == 0 ? null : CubaseProjectState.DecodeEnvelope(new ReadOnlySpan<byte>(before, beforeLength));
            var next = afterLength == 0 ? null : CubaseProjectState.DecodeEnvelope(new ReadOnlySpan<byte>(after, afterLength));
            if (!CubaseAraLyrics.SameSourceAssociation(previous, next))
                CubasePairRegistry.NotifySourceAssociationChanged(new Guid(new ReadOnlySpan<byte>(endpoint, 16)));
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase source association notification failed: {0}", exception);
            return 5;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int InvalidatePairSource(byte* endpoint)
    {
        if (endpoint == null)
            return 1;
        try
        {
            CubasePairRegistry.NotifySourceAssociationChanged(new Guid(new ReadOnlySpan<byte>(endpoint, 16)));
            return 0;
        }
        catch (KeyNotFoundException) { return 2; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase source identity invalidation failed: {0}", exception);
            return 5;
        }
    }

    static readonly Dictionary<ulong, CubaseLyricStatus> sDocumentLyricStatuses = new();

    static void StoreDocumentLyricStatus(ulong document, CubaseLyricStatus status)
    {
        if (sPairDocuments.TryGetValue(document, out var proxyEndpoint))
        {
            CubasePairRegistry.SetLyricStatus(proxyEndpoint, status);
            return;
        }
        if (sDocumentPairEndpoints.TryGetValue(document, out var endpoint) &&
            CubasePairRegistry.SetLyricStatus(endpoint, status))
            return;
        sDocumentLyricStatuses[document] = status;
    }

    static CubaseLyricStatus ReadDocumentLyricStatus(ulong document)
    {
        if (sPairDocuments.TryGetValue(document, out var proxyEndpoint))
            return CubasePairRegistry.GetLyricStatus(proxyEndpoint) ?? new(-1);
        if (sDocumentPairEndpoints.TryGetValue(document, out var endpoint))
        {
            if (CubasePairRegistry.GetLyricStatus(endpoint) is { } pairedStatus)
                return pairedStatus;
        }
        return sDocumentLyricStatuses.TryGetValue(document, out var status) ? status : new(-1);
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ReportDocumentLyricStatus(ulong document, int kind)
    {
        if (kind < 3 || kind > 7)
            return 1;
        lock (sLifecycleGate)
        {
            if (!sDocuments.ContainsKey(document) && !sPairDocuments.ContainsKey(document))
                return 2;
            if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
                return 4;
            try
            {
                StoreDocumentLyricStatus(document, new(kind));
                return 0;
            }
            catch (KeyNotFoundException) { return 2; }
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetDocumentLyricStatus(ulong document, int* kind, ulong* sourceNoteId)
    {
        if (kind == null || sourceNoteId == null)
            return 1;
        *kind = -1;
        *sourceNoteId = 0;
        lock (sLifecycleGate)
        {
            if (!sDocuments.ContainsKey(document) && !sPairDocuments.ContainsKey(document))
                return 2;
            if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
                return 4;
            try
            {
                var status = ReadDocumentLyricStatus(document);
                *kind = status.Kind;
                *sourceNoteId = status.SourceNoteId;
                return 0;
            }
            catch (KeyNotFoundException) { return 2; }
        }
    }

    // Private ABI17. disposition: 0 unchanged, 1 applied, 2 local conflict,
    // 3 no unique source note. No runtime VST3 noteId crosses this boundary.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int ApplyDocumentLyric(ulong document, double songQuarter, int pitch,
        char* text, int length, ulong* sourceNoteId, int* disposition)
    {
        if (sourceNoteId == null || disposition == null)
            return 1;
        *sourceNoteId = 0;
        *disposition = 3;
        if (length < 0 || length > 256 || (text == null && length != 0) ||
            !double.IsFinite(songQuarter) || pitch < 0 || pitch > 127)
            return 1;
        try
        {
            var characters = new ReadOnlySpan<char>(text, length);
            if (characters.Contains('\0'))
                return 1;
            new UnicodeEncoding(false, false, true).GetByteCount(characters);
            string lyric = characters.ToString();
            lock (sLifecycleGate)
            {
                if (sPairDocuments.TryGetValue(document, out var endpoint))
                {
                    var paired = CubasePairRegistry.ApplyDocumentLyric(endpoint, songQuarter, pitch, lyric);
                    *sourceNoteId = paired.Item2;
                    *disposition = paired.Item3;
                    if (paired.Item1 == 0)
                        StoreDocumentLyricStatus(document, new(paired.Item3, paired.Item2));
                    return paired.Item1;
                }
                if (!sDocuments.TryGetValue(document, out var context))
                    return 2;
                if (Application.Current != null && !Dispatcher.UIThread.CheckAccess())
                    return 4;
                if (context.Document.HasUncommittedChanges)
                {
                    StoreDocumentLyricStatus(document, new(5));
                    return 7;
                }
                bool deferred = sPendingDocumentStates.TryGetValue(document, out var pending);
                var state = deferred ? pending! : context.SaveState();
                var association = CubaseProjectState.DecodeEnvelope(state).Ara;
                if (association == null)
                {
                    StoreDocumentLyricStatus(document, new(3));
                    return 0;
                }
                ulong id = CubaseAraLyrics.ResolveSourceNote(state, association.SourceId, songQuarter, pitch);
                if (id == 0)
                {
                    StoreDocumentLyricStatus(document, new(3));
                    return 0;
                }
                *sourceNoteId = id;
                CubaseAraLyricUpdate[] updates = [new(id, lyric)];
                var result = deferred
                    ? CubaseAraLyrics.UpdateState(state, association.SourceId, updates)
                    : context.ApplyHostLyrics(association.SourceId, updates);
                *disposition = result.Conflicts.Length != 0 ? 2 : result.ChangedNotes.Length != 0 ? 1 : 0;
                StoreDocumentLyricStatus(document, new(*disposition, id));
                if (deferred && !state.AsSpan().SequenceEqual(result.State))
                {
                    sPendingDocumentStates[document] = result.State;
                    if (sDocumentCallbacks.TryGetValue(document, out var callback))
                        callback(result.State);
                }
                return 0;
            }
        }
        catch (KeyNotFoundException) { return 2; }
        catch (ArgumentException) { return 1; }
        catch (Exception exception)
        {
            Trace.TraceError("Cubase host lyric application failed: {0}", exception);
            return 5;
        }
    }
}
