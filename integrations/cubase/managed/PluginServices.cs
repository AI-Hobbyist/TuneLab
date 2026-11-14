using System;
using System.Collections.Generic;
using System.IO;
using TuneLab.Audio;
using TuneLab.Audio.NAudio;
using TuneLab.Extensions;
using TuneLab.Foundation;
using TuneLab.Utils;

namespace TuneLab.Cubase.Managed;

// Shared for the lifetime of the loaded integration assembly. Instance removal
// must not tear down engines still serving another document.
internal static class PluginServices
{
    static readonly Lazy<bool> sCore = new(() =>
    {
        Log.SetupLogger(new FileLogger(Path.Combine(TuneLab.PathManager.TuneLabFolder,
            "Logs", $"Cubase-{Environment.ProcessId}.log"), echoToConsole: false));
        TuneLab.Program.InitCoreServices();
        AudioUtils.Init(new NAudioCodec());
        return true;
    });

    static readonly Lazy<IReadOnlyList<string>> sEngines = new(() =>
    {
        EnsureCore();
        var directory = Path.GetDirectoryName(typeof(PluginServices).Assembly.Location)
            ?? throw new InvalidOperationException("The integration assembly has no package directory.");
        LegacyCompatLoader.Wire(directory);
        ExtensionManager.LoadExtensions();
        return SoundSourceEngines.InitAll();
    });

    internal static void EnsureCore() => _ = sCore.Value;
    internal static IReadOnlyList<string> InitializeEngines() => sEngines.Value;
}
