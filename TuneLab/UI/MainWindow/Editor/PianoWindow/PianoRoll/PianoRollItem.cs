using Avalonia;
using Avalonia.Media;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using TuneLab.Configs;
using TuneLab.Extensions;
using TuneLab.Foundation;
using TuneLab.GUI;
using TuneLab.Utils;

using Point = Avalonia.Point;

namespace TuneLab.UI;

internal partial class PianoRoll
{
    class PianoRollItem(PianoRoll pianoRoll) : Item
    {
        public PianoRoll PianoRoll => pianoRoll;
    }

    interface IKeyItem
    {
        int KeyNumber { get; }
    }

    class WhiteKeyItem(PianoRoll pianoRoll) : PianoRollItem(pianoRoll), IKeyItem
    {
        public required Rect Rect { get; set; }
        public required int KeyNumber { get; set; }
        public string? Label { get; set; }

        public override void Render(DrawingContext context)
        {
            var whiteKeyBrush = PianoRoll.HoverItem() == this ? HighLightKeyBrush : WhiteKeyBrush;
            context.FillRectangle(whiteKeyBrush, Rect, 4);
            DrawComfortDimming(context, PianoRoll, Rect, KeyNumber, isBlackKey: false);
            context.DrawLine(WhiteKeySeparatorPen, new Point(Rect.Left + 3, Rect.Bottom - 0.5), new Point(Rect.Right, Rect.Bottom - 0.5));
            if (!string.IsNullOrEmpty(Label) && Rect.Height >= 11)
            {
                var textBrush = PianoRoll.HoverItem() == this ? HoverTextBrush : WhiteKeyTextBrush;
                DrawPianoKeyLabel(context, Rect, textBrush, KeyNumber, Label, Alignment.RightCenter, Alignment.RightCenter, new(-6, 0));
            }
        }

        public override bool Raycast(Point point)
        {
            return Rect.Contains(point);
        }

        static readonly IBrush WhiteKeyBrush = new Color(255, 204, 204, 204).ToBrush();
        static readonly IPen WhiteKeySeparatorPen = new Pen(new Color(255, 154, 154, 164).ToBrush(), 1);
        static readonly IBrush WhiteKeyTextBrush = new Color(255, 24, 24, 30).ToBrush();
    }

    class BlackKeyItem(PianoRoll pianoRoll) : PianoRollItem(pianoRoll), IKeyItem
    {
        public required Rect Rect { get; set; }
        public required int KeyNumber { get; set; }
        public string? Label { get; set; }

        public override void Render(DrawingContext context)
        {
            var blackKeyBrush = PianoRoll.HoverItem() == this ? HighLightKeyBrush : BlackKeyBrush;
            context.FillRectangle(blackKeyBrush, Rect);
            DrawComfortDimming(context, PianoRoll, Rect, KeyNumber, isBlackKey: true);
            if (!string.IsNullOrEmpty(Label) && Rect.Height >= 11)
            {
                var textBrush = PianoRoll.HoverItem() == this ? HoverTextBrush : BlackKeyTextBrush;
                DrawPianoKeyLabel(context, Rect, textBrush, KeyNumber, Label, Alignment.Center, Alignment.Center);
            }
        }

        public override bool Raycast(Point point)
        {
            return Rect.Contains(point);
        }

        static readonly IBrush BlackKeyBrush = Style.BACK.ToBrush();
        static readonly IBrush BlackKeyTextBrush = Style.LIGHT_WHITE.ToBrush();
    }

    class TextItem(PianoRoll pianoRoll) : PianoRollItem(pianoRoll)
    {
        public required string Text { get; set; }

        public double Bottom { get; set; }

        public override void Render(DrawingContext context)
        {
            context.DrawString(Text, new Rect(0, Bottom - 24, PianoRoll.Bounds.Width, 24), textBrush, 12, Alignment.RightCenter, Alignment.RightCenter, new(-6, 0));
        }

        static readonly IBrush textBrush = Brushes.Black;
    }

    static IBrush HighLightKeyBrush = Style.DefaultTrackColor.ToBrush();
    static readonly IBrush HoverTextBrush = Brushes.White;

    static void DrawComfortDimming(DrawingContext context, PianoRoll pianoRoll, Rect rect, int keyNumber, bool isBlackKey)
    {
        double opacity = ComfortDimmingOpacity(pianoRoll, keyNumber, isBlackKey);
        if (opacity > 0)
            context.FillRectangle(Colors.Black.Opacity(opacity).ToBrush(), rect);
    }

    static double ComfortDimmingOpacity(PianoRoll pianoRoll, int keyNumber, bool isBlackKey)
    {
        var range = pianoRoll.ComfortRange;
        if (range == null || pianoRoll.HoverItem() is IKeyItem item && item.KeyNumber == keyNumber)
            return 0;

        return range.LevelOf(keyNumber) switch
        {
            ComfortPitchLevel.Available => isBlackKey ? 0.06 : 0.09,
            ComfortPitchLevel.Weak => isBlackKey ? 0.12 : 0.18,
            ComfortPitchLevel.Outside => isBlackKey ? 0.28 : 0.36,
            _ => 0,
        };
    }

