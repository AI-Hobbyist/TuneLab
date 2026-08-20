using System;
using System.Collections.Generic;
using System.IO;
using System.Text.Json;
using TuneLab.Data;
using TuneLab.Extensions.Instruments;
using TuneLab.Extensions.Voices;
using TuneLab.Foundation;
using TuneLab.SDK;

namespace TuneLab.Extensions;

internal enum ComfortPitchLevel
{
    Comfortable,
    Available,
    Weak,
    Outside,
}

internal sealed class SoundSourceComfortRange
{
    public int MinPitch { get; }
    public int MaxPitch { get; }
    public IReadOnlySet<int> AvailablePitches => mAvailablePitches;
    public IReadOnlySet<int> WeakPitches => mWeakPitches;

    public SoundSourceComfortRange(int minPitch, int maxPitch, IReadOnlySet<int> availablePitches, IReadOnlySet<int> weakPitches)
    {
        MinPitch = Math.Min(minPitch, maxPitch);
        MaxPitch = Math.Max(minPitch, maxPitch);
        mAvailablePitches = availablePitches;
        mWeakPitches = weakPitches;
    }

    public ComfortPitchLevel LevelOf(int pitch)
    {
        if (mWeakPitches.Contains(pitch))
            return ComfortPitchLevel.Weak;
        if (pitch >= MinPitch && pitch <= MaxPitch)
            return ComfortPitchLevel.Comfortable;
        return mAvailablePitches.Contains(pitch) ? ComfortPitchLevel.Available : ComfortPitchLevel.Outside;
    }

    public static SoundSourceComfortRange? Resolve(ISoundSource? source)
    {
        if (source == null || string.IsNullOrEmpty(source.ID))
            return null;

        string key = SourceKey(source);
        if (mCache.TryGetValue(key, out var cached) && !cached.ShouldRefresh())
            return cached.Range;

        string? path = FindComfortFile(source);
        var range = path != null ? Load(path) : null;
        mCache[key] = new CacheEntry(path, range);
        return range;
    }

    static SoundSourceComfortRange? Load(string path)
    {
        try
        {
            using var stream = File.OpenRead(path);
            var file = JsonSerializer.Deserialize<ComfortJson>(stream, sJsonOptions);
            if (file == null || string.IsNullOrWhiteSpace(file.Comfort))
                return null;

            if (!TryParsePitchRange(file.Comfort, out int min, out int max))
                return null;

            HashSet<int> available = [];
            AddPitchOrRanges(file.Available, available);

            HashSet<int> weak = [];
            AddPitchOrRanges(file.Weak, weak);

            return new SoundSourceComfortRange(min, max, available, weak);
        }
        catch (Exception ex)
        {
            Log.Warning("Failed to load comfort range '" + path + "': " + ex.Message);
            return null;
        }
    }

    static void AddPitchOrRanges(List<string>? ranges, HashSet<int> pitches)
    {
        if (ranges == null)
            return;

        foreach (var item in ranges)
            AddPitchOrRange(item, pitches);
    }

    static void AddPitchOrRanges(JsonElement ranges, HashSet<int> pitches)
    {
        if (ranges.ValueKind == JsonValueKind.String)
        {
            AddPitchOrRange(ranges.GetString(), pitches);
            return;
        }

        if (ranges.ValueKind != JsonValueKind.Array)
            return;

        foreach (var item in ranges.EnumerateArray())
        {
            if (item.ValueKind == JsonValueKind.String)
                AddPitchOrRange(item.GetString(), pitches);
        }
    }

    static void AddPitchOrRange(string? text, HashSet<int> pitches)
    {
        if (string.IsNullOrWhiteSpace(text))
            return;

        if (TryParsePitchRange(text, out int min, out int max))
        {
            for (int pitch = min; pitch <= max; pitch++)
                pitches.Add(pitch);
        }
    }

    static string? FindComfortFile(ISoundSource source)
    {
        foreach (var dir in CandidateDirectories(source))
        {
            string path = Path.Combine(dir, ComfortFileName);
            if (File.Exists(path))
                return path;
        }
        return null;
    }

    static IEnumerable<string> CandidateDirectories(ISoundSource source)
    {
        string? portraitDir = PortraitDirectory(source);
        if (!string.IsNullOrEmpty(portraitDir))
            yield return portraitDir;

        if (!TryGetPackageDirectory(source, out var packageDir))
            yield break;

        yield return packageDir;

        foreach (var relative in CandidateRelativeDirectories(source))
            yield return Path.Combine(packageDir, relative);
    }

    static IEnumerable<string> CandidateRelativeDirectories(ISoundSource source)
    {
        string kind = source.Kind == SourceKind.Voice ? "voice" : "instrument";
        string plural = source.Kind == SourceKind.Voice ? "voices" : "instruments";

        yield return source.ID;
        yield return Path.Combine(kind, source.ID);
        yield return Path.Combine(plural, source.ID);

        if (!string.IsNullOrEmpty(source.Type))
        {
            yield return source.Type;
            yield return Path.Combine(source.Type, source.ID);
            yield return Path.Combine(kind, source.Type, source.ID);
            yield return Path.Combine(plural, source.Type, source.ID);
        }
    }

