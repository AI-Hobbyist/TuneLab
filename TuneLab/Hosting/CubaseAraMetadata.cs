using System;
using System.Buffers.Binary;
using System.IO;
using System.Linq;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.SDK;

namespace TuneLab.Hosting;

internal sealed class CubaseAraMetadata
{
    public int Version { get; set; } = 1;
    public int MatchingVersion { get; set; } = 1;
    public string SourceId { get; set; } = string.Empty;
    public byte[] BaselineState { get; set; } = [];
    public ulong[] BaselineNoteIds { get; set; } = [];
    public ulong[] CurrentNoteIds { get; set; } = [];
    public double SourceZeroSongSeconds { get; set; }
    public double SourceZeroSongQuarter { get; set; }
    public double MeterOriginTick { get; set; }

    internal void Validate(NativeProjectFile current)
    {
        if (Version != 1 || MatchingVersion != 1)
            throw new NotSupportedException("Unsupported ARA association version.");
        // A baseline is a plain v1 musical state, never another association.
        // Reject nesting before decoding to keep malformed archives nonrecursive.
        if (BaselineState == null || BaselineState.Length < 16 ||
            BinaryPrimitives.ReadUInt32LittleEndian(BaselineState.AsSpan(8)) != 1)
            throw new InvalidDataException("ARA baseline must be a plain project state.");
        var baseline = CubaseProjectState.Decode(BaselineState);
        int CountNotes(NativeProjectFile file) => (file.Project.Tracks.SelectMany(track => track.Parts)
            .OfType<MidiPartInfo>().FirstOrDefault()
            ?? throw new InvalidDataException("ARA association requires a source part.")).Notes.Count;
        if (BaselineNoteIds == null || CurrentNoteIds == null || SourceId == null ||
            BaselineNoteIds.Length != CountNotes(baseline) || CurrentNoteIds.Length != CountNotes(current) ||
            BaselineNoteIds.Any(id => id == 0) || BaselineNoteIds.Distinct().Count() != BaselineNoteIds.Length ||
            CurrentNoteIds.Where(id => id != 0).Distinct().Count() != CurrentNoteIds.Count(id => id != 0) ||
            CurrentNoteIds.Any(id => id != 0 && !BaselineNoteIds.Contains(id)) ||
            !double.IsFinite(SourceZeroSongSeconds) || !double.IsFinite(SourceZeroSongQuarter) || !double.IsFinite(MeterOriginTick))
            throw new InvalidDataException("Invalid ARA note association or time origin.");
    }
}

internal sealed record CubaseProjectEnvelope(NativeProjectFile Project, CubaseAraMetadata? Ara);
