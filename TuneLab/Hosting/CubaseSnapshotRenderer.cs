using System;
using System.Threading;

namespace TuneLab.Hosting;

internal sealed record CubaseRenderedAudio(int SampleRate, float[] Interleaved);

// One-shot callers retain their existing contract. Native workers can own a
// CubaseRenderSession across revisions without sharing a live UI document.
internal static class CubaseSnapshotRenderer
{
    public static CubaseRenderedAudio Render(byte[] state, TimeSpan timeout,
        CancellationToken cancellation = default)
    {
        using var session = new CubaseRenderSession();
        return session.Render(state, timeout, cancellation);
    }

    public static CubaseRenderedAudio RenderPreview(byte[] state, int pitch, TimeSpan timeout,
        CancellationToken cancellation = default)
    {
        using var session = new CubaseRenderSession();
        return session.Render(state, timeout, cancellation, pitch);
    }
}
