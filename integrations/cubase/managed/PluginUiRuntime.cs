using System;
using System.Threading;
using Avalonia;
using Avalonia.Threading;

namespace TuneLab.Cubase.Managed;

internal static class PluginUiRuntime
{
    // Called only when attaching an editor, never while scanning or processing
    // audio. Cubase owns the Windows message loop and the process lifetime.
    internal static void Initialize()
    {
        if (Thread.CurrentThread.GetApartmentState() != ApartmentState.STA)
        {
            throw new InvalidOperationException("The embedded editor requires an STA UI thread.");
        }

        PluginServices.EnsureCore();
        if (Application.Current == null)
        {
            TuneLab.Program.ConfigureAppCommon(AppBuilder.Configure<TuneLab.App>()
                .UsePlatformDetect())
                .SetupWithoutStarting();
        }
        else if (Application.Current is not TuneLab.App)
        {
            throw new InvalidOperationException("An incompatible Avalonia application is already initialized.");
        }

        Dispatcher.UIThread.VerifyAccess();
        AvaloniaSynchronizationContext.InstallIfNeeded();
        PluginServices.InitializeEngines();
    }
}
