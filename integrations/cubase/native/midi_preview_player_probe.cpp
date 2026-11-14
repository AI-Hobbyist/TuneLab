#include "midi_preview_player.h"
#include "public.sdk/source/vst/hosting/eventlist.h"
#include <iostream>

using namespace TuneLab::Cubase;
using namespace Steinberg::Vst;

namespace
{
std::atomic<bool> allowRender{false}, started{false}, cancelled{false};
std::atomic<bool> ignoreCancellation{false}, allowNewRender{false};
std::atomic<int> renderCalls{0};
std::array<float, 128> samples{};
std::array<float, 128> newSamples{};

template <typename Predicate> bool waitUntil(Predicate predicate)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(2);
    while (!predicate() && std::chrono::steady_clock::now() < end)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    return predicate();
}

ManagedRuntime fakeRuntime()
{
    ManagedRuntime runtime;
    runtime.createRenderRequest = [](auto *handle) -> std::int32_t {
        cancelled.store(false);
        *handle = 1;
        return 0;
    };
    runtime.cancelRenderRequest = [](auto) -> std::int32_t {
        cancelled.store(true);
        return 0;
    };
    runtime.releaseRenderRequest = [](auto) -> std::int32_t { return 0; };
    runtime.renderPreview = [](const std::uint8_t *state, auto, auto, auto, auto,
                               auto *audio) -> std::int32_t {
        started.store(true);
        renderCalls.fetch_add(1);
        const bool newScore = state[0] == 2;
        while (!(newScore ? allowNewRender.load() : allowRender.load()) &&
               !(cancelled.load() && !ignoreCancellation.load()))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        const bool aborted = cancelled.load() && !ignoreCancellation.load();
        *audio = aborted ? 0 : (newScore ? 2 : 1);
        return aborted ? 6 : 0;
    };
    runtime.getRenderedAudio = [](auto handle, auto *rate, auto *frames,
                                  const float **audio) -> std::int32_t {
        *rate = 48000;
        *frames = 64;
        *audio = handle == 2 ? newSamples.data() : samples.data();
        return 0;
    };
    runtime.releaseRenderedAudio = [](auto) -> std::int32_t { return 0; };
    return runtime;
}
} // namespace

int main()
{
    // Thirty-two silent leading frames expose a cursor that incorrectly
    // advances only when copyStereo reports nonzero samples.
    std::fill(samples.begin() + 64, samples.end(), 0.5f);
    std::fill(newSamples.begin() + 64, newSamples.end(), 0.25f);
    std::array<float, 16> left{}, right{};
    EventList events;
    Event on{};
    on.type = Event::kNoteOnEvent;
    on.noteOn = {0, 60, 0, 1, 0, 1};
    Event off{};
    off.type = Event::kNoteOffEvent;
    off.noteOff = {0, 60, 0, 1, 0};
    {
        MidiPreviewPlayer player(fakeRuntime());
        player.update({1}, 48000);
        const auto process = [&] {
            left.fill(0);
            right.fill(0);
            return player.process(&events, left.data(), right.data(), 16, 48000);
        };
        events.addEvent(on);
        process();
        events.clear();
        if (!waitUntil([] { return started.load(); }))
            return 1;
        events.addEvent(off);
        process();
        events.clear();
        allowRender.store(true);
        if (!waitUntil([&] { return player.result() == 0; }))
            return 2;
        for (int index = 0; index < 8; ++index)
        {
            if (process()) // Completed cold render cannot revive a released key.
                return 3;
        }
        events.addEvent(on);
        if (process())
            return 4;
        events.clear();
        if (process() || !process() || left[0] != 0.5f)
            return 5;
        off.sampleOffset = 7;
        events.addEvent(off);
        if (!process() || left[6] != 0.5f || left[7] != 0 || left[15] != 0)
            return 6;
        events.clear();
        events.addEvent(on);
        process();
        events.clear();
        off.sampleOffset = 0;
        events.addEvent(off);
        player.process<float>(&events, nullptr, nullptr, 0, 48000);
        events.clear();
        if (process())
            return 9;

        // Two same-pitch IDs share immutable audio, but keep independent
        // cursors, velocities and release boundaries.
        player.reset();
        auto harmony = on;
        harmony.noteOn.noteId = 2;
        harmony.noteOn.velocity = 0.5f;
        events.addEvent(on);
        events.addEvent(harmony);
        process();
        events.clear();
        if (process() || !process() || left[0] != 0.75f || right[15] != 0.75f)
            return 10;
        off.sampleOffset = 7;
        events.addEvent(off);
        if (!process() || left[6] != 0.75f || right[6] != 0.75f || left[7] != 0.25f ||
            right[15] != 0.25f)
            return 11;
        events.clear();
        player.reset();
        std::array<double, 16> doubleLeft{}, doubleRight{};
        events.addEvent(on);
        player.process(&events, doubleLeft.data(), doubleRight.data(), 16, 48000);
        events.clear();
        player.process(&events, doubleLeft.data(), doubleRight.data(), 16, 48000);
        doubleLeft.fill(0);
        doubleRight.fill(0);
        if (!player.process(&events, doubleLeft.data(), doubleRight.data(), 16, 48000) ||
            doubleLeft[0] != 0.5 || doubleRight[15] != 0.5)
            return 12;

        // Force a genuine in-flight old revision to ignore cancellation and
        // return audio anyway. It must remain unreadable in the new revision.
        allowRender.store(false);
        ignoreCancellation.store(true);
        started.store(false);
        player.update({1}, 48000);
        events.addEvent(on);
        process();
        events.clear();
        if (!waitUntil([] { return started.load(); }))
            return 13;
        const int oldCall = renderCalls.load();
        player.update({2}, 48000);
        events.addEvent(on);
        process();
        events.clear();
        allowRender.store(true);
        if (!waitUntil([&] { return renderCalls.load() > oldCall; }) || process())
            return 14;
        allowNewRender.store(true);
        if (!waitUntil([&] { return player.result() == 0; }))
            return 15;
        process();
        process();
        if (!process() || left[0] != 0.25f || right[15] != 0.25f)
            return 16;
        ignoreCancellation.store(false);
        player.reset();
        // Close while a different pitch is actually in flight. Destructor
        // cancellation must unblock and join its worker.
        allowRender.store(false);
        started.store(false);
        player.update({1}, 48000);
        on.noteOn.pitch = 61;
        on.noteOn.noteId = 2;
        events.addEvent(on);
        process();
        if (!waitUntil([] { return started.load(); }))
            return 7;
    }
    if (!cancelled.load())
        return 8;
    std::cout << "Preview fixture: released cold key stays silent, leading zeros advance, "
                 "polyphonic ID/velocity mixing, double output, stale revision rejection, "
                 "sample-offset Note Off and in-flight close cancellation passed\n";
    return 0;
}
