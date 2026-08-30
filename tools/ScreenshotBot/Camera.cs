using System;
using System.Collections.Generic;
using System.Globalization;
using System.IO;
using System.Linq;
using System.Threading.Tasks;
using Avalonia;
using Avalonia.Controls;
using Avalonia.Headless;
using Avalonia.Media;
using Avalonia.Media.Imaging;
using Avalonia.Threading;
using Avalonia.VisualTree;

namespace TuneLab.ScreenshotBot;

// 编号气泡钉在框的哪个角（贴着框内侧，避免遮住框外的界面元素）。
// 前四个把气泡贴在框内侧；OutsideCorner 把气泡骑在框的左上角上——小目标（工具按钮之类）
// 用它，免得气泡把图标本身盖住。
internal enum CalloutAt { TopLeft, TopRight, BottomLeft, BottomRight, OutsideCorner }

// 一处标注：框住 Area、并在角上放一个编号气泡。Area 由控件实测边界给出（不写死像素）。
internal sealed record Callout(Rect Area, string Label, bool Box = true, CalloutAt At = CalloutAt.TopLeft);

// 负责「渲染 → 裁剪 → 标注 → 落盘」。渲染走 headless 的真 Skia 帧捕获，画面与用户屏幕上一致。
internal sealed class Camera(string outDir)
{
    public string OutDir { get; } = outDir;
    public List<string> Saved { get; } = new();

    // 等画面稳定：headless 的渲染定时器要手动推帧，同时把 dispatcher 队列跑空（动画/标脏重建都在队列里）。
    public static async Task Settle(int frames = 12, int delayMs = 40)
    {
        for (int i = 0; i < frames; i++)
        {
            await Task.Delay(delayMs);
            Dispatcher.UIThread.RunJobs();
            AvaloniaHeadlessPlatform.ForceRenderTimerTick();
        }
    }

    public void Shoot(TopLevel top, string name, Rect? crop = null, IReadOnlyList<Callout>? callouts = null)
    {
        var frame = top.CaptureRenderedFrame();
        if (frame == null)
        {
            Console.WriteLine($"[skip] {name}: CaptureRenderedFrame returned null");
            return;
        }

        var srcRect = crop ?? new Rect(0, 0, frame.PixelSize.Width, frame.PixelSize.Height);
        srcRect = srcRect.Intersect(new Rect(0, 0, frame.PixelSize.Width, frame.PixelSize.Height));
        var size = new PixelSize(Math.Max(1, (int)Math.Round(srcRect.Width)), Math.Max(1, (int)Math.Round(srcRect.Height)));

        using var target = new RenderTargetBitmap(size, new Vector(96, 96));
        using (var ctx = target.CreateDrawingContext())
        {
            ctx.DrawImage(frame, srcRect, new Rect(0, 0, size.Width, size.Height));
            if (callouts != null)
                foreach (var c in callouts)
                    DrawCallout(ctx, c, srcRect);
        }

        Directory.CreateDirectory(OutDir);
        string path = Path.Combine(OutDir, name + ".png");
        target.Save(path);
        Saved.Add(path);
        Console.WriteLine($"[shot] {name}  {size.Width}x{size.Height}");
    }

    // 标注配色：外描边用深色垫底，保证在亮/暗画面上都看得清。
    static readonly Color Accent = Color.FromRgb(0x4F, 0x9C, 0xF5);
    static readonly Color Ink = Color.FromRgb(0x0C, 0x11, 0x1B);

    static void DrawCallout(DrawingContext ctx, Callout callout, Rect srcRect)
    {
        var area = new Rect(
            callout.Area.X - srcRect.X, callout.Area.Y - srcRect.Y,
            callout.Area.Width, callout.Area.Height);

        if (callout.Box)
        {
            ctx.DrawRectangle(null, new Pen(new SolidColorBrush(Ink, 0.85), 4), area.Inflate(1), 3);
            ctx.DrawRectangle(null, new Pen(new SolidColorBrush(Accent), 2), area, 3);
        }

        // 编号气泡：默认贴在框的指定角内侧；小目标改骑在左上角外沿，并把气泡也缩小。
        bool small = callout.At == CalloutAt.OutsideCorner;
        double r = small ? 10 : 15;
        const double inset = 3;
        Point center;
        if (small)
        {
            center = new Point(area.X, area.Y);
        }
        else
        {
            double cx = callout.At is CalloutAt.TopLeft or CalloutAt.BottomLeft
                ? area.X + r + inset : area.Right - r - inset;
            double cy = callout.At is CalloutAt.TopLeft or CalloutAt.TopRight
                ? area.Y + r + inset : area.Bottom - r - inset;
            center = new Point(cx, cy);
        }
        ctx.DrawEllipse(new SolidColorBrush(Ink), new Pen(new SolidColorBrush(Accent), 2), center, r, r);

        var text = new FormattedText(callout.Label, CultureInfo.InvariantCulture, FlowDirection.LeftToRight,
            new Typeface(FontFamily.Default, FontStyle.Normal, FontWeight.Bold), small ? 13 : 17, new SolidColorBrush(Colors.White));
        ctx.DrawText(text, new Point(center.X - text.Width / 2, center.Y - text.Height / 2));
    }

    // ==== 控件定位：标注锚点一律从实测边界来 ====

    public static Rect BoundsIn(Visual visual, Visual root)
    {
        var origin = visual.TranslatePoint(default, root) ?? default;
        return new Rect(origin, visual.Bounds.Size);
    }

    public static IEnumerable<T> Descendants<T>(Visual root) where T : Visual
    {
        foreach (var child in root.GetVisualChildren())
        {
            if (child is T typed)
                yield return typed;
            foreach (var nested in Descendants<T>(child))
                yield return nested;
        }
    }

    public static T? Find<T>(Visual root, Func<T, bool>? predicate = null) where T : Visual
        => Descendants<T>(root).FirstOrDefault(v => predicate == null || predicate(v));
}
