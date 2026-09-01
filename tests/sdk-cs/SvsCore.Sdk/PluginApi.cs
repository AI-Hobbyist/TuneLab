using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace SvsCore.Sdk;

public sealed record VoiceSource(string Id, string Name, string Description, byte[]? Avatar = null,
                                 byte[]? Portrait = null);
public sealed record Format(string Name, string Extension);

[StructLayout(LayoutKind.Sequential)]
public struct StringView { public IntPtr Data; public nuint Size; }

[StructLayout(LayoutKind.Sequential)]
public struct Image { public StringView MimeType; public StringView Path; public IntPtr Data; public nuint Size; }

[StructLayout(LayoutKind.Sequential)]
public struct VoiceSourceInfo {
    public StringView Id;
    public StringView Name;
    public StringView Description;
    public Image Avatar;
    public Image Portrait;
}

[StructLayout(LayoutKind.Sequential)]
public struct PluginVtable {
    public uint Size;
    public uint ApiVersion;
    public IntPtr Name;
    public IntPtr Version;
    public IntPtr VoiceSourceCount;
    public IntPtr VoiceSourceGet;
    public IntPtr FormatCount;
    public IntPtr FormatName;
    public IntPtr FormatExtension;
}

public static unsafe class PluginApi {
    public const uint ApiVersion = 0x00010000u;
    const int Ok = 0;
    const int NotFound = 2;
    static VoiceSource[] sVoices = [];
    static Format[] sFormats = [];
    static IntPtr[] sVoiceIds = [];
    static IntPtr[] sVoiceNames = [];
    static IntPtr[] sVoiceDescriptions = [];
    static IntPtr[] sFormatNames = [];
    static IntPtr[] sFormatExtensions = [];
    static GCHandle[] sImages = [];
    static IntPtr sName;
    static IntPtr sVersion;
    static IntPtr sVtable;

    public static IntPtr Create(string name, string version, VoiceSource[] voices, Format[] formats) {
        if (sVtable != IntPtr.Zero) return sVtable;
        sVoices = voices;
        sFormats = formats;
        sName = Utf8(name);
        sVersion = Utf8(version);
        sVoiceIds = voices.Select(voice => Utf8(voice.Id)).ToArray();
        sVoiceNames = voices.Select(voice => Utf8(voice.Name)).ToArray();
        sVoiceDescriptions = voices.Select(voice => Utf8(voice.Description)).ToArray();
        sFormatNames = formats.Select(format => Utf8(format.Name)).ToArray();
        sFormatExtensions = formats.Select(format => Utf8(format.Extension)).ToArray();
        sImages = voices.SelectMany(voice => new[] { voice.Avatar, voice.Portrait })
            .Select(image => image is null ? default : GCHandle.Alloc(image, GCHandleType.Pinned)).ToArray();
        var table = new PluginVtable {
            Size = (uint)Marshal.SizeOf<PluginVtable>(), ApiVersion = ApiVersion,
            Name = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr>)&GetName,
            Version = (IntPtr)(delegate* unmanaged[Stdcall]<IntPtr>)&GetVersion,
            VoiceSourceCount = (IntPtr)(delegate* unmanaged[Stdcall]<nuint>)&VoiceSourceCount,
            VoiceSourceGet = (IntPtr)(delegate* unmanaged[Stdcall]<nuint, VoiceSourceInfo*, int>)&VoiceSourceGet,
            FormatCount = (IntPtr)(delegate* unmanaged[Stdcall]<nuint>)&FormatCount,
            FormatName = (IntPtr)(delegate* unmanaged[Stdcall]<nuint, IntPtr>)&FormatName,
            FormatExtension = (IntPtr)(delegate* unmanaged[Stdcall]<nuint, IntPtr>)&FormatExtension,
        };
        sVtable = Marshal.AllocHGlobal(Marshal.SizeOf<PluginVtable>());
        Marshal.StructureToPtr(table, sVtable, false);
        return sVtable;
    }

    public static unsafe IntPtr GetApi(uint hostApiVersion, uint* pluginApiVersion) {
        if (pluginApiVersion != null) *pluginApiVersion = ApiVersion;
        return hostApiVersion == ApiVersion ? sVtable : IntPtr.Zero;
    }

    static IntPtr Utf8(string text) => Marshal.StringToCoTaskMemUTF8(text);
    static StringView View(IntPtr data, string text) => new() { Data = data, Size = (nuint)System.Text.Encoding.UTF8.GetByteCount(text) };
    static Image ImageFor(int index, byte[]? data) => new() {
        MimeType = data is null ? default : View(Utf8("image/png"), "image/png"),
        Data = data is null ? IntPtr.Zero : sImages[index].AddrOfPinnedObject(),
        Size = data is null ? 0u : (nuint)data.Length,
    };

    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    static IntPtr GetName() => sName;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    static IntPtr GetVersion() => sVersion;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    static nuint VoiceSourceCount() => (nuint)sVoices.Length;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    static int VoiceSourceGet(nuint index, VoiceSourceInfo* output) {
        if (index >= (nuint)sVoices.Length || output is null) return NotFound;
        var voice = sVoices[(int)index];
        *output = new VoiceSourceInfo {
            Id = View(sVoiceIds[(int)index], voice.Id), Name = View(sVoiceNames[(int)index], voice.Name),
            Description = View(sVoiceDescriptions[(int)index], voice.Description),
            Avatar = ImageFor((int)index * 2, voice.Avatar), Portrait = ImageFor((int)index * 2 + 1, voice.Portrait),
        };
        return Ok;
    }
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    static nuint FormatCount() => (nuint)sFormats.Length;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    static IntPtr FormatName(nuint index) => index < (nuint)sFormats.Length ? sFormatNames[(int)index] : IntPtr.Zero;
    [UnmanagedCallersOnly(CallConvs = [typeof(CallConvStdcall)])]
    static IntPtr FormatExtension(nuint index) => index < (nuint)sFormats.Length ? sFormatExtensions[(int)index] : IntPtr.Zero;
}