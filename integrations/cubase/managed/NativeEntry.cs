using System.Runtime.CompilerServices;
using System.Runtime.InteropServices;

namespace TuneLab.Cubase.Managed;

// This assembly is private to the Cubase integration, outside the frozen SDK ABI.
public static class NativeEntry
{
    [UnmanagedCallersOnly(CallConvs = new[] { typeof(CallConvCdecl) })]
    public static int GetAbiVersion()
    {
        return 1;
    }
}
