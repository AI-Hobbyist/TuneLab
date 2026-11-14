using System;
using System.Linq;
using TuneLab.Extensions.Formats.TLP;
using TuneLab.SDK;

namespace TuneLab.Hosting;

// A disposable audition score, separate from the saved song. Its one-second
// note is synthesized at the requested pitch, without resampling a piano tone
// or changing the user's notes, tempo, effects, or undo history.
internal static class CubaseMidiPreview
{
    public static NativeProjectFile CreateScore(byte[] state, int pitch)
    {
        if (pitch is < 0 or > 127)
        {
            throw new ArgumentOutOfRangeException(nameof(pitch));
        }
        var source = CubaseProjectState.Decode(state).Project.Tracks
            .SelectMany(track => track.Parts).OfType<MidiPartInfo>().FirstOrDefault()
            ?? throw new InvalidOperationException("MIDI preview requires a selected sound source part.");
        var template = source.Notes.FirstOrDefault();
        var part = new MidiPartInfo
        {
            Name = "MIDI preview",
            Pos = 0,
            StartOffset = 0,
            EndOffset = 960,
            SoundSource = source.SoundSource,
            Properties = source.Properties,
            Notes =
            {
                new NoteInfo
                {
                    Pos = 0,
                    Dur = 960,
                    Pitch = pitch,
                    Lyric = template?.Lyric ?? string.Empty,
                    Pronunciation = template?.Pronunciation ?? string.Empty,
                    Properties = template?.Properties ?? TuneLab.Foundation.PropertyObject.Empty
                }
            }
        };
        var preview = new NativeProjectFile();
        preview.Project.Tempos = [new TempoInfo { Pos = 0, Bpm = 120 }];
        preview.Project.Tracks.Add(new TrackInfo { Parts = { part } });
        return preview;
    }
}
