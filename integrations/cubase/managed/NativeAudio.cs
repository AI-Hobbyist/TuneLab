using System;
using System.Collections.Concurrent;
using System.Diagnostics;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using System.Threading;
using TuneLab.Hosting;
using TuneLab.Audio;

namespace TuneLab.Cubase.Managed;

public static partial class NativeEntry
{
    sealed class AudioLease : IDisposable
    {
        readonly GCHandle mPin;
        internal AudioLease(CubaseRenderedAudio audio)
        {
            Audio = audio;
            mPin = GCHandle.Alloc(audio.Interleaved, GCHandleType.Pinned);
        }
        internal CubaseRenderedAudio Audio { get; }
        internal nint Address => mPin.AddrOfPinnedObject();
        public void Dispose() => mPin.Free();
    }

    static readonly ConcurrentDictionary<ulong, AudioLease> sAudio = new();
    static long sNextAudio;

    // Blocking synthesis entry: call only on a render worker, not the audio
    // callback. The returned immutable buffer remains pinned until Release.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int RenderSnapshot(byte* state, int length, int outputSampleRate,
        ulong requestHandle, ulong* audio)
        => RenderAudio(state, length, outputSampleRate, requestHandle, audio, previewPitch: null);

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int RenderPreview(byte* state, int length, int pitch, int outputSampleRate,
        ulong requestHandle, ulong* audio)
    {
        if (pitch is < 0 or > 127)
        {
            if (audio != null)
            {
                *audio = 0;
            }
            return 1;
        }
        return RenderAudio(state, length, outputSampleRate, requestHandle, audio, pitch);
    }

    static unsafe int RenderAudio(byte* state, int length, int outputSampleRate,
        ulong requestHandle, ulong* audio, int? previewPitch)
    {
        if (state == null || length <= 0 || outputSampleRate <= 0 || audio == null)
        {
            return 1;
        }
        *audio = 0;
        if (!sRenderRequests.TryGetValue(requestHandle, out var request))
        {
            return 2;
        }
        lock (request)
        {
            if (request.Released)
            {
                return 2;
            }
            if (request.Active)
            {
                return 7;
            }
            request.Active = true;
        }
        AudioLease? lease = null;
        try
        {
            request.Cancellation.Token.ThrowIfCancellationRequested();
            PluginServices.InitializeEngines();
            var snapshot = new ReadOnlySpan<byte>(state, length).ToArray();
            var rendered = previewPitch.HasValue
                ? CubaseSnapshotRenderer.RenderPreview(snapshot, previewPitch.Value,
                    TimeSpan.FromMinutes(5), request.Cancellation.Token)
                : CubaseSnapshotRenderer.Render(snapshot, TimeSpan.FromMinutes(5), request.Cancellation.Token);
            if (rendered.SampleRate != outputSampleRate)
            {
                rendered = new CubaseRenderedAudio(outputSampleRate, AudioUtils.Resample(
                    rendered.Interleaved, 2, rendered.SampleRate, outputSampleRate));
            }
            request.Cancellation.Token.ThrowIfCancellationRequested();
            lease = new AudioLease(rendered);
            ulong id = checked((ulong)Interlocked.Increment(ref sNextAudio));
            if (!sAudio.TryAdd(id, lease))
            {
                lease.Dispose();
                return 5;
            }
            *audio = id;
            return 0;
        }
        catch (OperationCanceledException)
        {
            lease?.Dispose();
            return 6;
        }
        catch (Exception exception)
        {
            lease?.Dispose();
            Trace.TraceError("Cubase snapshot render failed: {0}", exception);
            return 5;
        }
        finally
        {
            lock (request)
            {
                request.Active = false;
            }
        }
    }

    // The native owner must not release the lease while any audio callback is
    // reading this address. No callback into managed code is needed to mix it.
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int GetRenderedAudio(ulong audio, int* sampleRate, int* frames, float** samples)
    {
        if (sampleRate == null || frames == null || samples == null)
        {
            return 1;
        }
        *sampleRate = 0;
        *frames = 0;
        *samples = null;
        if (!sAudio.TryGetValue(audio, out var lease))
        {
            return 2;
        }
        *sampleRate = lease.Audio.SampleRate;
        *frames = lease.Audio.Interleaved.Length / 2;
        *samples = (float*)lease.Address;
        return 0;
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int ReleaseRenderedAudio(ulong audio)
    {
        if (!sAudio.TryRemove(audio, out var lease))
        {
            return 2;
        }
        lease.Dispose();
        return 0;
    }
}
