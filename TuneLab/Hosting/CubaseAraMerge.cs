using System;
using System.Collections.Generic;
using System.Linq;
using TuneLab.SDK;
using TuneLab.Data.Timing;
using TuneLab.Foundation;

namespace TuneLab.Hosting;

// IDs are supplied by the source association. Zero identifies an editor-added
// note. Keep the association separate from frozen NoteInfo and engine properties.
internal static class CubaseAraMerge
{
    public static byte[] UpdateState(ReadOnlySpan<byte> currentState, CubaseAraScoreProjection incoming)
    {
        var current = currentState.IsEmpty ? null : CubaseProjectState.DecodeEnvelope(currentState);
        var association = current?.Ara;
        var merged = incoming;
        if (association != null)
        {
            var baseline = new CubaseAraScoreProjection(CubaseProjectState.Decode(association.BaselineState),
                association.BaselineNoteIds, association.SourceZeroSongSeconds, association.SourceZeroSongQuarter);
            var edited = new CubaseAraScoreProjection(current!.Project, association.CurrentNoteIds,
                association.SourceZeroSongSeconds, association.SourceZeroSongQuarter);
            merged = Merge(baseline, edited, incoming);
        }
        else if (current != null)
        {
            throw new InvalidOperationException("An existing unassociated project cannot be replaced by an ARA source update.");
        }
        return CubaseProjectState.Encode(merged.File, new CubaseAraMetadata
        {
            SourceId = association?.SourceId ?? string.Empty,
            BaselineState = CubaseProjectState.Encode(incoming.File),
            BaselineNoteIds = incoming.NoteIds.ToArray(),
            CurrentNoteIds = merged.NoteIds.ToArray(),
            SourceZeroSongSeconds = incoming.SourceZeroSongSeconds,
            SourceZeroSongQuarter = incoming.SourceZeroSongQuarter,
            MeterOriginTick = incoming.MeterOriginTick
        });
    }

    public static CubaseAraScoreProjection Merge(CubaseAraScoreProjection baseline,
        CubaseAraScoreProjection edited, CubaseAraScoreProjection incoming)
    {
        var before = SourceNotes(baseline);
        var next = SourceNotes(incoming);
        var file = CubaseProjectState.Decode(CubaseProjectState.Encode(edited.File));
        var part = file.Project.Tracks.SelectMany(track => track.Parts).OfType<MidiPartInfo>().First();
        if (edited.NoteIds.Count != part.Notes.Count)
            throw new ArgumentException("ARA edited note association does not match the score.", nameof(edited));
        var baselinePart = baseline.File.Project.Tracks.SelectMany(track => track.Parts).OfType<MidiPartInfo>().First();
        var incomingPart = incoming.File.Project.Tracks.SelectMany(track => track.Parts).OfType<MidiPartInfo>().First();
        var oldTiming = Timing(edited.File.Project);
        var newTiming = Timing(incoming.File.Project);
        var baselineTiming = Timing(baseline.File.Project);
        bool sameTiming = oldTiming.Tempos.SequenceEqual(newTiming.Tempos);
        double oldAnchor = part.Pos;
        double newAnchor = sameTiming ? oldAnchor : newTiming.ToTick(oldTiming.ToSecond(oldAnchor));
        double Map(double tick) => sameTiming ? tick :
            newTiming.ToTick(oldTiming.ToSecond(oldAnchor + tick)) - newAnchor;
        var currentIds = new HashSet<ulong>();
        var merged = new List<(ulong Id, NoteInfo Note)>();
        for (int index = 0; index < part.Notes.Count; index++)
        {
            ulong id = edited.NoteIds[index];
            var note = part.Notes[index];
            if (id == 0)
            {
                double end = Map(note.Pos + note.Dur);
                note.Pos = Map(note.Pos);
                note.Dur = end - note.Pos;
                merged.Add((id, note));
                continue;
            }
            if (!currentIds.Add(id))
                throw new ArgumentException("ARA edited note IDs must be unique.", nameof(edited));
            if (!before.TryGetValue(id, out var previous))
                throw new ArgumentException("ARA edited note ID has no import baseline.", nameof(edited));
            if (!next.TryGetValue(id, out var replacement))
                continue;

            // Shared edits are source-time data. A different region/context
            // changes the tick representation, not the edited source seconds.
            double oldStart = oldTiming.ToSecond(oldAnchor + note.Pos);
            double oldDuration = oldTiming.ToSecond(oldAnchor + note.Pos + note.Dur) - oldStart;
            double baseStart = baselineTiming.ToSecond(baselinePart.Pos + previous.Pos);
            double baseDuration = baselineTiming.ToSecond(baselinePart.Pos + previous.Pos + previous.Dur) - baseStart;
            double hostStartTick = incomingPart.Pos + replacement.Pos;
            double hostStart = newTiming.ToSecond(hostStartTick);
            double hostDuration = newTiming.ToSecond(hostStartTick + replacement.Dur) - hostStart;
            bool editedPosition = Math.Abs(oldStart - baseStart) > 1e-9;
            bool editedDuration = Math.Abs(oldDuration - baseDuration) > 1e-9;
            double startTick = editedPosition ? Map(note.Pos) + newAnchor : hostStartTick;
            double duration;
            if (editedDuration && sameTiming && startTick == oldAnchor + note.Pos)
                duration = note.Dur;
            else if (!editedDuration && startTick == hostStartTick)
                duration = replacement.Dur;
            else
            {
                double seconds = editedDuration ? oldDuration : hostDuration;
                duration = newTiming.ToTick(newTiming.ToSecond(startTick) + seconds) - startTick;
            }
            note.Pos = startTick - newAnchor;
            note.Dur = duration;
            if (note.Pitch == previous.Pitch)
                note.Pitch = replacement.Pitch;
            merged.Add((id, note));
        }
        foreach (var pair in next)
        {
            // A baseline ID missing from edited was deleted by the user. Do
            // not resurrect it merely because the host repeats its snapshot.
            if (!before.ContainsKey(pair.Key))
            {
                pair.Value.Pos += incomingPart.Pos - newAnchor;
                merged.Add((pair.Key, pair.Value));
            }
        }
        ReprojectExpressions(part, Map);
        part.StartOffset = Map(part.StartOffset);
        part.EndOffset = Map(part.EndOffset);
        part.Pos = newAnchor;
        var ordered = merged.OrderBy(entry => entry.Note.Pos).ToArray();
        part.Notes = ordered.Select(entry => entry.Note).ToList();
        part.EndOffset = Math.Max(part.EndOffset, part.Notes.Select(note => note.Pos + note.Dur).DefaultIfEmpty().Max());
        file.Project.Tempos = incoming.File.Project.Tempos.Select(tempo =>
            new TempoInfo { Pos = tempo.Pos, Bpm = tempo.Bpm }).ToList();
        file.Project.TimeSignatures = incoming.File.Project.TimeSignatures.Select(signature => new TimeSignatureInfo
        {
            BarIndex = signature.BarIndex,
            Numerator = signature.Numerator,
            Denominator = signature.Denominator
        }).ToList();
        return new(file, ordered.Select(entry => entry.Id).ToArray(),
            incoming.SourceZeroSongSeconds, incoming.SourceZeroSongQuarter, incoming.MeterOriginTick);
    }

