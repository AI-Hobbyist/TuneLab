using Avalonia.Controls;
using Avalonia.Input;
using Avalonia.Media;
using Avalonia;
using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;
using TuneLab.GUI;
using TuneLab.GUI.Components;
using TuneLab.GUI.Input;
using TuneLab.Utils;
using TuneLab.Foundation;
using TuneLab.Configs;
using TuneLab.Data;
using TuneLab.Extensions;

namespace TuneLab.UI;

internal partial class PianoRoll : View
{
    public interface IDependency
    {
        PitchAxis PitchAxis { get; }
        IHolder<IMidiPart> PartHolder { get; }
    }

    public PianoRoll(IDependency dependency)
    {
        mDependency = dependency;

        mMiddleDragOperation = new(this);
        mPlayKeySampleOperation = new(this);

        PitchAxis.AxisChanged += InvalidateVisual;
        Settings.ShowAllPianoKeyLabels.Modified.Subscribe(InvalidateVisual, s);
        Settings.PianoKeyLabelStyle.Modified.Subscribe(InvalidateVisual, s);
        Settings.NumberedPianoKeyTonic.Modified.Subscribe(InvalidateVisual, s);
        mDependency.PartHolder.Modified.Subscribe(InvalidateVisual, s);
        mDependency.PartHolder.When(part => part.SoundSource.Modified).Subscribe(InvalidateVisual, s);
    }

    ~PianoRoll()
    {
        PitchAxis.AxisChanged -= InvalidateVisual;
        s.DisposeAll();
    }

    protected override void OnRender(DrawingContext context)
    {
        context.FillRectangle(Style.BACK.ToBrush(), this.Rect());
    }

    PitchAxis PitchAxis => mDependency.PitchAxis;
    SoundSourceComfortRange? ComfortRange => SoundSourceComfortRange.Resolve(mDependency.PartHolder.Value?.SoundSource);

    readonly IDependency mDependency;
    readonly DisposableManager s = new();
}
