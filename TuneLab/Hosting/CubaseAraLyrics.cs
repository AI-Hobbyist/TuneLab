using System;
using System.Collections.Generic;
using System.Linq;
using TuneLab.SDK;
using TuneLab.Foundation;

namespace TuneLab.Hosting;

// IDs here are persistent source IDs resolved by the ingress adapter, never
// VST3 runtime noteIds. This operation runs outside the audio callback.
internal readonly record struct CubaseAraLyricUpdate(ulong NoteId, string Text);
internal sealed record CubaseAraLyricsResult(byte[] State, ulong[] ChangedNotes, ulong[] Conflicts);

internal static class CubaseAraLyrics
{
    internal static bool SameSourceAssociation(CubaseProjectEnvelope? before, CubaseProjectEnvelope? after)
    {
        var left = before?.Ara;
        var right = after?.Ara;
        if (left == null || right == null)
            return left == null && right == null;
        if (left.SourceId != right.SourceId || left.SourceZeroSongSeconds != right.SourceZeroSongSeconds ||
            left.SourceZeroSongQuarter != right.SourceZeroSongQuarter || left.MeterOriginTick != right.MeterOriginTick ||
            !left.BaselineNoteIds.SequenceEqual(right.BaselineNoteIds))
            return false;
        var first = CubaseProjectState.Decode(left.BaselineState);
        var second = CubaseProjectState.Decode(right.BaselineState);
        var firstPart = SourcePart(first);
        var secondPart = SourcePart(second);
        // Only host identity, geometry and clocks invalidate runtime bindings.
        return firstPart.Pos == secondPart.Pos &&
            firstPart.Notes.Select(note => (note.Pos, note.Dur, note.Pitch))
                .SequenceEqual(secondPart.Notes.Select(note => (note.Pos, note.Dur, note.Pitch))) &&
            first.Project.Tempos.Select(tempo => (tempo.Pos, tempo.Bpm))
                .SequenceEqual(second.Project.Tempos.Select(tempo => (tempo.Pos, tempo.Bpm))) &&
            first.Project.TimeSignatures.Select(meter => (meter.BarIndex, meter.Numerator, meter.Denominator))
                .SequenceEqual(second.Project.TimeSignatures.Select(meter => (meter.BarIndex, meter.Numerator, meter.Denominator)));
    }

    // Resolve against the host baseline, not locally moved/transposed notes.
    // Equal-pitch simultaneous notes are ambiguous; never pick by list order.
    public static ulong ResolveSourceNote(ReadOnlySpan<byte> state, string sourceId,
        double songQuarter, int pitch)
    {
        if (!double.IsFinite(songQuarter) || pitch < 0 || pitch > 127)
            throw new ArgumentOutOfRangeException(nameof(songQuarter));
        var envelope = CubaseProjectState.DecodeEnvelope(state);
        var association = envelope.Ara
            ?? throw new InvalidOperationException("Host lyrics require a source-associated project.");
        if (association.SourceId != sourceId)
            throw new ArgumentException("The lyric source does not match the document.", nameof(sourceId));
        var baseline = SourcePart(CubaseProjectState.Decode(association.BaselineState));
        ulong found = 0;
        for (int index = 0; index < baseline.Notes.Count; index++)
        {
            var note = baseline.Notes[index];
            double quarter = association.SourceZeroSongQuarter + (baseline.Pos + note.Pos) / MusicTheory.RESOLUTION;
            if (note.Pitch != pitch || Math.Abs(quarter - songQuarter) > 1e-7)
                continue;
            if (found != 0)
                return 0;
            found = association.BaselineNoteIds[index];
        }
        return found;
    }

    public static CubaseAraLyricsResult UpdateState(ReadOnlySpan<byte> state, string sourceId,
        IReadOnlyList<CubaseAraLyricUpdate> updates)
    {
        var envelope = CubaseProjectState.DecodeEnvelope(state);
        var association = envelope.Ara
            ?? throw new InvalidOperationException("Host lyrics require a source-associated project.");
        if (association.SourceId != sourceId)
            throw new ArgumentException("The lyric source does not match the document.", nameof(sourceId));
        var baseline = CubaseProjectState.Decode(association.BaselineState);
        var before = SourcePart(baseline).Notes;
        var current = SourcePart(envelope.Project).Notes;
        var baselineIndices = association.BaselineNoteIds.Select((id, index) => (id, index))
            .ToDictionary(entry => entry.id, entry => entry.index);
        var currentIndices = association.CurrentNoteIds.Select((id, index) => (id, index))
            .Where(entry => entry.id != 0).ToDictionary(entry => entry.id, entry => entry.index);
        var seen = new HashSet<ulong>();
        var changed = new List<ulong>();
        var conflicts = new List<ulong>();
        foreach (var update in updates)
        {
            if (update.Text == null || !seen.Add(update.NoteId) ||
                !baselineIndices.TryGetValue(update.NoteId, out int baselineIndex))
                throw new ArgumentException("Lyrics require unique known source IDs and nonnull text.", nameof(updates));
            var previous = before[baselineIndex];
            if (previous.Lyric == update.Text)
            {
                // No mutation is needed, but report a still-divergent local
                // lyric. After the user resolves it, the same event is unchanged.
                if (currentIndices.TryGetValue(update.NoteId, out int repeatedIndex) &&
                    current[repeatedIndex].Lyric != update.Text)
                    conflicts.Add(update.NoteId);
                continue;
            }
            if (currentIndices.TryGetValue(update.NoteId, out int currentIndex))
            {
                var note = current[currentIndex];
                // Retain local lyrics and explicit pronunciation/phoneme edits.
                // The host baseline still advances so repeated delivery is inert.
                if (note.Lyric != previous.Lyric || note.Pronunciation != previous.Pronunciation ||
                    note.LeadingPhonemes.Count != 0 || note.BodyPhonemes.Count != 0)
                {
                    conflicts.Add(update.NoteId);
                }
                else
                {
                    note.Lyric = update.Text;
                    note.Pronunciation = string.Empty;
                    changed.Add(update.NoteId);
                }
            }
            // Deleted notes stay deleted. Empty text clears; '-' and Unicode
            // pass through unchanged. The selected engine owns their semantics.
            previous.Lyric = update.Text;
        }
        association.BaselineState = CubaseProjectState.Encode(baseline);
        return new(CubaseProjectState.Encode(envelope.Project, association), changed.ToArray(), conflicts.ToArray());
    }

    internal static MidiPartInfo SourcePart(TuneLab.Extensions.Formats.TLP.NativeProjectFile file)
        => file.Project.Tracks.SelectMany(track => track.Parts).OfType<MidiPartInfo>().First();
}