    static TempoSnapshot Timing(ProjectInfo project) => new(project.Tempos.Select(
        tempo => new TempoMark(tempo.Pos, tempo.Bpm)).ToArray(), MusicTheory.RESOLUTION);

    static void ReprojectExpressions(MidiPartInfo part, Func<double, double> map)
    {
        List<Point> Points(List<Point> points) => points.Select(point => new Point(map(point.X), point.Y)).ToList();
        List<List<Point>> Segments(List<List<Point>> segments) => segments.Select(Points).ToList();
        part.Pitch.Segments = Segments(part.Pitch.Segments);
        foreach (var automation in part.Automations.Values)
            automation.Points = Points(automation.Points);
        foreach (var automation in part.PiecewiseAutomations.Values)
            automation.Segments = Segments(automation.Segments);
        foreach (var effect in part.Effects)
        {
            foreach (var automation in effect.Automations.Values)
                automation.Points = Points(automation.Points);
            foreach (var automation in effect.PiecewiseAutomations.Values)
                automation.Segments = Segments(automation.Segments);
        }
        foreach (var vibrato in part.Vibratos)
        {
            double end = map(vibrato.Pos + vibrato.Dur);
            vibrato.Pos = map(vibrato.Pos);
            vibrato.Dur = end - vibrato.Pos;
        }
    }

    static Dictionary<ulong, NoteInfo> SourceNotes(CubaseAraScoreProjection projection)
    {
        // Clone input notes too: the returned score must not mutate either
        // baseline on a subsequent editor change or another modification.
        var copy = CubaseProjectState.Decode(CubaseProjectState.Encode(projection.File));
        var part = copy.Project.Tracks.SelectMany(track => track.Parts).OfType<MidiPartInfo>().First();
        if (part.Notes.Count != projection.NoteIds.Count)
            throw new ArgumentException("ARA source note association does not match the score.");
        var result = new Dictionary<ulong, NoteInfo>();
        for (int index = 0; index < part.Notes.Count; index++)
            if (projection.NoteIds[index] == 0 || !result.TryAdd(projection.NoteIds[index], part.Notes[index]))
                throw new ArgumentException("ARA source note IDs must be nonzero and unique.");
        return result;
    }
}
