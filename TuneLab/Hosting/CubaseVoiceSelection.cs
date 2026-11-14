using System;
using System.Collections.Generic;
using System.Linq;
using TuneLab.Data;
using TuneLab.Extensions.Voices;
using TuneLab.SDK;

namespace TuneLab.Hosting;

internal sealed record CubaseEngineChoice(string Type, string Name)
{
    public override string ToString() => Name;
}

internal sealed record CubaseSingerChoice(string Id, string Name)
{
    public override string ToString() => Name;
}

// Uses the existing engine contract; no new capability is required of engines.
internal static class CubaseVoiceSelection
{
    public static IReadOnlyList<CubaseEngineChoice> Engines() => VoicesManager.GetAllVoiceEngines()
        .Select(type => new CubaseEngineChoice(type,
            type.Length == 0 ? "Empty Voice" : VoicesManager.GetDisplayName(type))).ToArray();

    public static IReadOnlyList<CubaseSingerChoice> Singers(string type)
    {
        var sources = VoicesManager.GetAllVoiceInfos(type);
        return sources == null ? Array.Empty<CubaseSingerChoice>() : sources
            .Select(source => new CubaseSingerChoice(source.Key, source.Value.Name)).ToArray();
    }

    public static string? Unavailable(string type, string id)
    {
        var sources = VoicesManager.GetAllVoiceInfos(type);
        if (sources == null)
        {
            return $"Engine unavailable: {type}";
        }
        return sources.ContainsKey(id) ? null : $"Singer unavailable: {type} / {id}";
    }

    public static void Select(IMidiPart part, string type, string id)
    {
        var error = Unavailable(type, id);
        if (error != null)
        {
            throw new InvalidOperationException(error);
        }
        if (part.SoundSource.Kind == SourceKind.Voice &&
            part.SoundSource.Type == type && part.SoundSource.ID == id)
        {
            return;
        }
        part.BeginMergeDirty();
        try
        {
            part.SoundSource.SetInfo(new SoundSourceInfo { Kind = SourceKind.Voice, Type = type, Id = id });
        }
        finally
        {
            part.EndMergeDirty();
        }
        part.Commit();
    }
}
