using TuneLab.Input;
using Xunit;

namespace TuneLab.Tests;

public class EditorActionCollectionTests
{
    [Fact]
    public void IdenticalIdsExecuteOnlyTheirOwningInstance()
    {
        var first = new EditorActionCollection();
        var second = new EditorActionCollection();
        int firstCount = 0;
        int secondCount = 0;
        first.Register(new EditorAction
        {
            Id = "test.instance",
            Kind = ActionKind.AppState,
            DisplayName = () => "First",
            Execute = () => firstCount++
        });
        second.Register(new EditorAction
        {
            Id = "test.instance",
            Kind = ActionKind.AppState,
            DisplayName = () => "Second",
            Execute = () => secondCount++
        });

        Assert.Null(first.Execute("test.instance"));
        Assert.Equal(1, firstCount);
        Assert.Equal(0, secondCount);
        first.Unregister("test.instance");
        Assert.Null(second.Execute("test.instance"));
        Assert.Equal(1, secondCount);
    }
}
