using System;
using System.Collections.Generic;
using System.Linq;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.Foundation;
using TuneLab.SDK;

namespace TuneLab.Hosting;

internal readonly record struct CubaseAraTempoPoint(double Seconds, double Quarter);
internal readonly record struct CubaseAraMeterPoint(double Quarter, int Numerator, int Denominator);
internal readonly record struct CubaseAraSourceNote(ulong Id, double Seconds, double Duration, int Pitch);
internal readonly record struct CubaseAraRegion(double SourceStart, double SourceDuration,
    double SongStart, double SongDuration);
internal sealed record CubaseAraScoreProjection(NativeProjectFile File, IReadOnlyList<ulong> NoteIds,
    double SourceZeroSongSeconds, double SourceZeroSongQuarter, double MeterOriginTick = 0);

// Creates source-local musical data. The region keeps song placement and crop;
// rendering this score uses source seconds, then the native region maps it into
// song seconds. The standalone project's time origin remains unchanged.
internal static class CubaseAraScore
{
    // A render-only copy: changing tempo scales synthesis timing without
    // resampling the waveform or modifying the editable source association.
    public static byte[] CreateRenderState(ReadOnlySpan<byte> state, double playbackScale)
    {
        if (!double.IsFinite(playbackScale) || playbackScale <= 0)
            throw new ArgumentOutOfRangeException(nameof(playbackScale));
        var envelope = CubaseProjectState.DecodeEnvelope(state);
        var file = envelope.Project;
        foreach (var tempo in file.Project.Tempos)
        {
            tempo.Bpm /= playbackScale;
            if (!double.IsFinite(tempo.Bpm) || tempo.Bpm <= 0)
                throw new ArgumentOutOfRangeException(nameof(playbackScale));
        }
        return CubaseProjectState.Encode(file, envelope.Ara);
    }

