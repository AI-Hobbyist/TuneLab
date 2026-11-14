#pragma once

#include "pluginterfaces/vst/ivstprocesscontext.h"
#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>

namespace TuneLab::Cubase
{
struct HostTimeSnapshot
{
    std::uint32_t flags = 0;
    std::int64_t samplePosition = 0;
    double sampleRate = 0;
    double quarterNotes = std::numeric_limits<double>::quiet_NaN();
    double tempo = std::numeric_limits<double>::quiet_NaN();
    double barQuarterNotes = std::numeric_limits<double>::quiet_NaN();
    std::int32_t numerator = 0;
    std::int32_t denominator = 0;
};

// Single audio-thread writer, control-thread readers. This is an observation
// of one process block, not a tempo map. All fields are atomic, so a reader
// racing the writer has no data race; SC sequence checks reject mixed blocks.
class HostTimeMailbox
{
    static_assert(std::atomic<double>::is_always_lock_free);
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);

  public:
    void publish(const Steinberg::Vst::ProcessContext *context) noexcept
    {
        using Context = Steinberg::Vst::ProcessContext;
        HostTimeSnapshot snapshot;
        if (context && std::isfinite(context->sampleRate) && context->sampleRate > 0)
        {
            snapshot.flags = context->state;
            snapshot.samplePosition = context->projectTimeSamples;
            snapshot.sampleRate = context->sampleRate;
            if ((snapshot.flags & Context::kProjectTimeMusicValid) &&
                std::isfinite(context->projectTimeMusic))
                snapshot.quarterNotes = context->projectTimeMusic;
            else
                snapshot.flags &= ~Context::kProjectTimeMusicValid;
            if ((snapshot.flags & Context::kTempoValid) && std::isfinite(context->tempo) &&
                context->tempo > 0)
                snapshot.tempo = context->tempo;
            else
                snapshot.flags &= ~Context::kTempoValid;
            if ((snapshot.flags & Context::kBarPositionValid) &&
                std::isfinite(context->barPositionMusic))
                snapshot.barQuarterNotes = context->barPositionMusic;
            else
                snapshot.flags &= ~Context::kBarPositionValid;
            if ((snapshot.flags & Context::kTimeSigValid) && context->timeSigNumerator > 0 &&
                context->timeSigDenominator > 0)
            {
                snapshot.numerator = context->timeSigNumerator;
                snapshot.denominator = context->timeSigDenominator;
            }
            else
                snapshot.flags &= ~Context::kTimeSigValid;
        }
        sequence.fetch_add(1);
        flags.store(snapshot.flags);
        samplePosition.store(snapshot.samplePosition);
        sampleRate.store(snapshot.sampleRate);
        quarterNotes.store(snapshot.quarterNotes);
        tempo.store(snapshot.tempo);
        barQuarterNotes.store(snapshot.barQuarterNotes);
        numerator.store(snapshot.numerator);
        denominator.store(snapshot.denominator);
        sequence.fetch_add(1);
    }

    bool tryRead(HostTimeSnapshot &snapshot) const noexcept
    {
        for (int attempt = 0; attempt < 3; ++attempt)
        {
            const auto before = sequence.load();
            if (before & 1)
                continue;
            HostTimeSnapshot candidate;
            candidate.flags = flags.load();
            candidate.samplePosition = samplePosition.load();
            candidate.sampleRate = sampleRate.load();
            candidate.quarterNotes = quarterNotes.load();
            candidate.tempo = tempo.load();
            candidate.barQuarterNotes = barQuarterNotes.load();
            candidate.numerator = numerator.load();
            candidate.denominator = denominator.load();
            if (sequence.load() == before)
            {
                snapshot = candidate;
                return true;
            }
        }
        return false;
    }

  private:
    std::atomic<std::uint64_t> sequence{0};
    std::atomic<std::uint32_t> flags{0};
    std::atomic<std::int64_t> samplePosition{0};
    std::atomic<double> sampleRate{0};
    std::atomic<double> quarterNotes{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<double> tempo{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<double> barQuarterNotes{std::numeric_limits<double>::quiet_NaN()};
    std::atomic<std::int32_t> numerator{0}, denominator{0};
};
} // namespace TuneLab::Cubase
