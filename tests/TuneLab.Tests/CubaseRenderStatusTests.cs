using TuneLab.Hosting;
using Xunit;

namespace TuneLab.Tests;

public class CubaseRenderStatusTests
{
    [Theory]
    [InlineData(-1, "尚未准备", "按键后准备")]
    [InlineData(0, "就绪", "音符已缓存")]
    [InlineData(7, "合成中", "合成中")]
    [InlineData(6, "已取消", "已取消")]
    [InlineData(5, "合成失败（5）", "合成失败（5）")]
    [InlineData(99, "合成失败（99）", "合成失败（99）")]
    public void DistinguishesUnpreparedReadyBusyCancelledAndFailed(int result, string song, string preview)
    {
        string text = new CubaseRenderStatus(result, result, 0).DisplayText();
        Assert.Contains($"工程：{song}", text);
        Assert.Contains($"MIDI 试听：{preview}", text);
        Assert.DoesNotContain("最近准备", text);
    }

    [Fact]
    public void DurationOnlyDescribesTheMostRecentCompletedPreview()
    {
        Assert.Contains("最近准备 12.3 ms", new CubaseRenderStatus(0, 0, 12345).DisplayText());
        Assert.DoesNotContain("最近准备", new CubaseRenderStatus(0, 7, 12345).DisplayText());
        Assert.DoesNotContain("最近准备", new CubaseRenderStatus(0, 5, 12345).DisplayText());
    }
}
