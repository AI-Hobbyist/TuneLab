#pragma once

#include "pluginterfaces/vst/ivstevents.h"
#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>

namespace TuneLab::Cubase
{
// Own the text before process() returns. Runtime noteId is not a persistent
// ARA/source identity; association and document edits belong to the consumer.
template <std::size_t MaxCodeUnits = 256> struct NoteTextEvent
{
    Steinberg::int32 noteId = -1;
    Steinberg::int32 sampleOffset = 0;
    double quarter = 0;
    std::uint64_t frame = 0;
    std::uint64_t revision = 0;
    Steinberg::int32 pitch = -1;
    double onsetQuarter = 0;
    Steinberg::uint32 length = 0;
    std::array<Steinberg::Vst::TChar, MaxCodeUnits + 1> text{};
};

// One audio-thread producer and one non-audio consumer. No allocation, CLR,
// host messages, locks or wake-up calls occur while copying the host payload.
template <std::size_t Capacity = 64, std::size_t MaxCodeUnits = 256> class NoteTextInput
{
    static_assert(Capacity > 1 && MaxCodeUnits > 0);
    static_assert(sizeof(Steinberg::Vst::TChar) == 2);
    static_assert(std::atomic<std::size_t>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

  public:
    using Record = NoteTextEvent<MaxCodeUnits>;

    std::size_t capture(Steinberg::Vst::IEventList *input, std::int32_t samples,
                        std::uint64_t revision = 0) noexcept
    {
        struct Unassociated
        {
            void observe(const Steinberg::Vst::Event &) noexcept
            {
            }
            void associate(Record &, const Steinberg::Vst::Event &, Steinberg::int32) noexcept
            {
            }
        } policy;
        return captureWith(input, samples, revision, policy);
    }

    template <typename Association>
    std::size_t captureWith(Steinberg::Vst::IEventList *input, std::int32_t samples,
                            std::uint64_t revision, Association &association) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        if (samples < 0)
            return 0;
        std::size_t accepted = 0;
        const auto count = input ? input->getEventCount() : 0;
        for (int32 index = 0; index < count; ++index)
        {
            Event source{};
            if (input->getEvent(index, source) != kResultOk)
                continue;
            association.observe(source);
            if (source.type != Event::kNoteExpressionTextEvent)
                continue;
            // The SDK leaves phoneme encoding unspecified. Do not treat its
            // reserved type, or any private type, as a lyrics protocol.
            if (source.noteExpressionText.typeId != kTextTypeID)
                continue;
            if (source.busIndex != 0 || source.sampleOffset < 0 ||
                (samples == 0 ? source.sampleOffset != 0 : source.sampleOffset >= samples) ||
                !std::isfinite(source.ppqPosition) || !validText(source.noteExpressionText))
            {
                rejected.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            const auto current = write.load(std::memory_order_relaxed);
            const auto next = (current + 1) % Capacity;
            if (next == read.load(std::memory_order_acquire))
            {
                lost.fetch_add(1, std::memory_order_relaxed);
                continue;
            }
            auto &record = events[current];
            record.noteId = source.noteExpressionText.noteId;
            record.sampleOffset = source.sampleOffset;
            record.quarter = source.ppqPosition;
            record.frame = frame + static_cast<std::uint64_t>(source.sampleOffset);
            record.revision = revision;
            record.length = source.noteExpressionText.textLen;
            record.pitch = -1;
            record.onsetQuarter = 0;
            association.associate(record, source, index);
            if (record.length != 0)
                std::copy_n(source.noteExpressionText.text, record.length, record.text.begin());
            record.text[record.length] = 0;
            write.store(next, std::memory_order_release);
            ++accepted;
        }
        frame += static_cast<std::uint64_t>(samples);
        return accepted;
    }

    bool pop(Record &record) noexcept
    {
        const auto current = read.load(std::memory_order_relaxed);
        if (current == write.load(std::memory_order_acquire))
            return false;
        record = events[current];
        read.store((current + 1) % Capacity, std::memory_order_release);
        return true;
    }

    // Consumer only. A detected loss requires discarding the uncertain prefix
    // before reconciling note associations, rather than applying partial text.
    void discardPending() noexcept
    {
        read.store(write.load(std::memory_order_acquire), std::memory_order_release);
    }

    std::uint64_t lostEvents() const noexcept
    {
        return lost.load(std::memory_order_relaxed);
    }

    std::uint64_t rejectedEvents() const noexcept
    {
        return rejected.load(std::memory_order_relaxed);
    }

  private:
    static bool validText(const Steinberg::Vst::NoteExpressionTextEvent &source) noexcept
    {
        if (source.noteId < 0 || source.textLen > MaxCodeUnits ||
            (source.textLen != 0 && !source.text))
            return false;
        for (Steinberg::uint32 index = 0; index < source.textLen; ++index)
        {
            const auto unit = static_cast<std::uint16_t>(source.text[index]);
            if (unit == 0 || (unit >= 0xdc00 && unit <= 0xdfff))
                return false;
            if (unit >= 0xd800 && unit <= 0xdbff)
            {
                if (++index >= source.textLen)
                    return false;
                const auto low = static_cast<std::uint16_t>(source.text[index]);
                if (low < 0xdc00 || low > 0xdfff)
                    return false;
            }
        }
        return true;
    }

    std::array<Record, Capacity> events{};
    std::atomic<std::size_t> read{0};
    std::atomic<std::size_t> write{0};
    std::atomic<std::uint64_t> lost{0};
    std::atomic<std::uint64_t> rejected{0};
    std::uint64_t frame = 0;
};
} // namespace TuneLab::Cubase
