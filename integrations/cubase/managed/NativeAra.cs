using System;
using System.Diagnostics;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using TuneLab.Hosting;
using TuneLab.SDK;

namespace TuneLab.Cubase.Managed;

// Private integration ABI 14; these types are not part of TuneLab.SDK.
[StructLayout(LayoutKind.Sequential)]
public struct AraTempoInput
{
    public double Seconds;
    public double Quarter;
}

[StructLayout(LayoutKind.Sequential)]
public struct AraMeterInput
{
    public double Quarter;
    public int Numerator;
    public int Denominator;
}

[StructLayout(LayoutKind.Sequential)]
public struct AraNoteInput
{
    public ulong Id;
    public double Seconds;
    public double Duration;
    public int Pitch;
}

[StructLayout(LayoutKind.Sequential)]
public struct AraRegionInput
{
    public double SourceStart;
    public double SourceDuration;
    public double SongStart;
    public double SongDuration;
}

public static partial class NativeEntry
{
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int BuildAraRenderState(byte* state, int length, double playbackScale,
        byte* destination, int capacity, int* required)
    {
        if (state == null || length <= 0 || required == null || capacity < 0 ||
            (destination == null && capacity != 0))
            return 1;
        *required = 0;
        try
        {
            var bytes = CubaseAraScore.CreateRenderState(new ReadOnlySpan<byte>(state, length), playbackScale);
            *required = bytes.Length;
            if (destination == null)
                return 0;
            if (capacity < bytes.Length)
                return 1;
            bytes.CopyTo(new Span<byte>(destination, capacity));
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("ARA render state construction failed: {0}", exception);
            return 1;
        }
    }

    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static unsafe int BuildAraProjectState(byte* template, int templateLength,
        AraTempoInput* tempo, int tempoCount, AraMeterInput* meters, int meterCount,
        AraNoteInput* notes, int noteCount,
        AraRegionInput* region, byte* destination, int capacity, int* required)
    {
        if (required == null || region == null || tempo == null || tempoCount < 2 || noteCount < 0 ||
            (notes == null && noteCount != 0) || meterCount < 0 || (meters == null && meterCount != 0) || templateLength < 0 ||
            (template == null && templateLength != 0) || capacity < 0 ||
            (destination == null && capacity != 0))
            return 1;
        *required = 0;
        try
        {
            var voice = templateLength == 0 ? new SoundSourceInfo() :
                CubaseProjectState.Decode(new ReadOnlySpan<byte>(template, templateLength)).Project.Tracks
                    .SelectMany(track => track.Parts).OfType<MidiPartInfo>().FirstOrDefault()?.SoundSource
                    ?? new SoundSourceInfo();
            var tempoPoints = new ReadOnlySpan<AraTempoInput>(tempo, tempoCount).ToArray()
                .Select(point => new CubaseAraTempoPoint(point.Seconds, point.Quarter)).ToArray();
            var meterPoints = new ReadOnlySpan<AraMeterInput>(meters, meterCount).ToArray()
                .Select(point => new CubaseAraMeterPoint(point.Quarter, point.Numerator, point.Denominator)).ToArray();
            var sourceNotes = new ReadOnlySpan<AraNoteInput>(notes, noteCount).ToArray()
                .Select(note => new CubaseAraSourceNote(note.Id, note.Seconds, note.Duration, note.Pitch)).ToArray();
            var projection = CubaseAraScore.Create(tempoPoints, sourceNotes,
                new(region->SourceStart, region->SourceDuration, region->SongStart, region->SongDuration), voice, meterPoints);
            var bytes = CubaseAraMerge.UpdateState(new ReadOnlySpan<byte>(template, templateLength), projection);
            *required = bytes.Length;
            if (destination == null)
                return 0;
            if (capacity < bytes.Length)
                return 1;
            bytes.CopyTo(new Span<byte>(destination, capacity));
            return 0;
        }
        catch (Exception exception)
        {
            Trace.TraceError("ARA score construction failed: {0}", exception);
            return 1;
        }
    }
}