    static double KeyLabelFontSize(Rect rect)
    {
        if (rect.Height < 14)
            return 9;
        if (rect.Height < 18)
            return 10;
        return 11;
    }

    static void DrawPianoKeyLabel(DrawingContext context, Rect rect, IBrush brush, int keyNumber, string label, int textAlignment, int rectAlignment, Point offset = default)
    {
        if (PianoKeyLabel.IsNumbered && PianoKeyLabel.TryGetNumberedPitchLabelParts(keyNumber, out var number, out int octaveOffset))
        {
            // 字体路径：连字只覆盖 ±2 个八度（1'' 到 1,,），且简谱字形（数字+八度点）字号更大、
            // 需要的纵向空间更多；不满足时退回手动画点（DrawNumberedDotLabel）。
            if (Math.Abs(octaveOffset) <= PianoKeyLabel.JianpuMaxOctaveOffset && CanDrawJianpuLabel(rect, octaveOffset))
            {
                DrawJianpuLabel(context, rect, brush, number, octaveOffset, textAlignment, rectAlignment, offset);
                return;
            }
            if (CanDrawNumberedDotLabel(rect, octaveOffset))
            {
                DrawNumberedDotLabel(context, rect, brush, number, octaveOffset, textAlignment, rectAlignment, offset);
                return;
            }
        }

        context.DrawString(label, rect, brush, KeyLabelFontSize(rect), textAlignment, rectAlignment, offset);
    }

    // 简谱音高标签：直接以 jianpu-ascii-font 的 ASCII 连字写法渲染——数字 1-7 为音符，' 高八度（点在上）、
    // , 低八度（点在下），# 为升号。由字体连字合成带八度点的规范简谱字形，比手动画点更标准、随字号缩放。
    static void DrawJianpuLabel(DrawingContext context, Rect rect, IBrush brush, string number, int octaveOffset, int textAlignment, int rectAlignment, Point offset)
    {
        string text = octaveOffset > 0
            ? number + new string('\'', octaveOffset)
            : octaveOffset < 0
                ? number + new string(',', -octaveOffset)
                : number;

        context.DrawString(text, rect, brush, JianpuLabelFontSize(rect), textAlignment, rectAlignment, offset, PianoKeyLabel.JianpuTypeface);
    }

    // 简谱字号：明显大于普通标签——字体的八度点是 5% em 的小圆点，字号小了几乎看不见。
    // 档位随键高：小键 14、常规黑键（≈28px）17、白键（≈48px）21。
    static double JianpuLabelFontSize(Rect rect)
    {
        if (rect.Height < 24)
            return 14;
        if (rect.Height < 34)
            return 17;
        return 21;
    }

    // 简谱字体的行高 ≈1.5em（字形盒 950/550 单位），加上八度点占位——需要的纵向空间比普通标签大。
    static bool CanDrawJianpuLabel(Rect rect, int octaveOffset)
    {
        return rect.Height >= JianpuLabelFontSize(rect) * 1.5 + Math.Abs(octaveOffset) * 2.5;
    }

    static bool CanDrawNumberedDotLabel(Rect rect, int octaveOffset)
    {
        int dotCount = Math.Abs(octaveOffset);
        if (dotCount == 0)
            return rect.Height >= 14;
        return rect.Height >= NumberedDotLabelMinHeight + dotCount * NumberedDotStep;
    }

    static void DrawNumberedDotLabel(DrawingContext context, Rect rect, IBrush brush, string number, int octaveOffset, int textAlignment, int rectAlignment, Point offset)
    {
        int dotCount = Math.Abs(octaveOffset);
        double fontSize = KeyLabelFontSize(rect);
        double textYOffset = dotCount == 0 ? 0 : (octaveOffset > 0 ? NumberedDotTextShift : -NumberedDotTextShift);
        context.DrawString(number, rect, brush, fontSize, textAlignment, rectAlignment, new(offset.X, offset.Y + textYOffset));

        if (dotCount == 0)
            return;

        double dotX = textAlignment == Alignment.Center ? rect.Center.X : rect.Right + offset.X - NumberedDotRightInset;
        double firstDotY = octaveOffset > 0
            ? rect.Center.Y - fontSize * 0.72 - (dotCount - 1) * NumberedDotStep
            : rect.Center.Y + fontSize * 0.72;

        for (int i = 0; i < dotCount; i++)
        {
            double dotY = firstDotY + i * NumberedDotStep;
            context.DrawEllipse(brush, null, new Point(dotX, dotY), NumberedDotRadius, NumberedDotRadius);
        }
    }

    static string? KeyLabel(int keyNumber)
    {
        if (!Settings.ShowAllPianoKeyLabels)
            return null;
        return PianoKeyLabel.IsNumbered
            ? PianoKeyLabel.NumberedPitchLabel(keyNumber)
            : PianoKeyLabel.PitchNameLabel(keyNumber);
    }

    const double NumberedDotRadius = 1.35;
    const double NumberedDotStep = 4;
    const double NumberedDotTextShift = 2.5;
    const double NumberedDotRightInset = 13;
    const double NumberedDotLabelMinHeight = 18;
}
