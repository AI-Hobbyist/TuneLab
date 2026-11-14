using System;
using System.IO;
using System.Reflection;
using System.Runtime.Loader;
using TuneLab.Extensions;
using TuneLab.SDK;
using Xunit;

namespace TuneLab.Tests;

public class PluginContractSharingTests
{
    sealed class ComponentContext : AssemblyLoadContext
    {
        readonly AssemblyDependencyResolver mResolver;

        internal ComponentContext(string assemblyPath) : base(isCollectible: true)
        {
            mResolver = new AssemblyDependencyResolver(assemblyPath);
        }

        protected override Assembly? Load(AssemblyName name)
        {
            var path = mResolver.ResolveAssemblyToPath(name);
            return path == null ? null : LoadFromAssemblyPath(path);
        }
    }

    [Fact]
    public void PluginSharesItsComponentHostsContractsInsteadOfDefaultContracts()
    {
        var hostPath = typeof(PluginLoadContext).Assembly.Location;
        var component = new ComponentContext(hostPath);
        try
        {
            var host = component.LoadFromAssemblyPath(hostPath);
            var loaderType = host.GetType("TuneLab.Extensions.PluginLoadContext", throwOnError: true)!;
            var loader = (AssemblyLoadContext)Activator.CreateInstance(loaderType,
                new object?[] { Path.GetDirectoryName(hostPath), null, true })!;
            try
            {
                var sdk = component.LoadFromAssemblyName(typeof(IVoiceSynthesisEngine).Assembly.GetName());
                var pluginSdk = loader.LoadFromAssemblyName(sdk.GetName());
                Assert.NotSame(typeof(IVoiceSynthesisEngine).Assembly, sdk);
                Assert.Same(sdk, pluginSdk);
            }
            finally
            {
                loader.Unload();
            }
        }
        finally
        {
            component.Unload();
        }
    }
}
