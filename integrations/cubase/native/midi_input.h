#pragma once

#include "pluginterfaces/vst/ivstevents.h"
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace TuneLab::Cubase
{
struct MidiNoteEvent
{
    bool on = false;
    std::int16_t channel = 0;
    std::int16_t pitch = 0;
    std::int32_t noteId = -1;
    float velocity = 0;
    float tuning = 0;
    std::int32_t sampleOffset = 0;
    std::uint64_t frame = 0;
    std::uint16_t flags = 0;
    std::uint64_t revision = 0;
};

// Worker-owned held-note identities. Note IDs take precedence over pitch;
// hosts without IDs pair repeated channel/pitch notes in onset order.
template <std::size_t Capacity = 64> class MidiHeldNotes
{
  public:
    struct Note
    {
        bool active = false;
        MidiNoteEvent onset;
        std::uint64_t order = 0;
    };

    bool apply(const MidiNoteEvent &event) noexcept
    {
        Note *match = nullptr;
        for (auto &note : held)
        {
            if (!note.active || note.onset.channel != event.channel)
            {
                continue;
            }
            const bool same = event.noteId >= 0 ? note.onset.noteId == event.noteId
                                                : note.onset.pitch == event.pitch;
            if (same && (!match || note.order < match->order))
            {
                match = &note;
            }
        }
        if (!event.on)
        {
            if (match)
            {
                match->active = false;
                return true;
            }
            return false;
        }
        // An unidentified repeated Note On is a separate held note, whereas
        // a repeated explicit identity restarts that identity.
        if (event.noteId < 0)
        {
            match = nullptr;
        }
        if (!match)
        {
            for (auto &note : held)
            {
                if (!note.active)
                {
                    match = &note;
                    break;
                }
            }
        }
        if (!match)
        {
            return false;
        }
        *match = Note{true, event, ++order};
        return true;
    }

    void clear() noexcept
    {
        for (auto &note : held)
        {
            note.active = false;
        }
    }

    const std::array<Note, Capacity> &notes() const noexcept
    {
        return held;
    }

  private:
    std::array<Note, Capacity> held{};
    std::uint64_t order = 0;
};

// One audio-thread producer and one preview-worker consumer. No allocation,
// host message, lock or wake-up syscall is required to transfer a note.
template <std::size_t Capacity = 256> class MidiInput
{
    static_assert(Capacity > 1);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

  public:
    bool push(const MidiNoteEvent &event) noexcept
    {
        const auto current = write.load(std::memory_order_relaxed);
        const auto next = (current + 1) % Capacity;
        if (next == read.load(std::memory_order_acquire))
        {
            lost.fetch_add(1, std::memory_order_relaxed);
            return false;
        }
        events[current] = event;
        write.store(next, std::memory_order_release);
        return true;
    }

    bool pop(MidiNoteEvent &event) noexcept
    {
        const auto current = read.load(std::memory_order_relaxed);
        if (current == write.load(std::memory_order_acquire))
        {
            return false;
        }
        event = events[current];
        read.store((current + 1) % Capacity, std::memory_order_release);
        return true;
    }

    // Consumer only. A lost Note Off requires clearing active preview voices
    // and discarding the queued prefix; loss must never leave a held voice.
    void discardPending() noexcept
    {
        read.store(write.load(std::memory_order_acquire), std::memory_order_release);
    }

    std::uint64_t lostEvents() const noexcept
    {
        return lost.load(std::memory_order_relaxed);
    }

    void capture(Steinberg::Vst::IEventList *input, std::int32_t samples) noexcept
    {
        capture(input, samples, 0, [](const auto &, bool) {});
    }

    template <typename Visitor>
    void capture(Steinberg::Vst::IEventList *input, std::int32_t samples, std::uint64_t revision,
                 Visitor visit) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (samples < 0)
        {
            return;
        }
        if (input)
        {
            const auto count = input->getEventCount();
            for (int32 index = 0; index < count; ++index)
            {
                Event source{};
                if (input->getEvent(index, source) != kResultOk || source.busIndex != 0 ||
                    source.sampleOffset < 0 ||
                    (samples == 0 ? source.sampleOffset != 0 : source.sampleOffset >= samples))
                {
                    continue;
                }
                MidiNoteEvent event;
                if (source.type == Event::kNoteOnEvent)
                {
                    event.on = true;
                    event.channel = source.noteOn.channel;
                    event.pitch = source.noteOn.pitch;
                    event.noteId = source.noteOn.noteId;
                    event.velocity = source.noteOn.velocity;
                    event.tuning = source.noteOn.tuning;
                }
                else if (source.type == Event::kNoteOffEvent)
                {
                    event.channel = source.noteOff.channel;
                    event.pitch = source.noteOff.pitch;
                    event.noteId = source.noteOff.noteId;
                    event.velocity = source.noteOff.velocity;
                    event.tuning = source.noteOff.tuning;
                }
                else
                {
                    continue;
                }
                if (event.channel < 0 || event.channel > 15 || event.pitch < 0 ||
                    event.pitch > 127 || !std::isfinite(event.velocity) || event.velocity < 0 ||
                    event.velocity > 1 || !std::isfinite(event.tuning))
                {
                    continue;
                }
                event.sampleOffset = source.sampleOffset;
                event.frame = frame + static_cast<std::uint64_t>(source.sampleOffset);
                event.flags = source.flags;
                event.revision = revision;
                const bool sent = push(event);
                visit(event, sent);
            }
        }
        // A monotonic preview clock continues while the host transport is
        // stopped, and is independent of seek/loop project-time jumps.
        frame += static_cast<std::uint64_t>(samples);
    }

  private:
    std::array<MidiNoteEvent, Capacity> events{};
    std::atomic<std::size_t> read{0};
    std::atomic<std::size_t> write{0};
    std::atomic<std::uint64_t> lost{0};
    std::uint64_t frame = 0;
};
} // namespace TuneLab::Cubase
