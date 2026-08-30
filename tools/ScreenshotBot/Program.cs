using System;
using System.IO;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Controls.ApplicationLifetimes;
using Avalonia.Headless;
using Avalonia.Threading;
using TuneLab.Configs;

namespace TuneLab.ScreenshotBot;

// 文档截图工具：把真实的 TuneLab 界面渲染成 PNG，供 docs/user-manual.zh-CN.md 使用。
//
// 关键取舍：
//   * 用 Avalonia headless 窗口系统 + 真 Skia 渲染，不开真窗口——不抢前台、不受屏幕分辨率/DPI 影响，
//     画面却与用户屏幕上一致（同一套 App/控件/样式，仅窗口系统不同）。
//   * 用 TUNELAB_DATA_DIR 把用户数据整体隔离到沙盒，截图既不带上开发机的个人设置与第三方插件，
//     也不会写坏它。
//   * 界面状态一律用数据层/编辑器 API 摆好，不靠模拟点击的坐标——重跑结果稳定。
internal static class Bot
{
    [STAThread]
    public static void Main(string[] args)
    {
        string repoRoot = FindRepoRoot();
        // 输出目录与沙盒都走环境变量而不是命令行参数：App 会读 Environment.GetCommandLineArgs()，
        // 把多余参数当成待打开的工程路径。
        string outDir = Environment.GetEnvironmentVariable("TUNELAB_SHOTS_OUT")
            ?? Path.Combine(repoRoot, "docs", "images", "manual");
        string sandboxRoot = Environment.GetEnvironmentVariable("TUNELAB_SHOTS_SANDBOX")
            ?? Path.Combine(Path.GetTempPath(), "TuneLab.ScreenshotBot");
        string dataDir = Path.Combine(sandboxRoot, "data");

        Console.WriteLine($"repo:    {repoRoot}");
        Console.WriteLine($"out:     {outDir}");
        Console.WriteLine($"sandbox: {sandboxRoot}");

        Environment.SetEnvironmentVariable("TUNELAB_DATA_DIR", dataDir);
        Sandbox.Prepare(dataDir, repoRoot);

        global::TuneLab.Program.InitCoreServices();
        ApplyDocSettings();

        var camera = new Camera(outDir);
        var builder = global::TuneLab.Program.ConfigureAppCommon(
            AppBuilder.Configure<global::TuneLab.App>()
                .UseHeadless(new AvaloniaHeadlessPlatformOptions { UseHeadlessDrawing = false }));

        builder.AfterSetup(setup => Dispatcher.UIThread.Post(
            () => { _ = RunAsync(camera, sandboxRoot, repoRoot); }, DispatcherPriority.Background));

        builder.StartWithClassicDesktopLifetime([]);
    }

    // 文档口径的设置：只钉住"必须确定"的项，其余一律留出厂默认——沙盒里没有 Settings.json，
    // 故设置窗各页照出的就是默认值，这正是手册要的。
    // **不要在这里改任何会被拍进插图的设置**：曾把 AutoSaveInterval 设成 3600 图个清静，结果
    // 「通用」页插图印着 3600（还超出滑条量程 10-60、滑条顶死在右端），与手册表格自相矛盾。
    // 自动保存写进沙盒的 AutoSave 目录，本来就无害。
    static void ApplyDocSettings()
    {
        // 语言可参数化：将来补了英文手册，拍英文版插图不必改代码（TUNELAB_SHOTS_LANG=en-US）。
        Settings.Language.Value = Environment.GetEnvironmentVariable("TUNELAB_SHOTS_LANG") is { Length: > 0 } lang
            ? lang : "zh-CN";
        // 背景图默认就是空；显式写一遍防开发机的个人配置经别的途径漏进插图。
        Settings.BackgroundImagePath.Value = string.Empty;
    }

    static async Task RunAsync(Camera camera, string sandboxRoot, string repoRoot)
    {
        int exitCode = 0;
        try
        {
            var lifetime = (IClassicDesktopStyleApplicationLifetime)Application.Current!.ApplicationLifetime!;
            if (lifetime.MainWindow is not global::TuneLab.UI.MainWindow window)
                throw new InvalidOperationException("MainWindow was not created");

            await ShotPlan.RunAsync(window, camera, sandboxRoot, repoRoot);
            Console.WriteLine($"done: {camera.Saved.Count} shot(s) -> {camera.OutDir}");
        }
        catch (Exception ex)
        {
            Console.WriteLine("FAILED: " + ex);
            exitCode = 1;
        }
        // 直接退进程：走正常关闭会触发音频引擎销毁，SDL 的设备回调此刻可能已在半路（宿主已有的关停竞态），
        // 截图早已落盘，没必要在这上面折腾。
        Console.Out.Flush();
        Environment.Exit(exitCode);
    }

    static string FindRepoRoot()
    {
        var dir = new DirectoryInfo(AppContext.BaseDirectory);
        while (dir != null && !File.Exists(Path.Combine(dir.FullName, "TuneLab.sln")))
            dir = dir.Parent;
        return dir?.FullName ?? Directory.GetCurrentDirectory();
    }
}
