using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;
using SvsCore.Sdk;

namespace SvsCore.TestPlugin;

public static class Entry {
    static readonly IntPtr Api = PluginApi.Create("SVS .NET Test Engine", "1.0.0",
        [new VoiceSource("dotnet-alice", "Managed Alice", "Managed voice source for hostfxr validation",
                         [0x89, 0x50, 0x4e, 0x47], [0x89, 0x50, 0x4e, 0x47, 0x0d, 0x0a])],
        [new Format("SVS Managed Project", "svsm")]);

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    public static unsafe IntPtr GetApi(uint hostApiVersion, uint* pluginApiVersion) {
        if (pluginApiVersion != null) *pluginApiVersion = PluginApi.ApiVersion;
        return hostApiVersion == PluginApi.ApiVersion ? Api : IntPtr.Zero;
    }
}