    static bool TryGetPackageDirectory(ISoundSource source, out string packageDir)
    {
        bool ok = source.Kind == SourceKind.Voice
            ? VoicesManager.TryGetActivePackageId(source.Type, out var packageId)
            : InstrumentsManager.TryGetActivePackageId(source.Type, out packageId);

        if (ok && packageId != ExtensionManager.BuiltInPackageId)
            return ExtensionManager.TryGetPackageDirectory(packageId, out packageDir);

        packageDir = string.Empty;
        return false;
    }

    static string? PortraitDirectory(ISoundSource source)
    {
        ImageResource? portrait = source.Kind == SourceKind.Voice
            ? (VoicesManager.TryGetVoiceInfo(source.Type, source.ID, out var voiceInfo) ? voiceInfo.Portrait : null)
            : (InstrumentsManager.TryGetInstrumentInfo(source.Type, source.ID, out var instrumentInfo) ? instrumentInfo.Portrait : null);

        if (portrait is FileImageResource fileImage && File.Exists(fileImage.Path))
            return Path.GetDirectoryName(fileImage.Path);

        return null;
    }

    static bool TryParsePitchRange(string text, out int minPitch, out int maxPitch)
    {
        minPitch = 0;
        maxPitch = 0;

        var parts = text.Split('-', StringSplitOptions.TrimEntries | StringSplitOptions.RemoveEmptyEntries);
        if (parts.Length == 1)
        {
            if (!TryParsePitch(parts[0], out minPitch))
                return false;
            maxPitch = minPitch;
            return true;
        }

        if (parts.Length == 2 && TryParsePitch(parts[0], out minPitch) && TryParsePitch(parts[1], out maxPitch))
        {
            if (minPitch > maxPitch)
                (minPitch, maxPitch) = (maxPitch, minPitch);
            return true;
        }

        return false;
    }

    static bool TryParsePitch(string text, out int pitch)
    {
        pitch = 0;
        text = text.Trim();
        if (text.Length < 2)
            return false;

        char letter = char.ToUpperInvariant(text[0]);
        if (!sNaturalPitchClasses.TryGetValue(letter, out int pitchClass))
            return false;

        int index = 1;
        if (index < text.Length)
        {
            if (text[index] == '#')
            {
                pitchClass++;
                index++;
            }
            else if (text[index] == 'b' || text[index] == 'B')
            {
                pitchClass--;
                index++;
            }
        }

        if (index >= text.Length || !int.TryParse(text[index..], out int octave))
            return false;

        pitch = MusicTheory.C0_PITCH + octave * 12 + PositiveMod(pitchClass, 12);
        return pitch >= MusicTheory.MIN_PITCH && pitch <= MusicTheory.MAX_PITCH;
    }

    static int PositiveMod(int value, int mod) => (value % mod + mod) % mod;

    static string SourceKey(ISoundSource source)
        => source.Kind + "|" + source.Type + "|" + source.ID;

    sealed class ComfortJson
    {
        public string? Comfort { get; set; }
        public JsonElement Available { get; set; }
        public List<string>? Weak { get; set; }
    }

    sealed class CacheEntry
    {
        public SoundSourceComfortRange? Range { get; }

        public CacheEntry(string? path, SoundSourceComfortRange? range)
        {
            mPath = path;
            Range = range;
            mCreatedAt = DateTime.UtcNow;
            if (path != null && File.Exists(path))
                mLastWriteUtc = File.GetLastWriteTimeUtc(path);
        }

        public bool ShouldRefresh()
        {
            if (DateTime.UtcNow - mCreatedAt < CacheDuration)
                return false;

            if (mPath == null)
                return true;

            return !File.Exists(mPath) || File.GetLastWriteTimeUtc(mPath) != mLastWriteUtc;
        }

        readonly string? mPath;
        readonly DateTime mCreatedAt;
        readonly DateTime mLastWriteUtc;
    }

    const string ComfortFileName = "comfort.json";
    static readonly TimeSpan CacheDuration = TimeSpan.FromSeconds(2);
    static readonly JsonSerializerOptions sJsonOptions = new() { PropertyNameCaseInsensitive = true };
    static readonly Dictionary<char, int> sNaturalPitchClasses = new()
    {
        ['C'] = 0,
        ['D'] = 2,
        ['E'] = 4,
        ['F'] = 5,
        ['G'] = 7,
        ['A'] = 9,
        ['B'] = 11,
    };
    static readonly Dictionary<string, CacheEntry> mCache = new();

    readonly IReadOnlySet<int> mAvailablePitches;
    readonly IReadOnlySet<int> mWeakPitches;
}
