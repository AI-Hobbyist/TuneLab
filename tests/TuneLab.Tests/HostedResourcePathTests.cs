using System;
using System.IO;
using System.Reflection;
using System.Runtime.CompilerServices;
using System.Runtime.Loader;
using Xunit;

namespace TuneLab.Tests;

public class HostedResourcePathTests
{
    [Fact]
    public void RelocatedAssemblyUsesItsOwnResourcesInsteadOfTheHostExecutableDirectory()
    {
        var directory = Path.Combine(Path.GetTempPath(), "TuneLabResources-" + Guid.NewGuid().ToString("N"));
        var managed = Path.Combine(directory, "plugin", "Contents", "Resources", "managed");
        Directory.CreateDirectory(managed);
        var assemblyPath = Path.Combine(managed, "TuneLab.dll");
        File.Copy(typeof(PathManager).Assembly.Location, assemblyPath);
        try
        {
            var paths = ReadRelocatedPaths(assemblyPath);
            Assert.Equal(Path.Combine(managed, "Resources"), paths[0]);
            Assert.Equal(Path.Combine(managed, "Resources", "Manual"), paths[1]);
            Assert.Equal(Path.Combine(managed, "Resources", "Translations"), paths[2]);
            Assert.Equal(AppDomain.CurrentDomain.BaseDirectory, paths[3]);
        }
        finally
        {
            for (int attempt = 0; attempt < 3; attempt++)
            {
                GC.Collect();
                GC.WaitForPendingFinalizers();
            }
            Directory.Delete(directory, recursive: true);
        }
    }

    // Keep assembly references outside the caller's frame so Windows can
    // release the mapped DLL before the temporary directory is removed.
    [MethodImpl(MethodImplOptions.NoInlining)]
    static string[] ReadRelocatedPaths(string assemblyPath)
    {
        var loader = new AssemblyLoadContext("TuneLab resource relocation", isCollectible: true);
        try
        {
            var assembly = loader.LoadFromAssemblyPath(assemblyPath);
            var paths = assembly.GetType("TuneLab.PathManager", throwOnError: true)!;
            string Read(string property) => (string)paths.GetProperty(property,
                BindingFlags.Static | BindingFlags.Public)!.GetValue(null)!;
            return [Read(nameof(PathManager.ResourcesFolder)), Read(nameof(PathManager.ManualFolder)),
                Read(nameof(PathManager.TranslationsFolder)), Read(nameof(PathManager.ExcutableFolder))];
        }
        finally
        {
            loader.Unload();
        }
    }

    [Fact]
    public void OrdinaryAssemblyLayoutKeepsExistingResourceLocations()
    {
        Assert.Equal(Path.Combine(AppDomain.CurrentDomain.BaseDirectory, "Resources"), PathManager.ResourcesFolder);
        Assert.Equal(Path.Combine(PathManager.ResourcesFolder, "Manual"), PathManager.ManualFolder);
    }
}