    public static CubaseAraScoreProjection Create(IReadOnlyList<CubaseAraTempoPoint> tempo,
        IReadOnlyList<CubaseAraSourceNote> notes, CubaseAraRegion region, SoundSourceInfo voice,
        IReadOnlyList<CubaseAraMeterPoint>? signatures = null)
    {
        ArgumentNullException.ThrowIfNull(voice);
        if (tempo.Count < 2 || !tempo.Any(point => point.Quarter == 0))
            throw new ArgumentException("A full ARA tempo map needs two points and quarter zero.", nameof(tempo));
        for (int index = 0; index < tempo.Count; index++)
        {
            var point = tempo[index];
            if (!double.IsFinite(point.Seconds) || !double.IsFinite(point.Quarter) ||
                (index > 0 && (point.Seconds <= tempo[index - 1].Seconds ||
                    point.Quarter <= tempo[index - 1].Quarter)))
                throw new ArgumentException("ARA tempo points must be finite and increasing.", nameof(tempo));
        }
        if (!double.IsFinite(region.SourceStart) || !double.IsFinite(region.SongStart) ||
            !double.IsFinite(region.SourceDuration) || !double.IsFinite(region.SongDuration) ||
            region.SourceDuration <= 0 || region.SongDuration <= 0)
            throw new ArgumentException("A score projection requires a nonempty finite region.", nameof(region));

        double ratio = region.SongDuration / region.SourceDuration;
        double originSeconds = region.SongStart - region.SourceStart * ratio;
        double originQuarter = QuarterAt(originSeconds);
        var file = new NativeProjectFile();
        int initialSegment = SegmentAt(originSeconds);
        file.Project.Tempos.Add(new TempoInfo { Pos = 0, Bpm = Bpm(initialSegment) });
        // The final sync point continues the preceding tempo, so it introduces
        // no new tempo mark. Keep only boundaries after source zero.
        for (int index = 1; index < tempo.Count - 1; index++)
        {
            if (tempo[index].Seconds <= originSeconds)
                continue;
            file.Project.Tempos.Add(new TempoInfo
            {
                Pos = (tempo[index].Quarter - originQuarter) * MusicTheory.RESOLUTION,
                Bpm = Bpm(index)
            });
        }
        var part = new MidiPartInfo
        {
            Name = "ARA source",
            Pos = 0,
            StartOffset = 0,
            EndOffset = 0,
            SoundSource = new SoundSourceInfo { Type = voice.Type, Id = voice.Id }
        };
        var ids = new List<ulong>(notes.Count);
        var seen = new HashSet<ulong>();
        foreach (var note in notes)
        {
            if (note.Id == 0 || !seen.Add(note.Id) || !double.IsFinite(note.Seconds) ||
                !double.IsFinite(note.Duration) || note.Duration < 0)
                throw new ArgumentException("ARA source notes need unique IDs and finite timing.", nameof(notes));
            double start = QuarterAt(originSeconds + note.Seconds * ratio) - originQuarter;
            double end = QuarterAt(originSeconds + (note.Seconds + note.Duration) * ratio) - originQuarter;
            part.Notes.Add(new NoteInfo
            {
                Pos = start * MusicTheory.RESOLUTION,
                Dur = (end - start) * MusicTheory.RESOLUTION,
                Pitch = note.Pitch
            });
            ids.Add(note.Id);
            part.EndOffset = Math.Max(part.EndOffset, end * MusicTheory.RESOLUTION);
        }
        file.Project.Tracks.Add(new TrackInfo { Parts = { part } });
        double meterOriginTick = ProjectMeters(file.Project, signatures, originQuarter);
        return new(file, ids, originSeconds, originQuarter, meterOriginTick);

        int SegmentAt(double seconds)
        {
            int lower = 0;
            int upper = tempo.Count;
            while (lower < upper)
            {
                int middle = lower + (upper - lower) / 2;
                if (tempo[middle].Seconds <= seconds)
                    lower = middle + 1;
                else
                    upper = middle;
            }
            return Math.Clamp(lower - 1, 0, tempo.Count - 2);
        }
        double QuarterAt(double seconds)
        {
            int index = SegmentAt(seconds);
            var left = tempo[index];
            var right = tempo[index + 1];
            return left.Quarter + (seconds - left.Seconds) *
                (right.Quarter - left.Quarter) / (right.Seconds - left.Seconds);
        }
        double Bpm(int index) => 60 * ratio * (tempo[index + 1].Quarter - tempo[index].Quarter) /
            (tempo[index + 1].Seconds - tempo[index].Seconds);
    }
    static double ProjectMeters(ProjectInfo project, IReadOnlyList<CubaseAraMeterPoint>? signatures,
        double originQuarter)
    {
        if (signatures == null || signatures.Count == 0)
            return 0;
        var bars = new int[signatures.Count];
        int active = 0;
        for (int index = 0; index < signatures.Count; index++)
        {
            var signature = signatures[index];
            if (!double.IsFinite(signature.Quarter) || signature.Numerator <= 0 || signature.Denominator <= 0)
                throw new ArgumentException("ARA meter points need finite positions and positive meters.", nameof(signatures));
            if (index > 0)
            {
                var previous = signatures[index - 1];
                double count = (signature.Quarter - previous.Quarter) * previous.Denominator / (4.0 * previous.Numerator);
                if (!double.IsFinite(count) || count <= 0 || Math.Abs(count - Math.Round(count)) > 1e-8)
                    throw new ArgumentException("ARA meter changes must lie on increasing bar boundaries.", nameof(signatures));
                bars[index] = checked(bars[index - 1] + (int)Math.Round(count));
            }
            if (signature.Quarter <= originQuarter)
                active = index;
        }
        var initial = signatures[active];
        double length = 4.0 * initial.Numerator / initial.Denominator;
        int offset = checked((int)Math.Floor((originQuarter - initial.Quarter) / length + 1e-9));
        double originTick = Math.Min(0, (initial.Quarter + offset * length - originQuarter) * MusicTheory.RESOLUTION);
        project.TimeSignatures.Add(new TimeSignatureInfo
        {
            BarIndex = checked(bars[active] + offset),
            Numerator = initial.Numerator,
            Denominator = initial.Denominator
        });
        for (int index = active + 1; index < signatures.Count; index++)
        {
            var signature = signatures[index];
            project.TimeSignatures.Add(new TimeSignatureInfo
            {
                BarIndex = bars[index],
                Numerator = signature.Numerator,
                Denominator = signature.Denominator
            });
        }
        return originTick;
    }

}
