#pragma once

#include "midi_preview_worker.h"

namespace TuneLab::Cubase
{
// Held notes/cursors belong exclusively to the audio thread. The worker may
// finish a released note to warm its cache, but cannot start or revive a voice.
class MidiPreviewPlayer
{
  public:
    explicit MidiPreviewPlayer(ManagedRuntime runtime) : worker(std::move(runtime))
    {
    }

    void update(std::vector<std::uint8_t> state, int sampleRate)
    {
        worker.update(std::move(state), sampleRate);
    }

    void reset() noexcept
    {
        resetEpoch.fetch_add(1);
    }
    int result() const noexcept
    {
        return worker.result();
    }
    std::int64_t renderMicroseconds() const noexcept
    {
        return worker.renderMicroseconds();
    }

    template <typename Sample>
    bool process(Steinberg::Vst::IEventList *events, Sample *left, Sample *right, int samples,
                 int sampleRate) noexcept
    {
        const auto revision = worker.revision();
        const auto epoch = resetEpoch.load();
        if (revision != currentRevision || epoch != observedReset)
        {
            held.clear();
            currentRevision = revision;
            observedReset = epoch;
        }
        int position = 0;
        bool audible = false;
        worker.input.capture(events, samples, revision, [&](const auto &event, bool sent) {
            if (event.sampleOffset < position)
            {
                return;
            }
            audible = mix(left, right, position, event.sampleOffset, sampleRate) || audible;
            position = event.sampleOffset;
            if (!sent)
            {
                // In particular, a rejected Note Off must stop every held
                // voice immediately, even while the worker is synthesizing.
                held.clear();
                return;
            }
            held.apply(event);
        });
        return mix(left, right, position, samples, sampleRate) || audible;
    }

  private:
    template <typename Sample>
    bool mix(Sample *left, Sample *right, int begin, int end, int rate) noexcept
    {
        if (!left || !right)
        {
            return false;
        }
        constexpr int chunkSize = 128;
        std::array<Sample, chunkSize> scratchLeft{}, scratchRight{};
        bool audible = false;
        for (std::size_t index = 0; index < held.notes().size(); ++index)
        {
            const auto &note = held.notes()[index];
            if (!note.active)
            {
                continue;
            }
            if (orders[index] != note.order)
            {
                orders[index] = note.order;
                cursors[index] = 0;
            }
            for (int position = begin; position < end; position += chunkSize)
            {
                const int count = (std::min)(chunkSize, end - position);
                bool ready = false;
                worker.copy(note.onset.pitch, scratchLeft.data(), scratchRight.data(), count,
                            cursors[index], currentRevision, rate, ready);
                if (!ready)
                {
                    break;
                }
                for (int sample = 0; sample < count; ++sample)
                {
                    const auto l = scratchLeft[sample] * static_cast<Sample>(note.onset.velocity);
                    const auto r = scratchRight[sample] * static_cast<Sample>(note.onset.velocity);
                    left[position + sample] += l;
                    right[position + sample] += r;
                    audible = audible || l != 0 || r != 0;
                }
                cursors[index] += count;
            }
        }
        return audible;
    }

    MidiPreviewWorker worker;
    MidiHeldNotes<> held;
    std::array<std::uint64_t, 64> orders{};
    std::array<std::int64_t, 64> cursors{};
    std::uint64_t currentRevision = 0;
    std::atomic<std::uint64_t> resetEpoch{0};
    std::uint64_t observedReset = 0;
};
} // namespace TuneLab::Cubase
