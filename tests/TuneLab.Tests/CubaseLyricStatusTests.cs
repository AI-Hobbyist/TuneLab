using System;
using TuneLab.Hosting;
using Xunit;

namespace TuneLab.Tests;

public class CubaseLyricStatusTests
{
    [Theory]
    [InlineData(-1, "等待")]
    [InlineData(0, "无变化")]
    [InlineData(1, "已应用")]
    [InlineData(2, "保留")]
    [InlineData(3, "未应用")]
    [InlineData(4, "未应用")]
    [InlineData(5, "重试")]
    [InlineData(6, "失败")]
    [InlineData(7, "已拒绝")]
    [InlineData(999, "不可用")]
    public void DisplayDistinguishesAppliedAndUnappliedOutcomes(int kind, string expected)
    {
        Assert.Contains(expected, new CubaseLyricStatus(kind).DisplayText());
    }

    [Fact]
    public void PairSharesOnlyOwnerStatusAndTracksConflictResolution()
    {
        var instrument = CubasePairRegistry.RegisterInstrument();
        var bridge = CubasePairRegistry.RegisterBridge();
        var other = CubasePairRegistry.RegisterBridge();
        try
        {
            CubasePairRegistry.Pair(instrument, bridge);
            CubasePairRegistry.SetLyricStatus(instrument, new(2, 11));
            Assert.Equal(new CubaseLyricStatus(2, 11), CubasePairRegistry.GetLyricStatus(bridge));
            Assert.Null(CubasePairRegistry.GetLyricStatus(other));
            CubasePairRegistry.SetLyricStatus(instrument, new(2, 11));
            Assert.Equal(2, CubasePairRegistry.GetLyricStatus(bridge)!.Value.Kind);
            CubasePairRegistry.SetLyricStatus(instrument, new(0, 11));
            Assert.Equal(0, CubasePairRegistry.GetLyricStatus(bridge)!.Value.Kind);
            CubasePairRegistry.SetLyricStatus(bridge, new(1, 22));
            Assert.Equal(new CubaseLyricStatus(1, 22), CubasePairRegistry.GetLyricStatus(instrument));
            CubasePairRegistry.SetLyricStatus(instrument, new(4));
            Assert.Equal(4, CubasePairRegistry.GetLyricStatus(bridge)!.Value.Kind);
            CubasePairRegistry.Unregister(bridge);
            Assert.False(CubasePairRegistry.SetLyricStatus(bridge, new(4)));
            Assert.Null(CubasePairRegistry.GetLyricStatus(instrument));
            Assert.Null(CubasePairRegistry.GetLyricStatus(bridge));
            CubasePairRegistry.Pair(instrument, other);
            Assert.Null(CubasePairRegistry.GetLyricStatus(instrument));
        }
        finally
        {
            CubasePairRegistry.Unregister(instrument);
            CubasePairRegistry.Unregister(bridge);
            CubasePairRegistry.Unregister(other);
        }
    }
}
