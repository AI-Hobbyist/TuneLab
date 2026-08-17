using System.Collections.Generic;
using System.Runtime.Versioning;
using TuneLab.Bridge;
using Xunit;

namespace TuneLab.Tests.Bridge;

[SupportedOSPlatform("windows")]
public class BridgeTrackTests
{
    [Fact]
    public void BuildBusMapGroupsEnabledTracksAndRejectsInvalidRoutes()
    {
        var first = new BridgeTrack { Name = "First", Enabled = true, BusIndex = 3 };
        var second = new BridgeTrack { Name = "Second", Enabled = true, BusIndex = 3 };
        var disabled = new BridgeTrack { Name = "Disabled", Enabled = false, BusIndex = 4 };
        var invalid = new BridgeTrack { Name = "Invalid", Enabled = true, BusIndex = BridgeTrack.MaxBusCount };

        var buses = BridgeRenderer.BuildBusMap(new List<BridgeTrack> { first, second, disabled, invalid });

        var grouped = Assert.Single(buses, pair => pair.Key == 3);
        Assert.Equal(new[] { first, second }, grouped.Value);
    }
}