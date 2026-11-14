#pragma once

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "project_state_stream.h"
#include <atomic>
#include <cmath>

namespace TuneLab::Cubase
{
inline constexpr Steinberg::Vst::ParamID bypassParameterId = 0;

// Optional versioned trailer after the existing project envelope. Older states
// end at the project payload and restore the default (not bypassed).
inline Steinberg::tresult readInstrumentParameters(Steinberg::IBStream *stream, bool &bypass)
{
    using namespace Steinberg;
    std::array<std::uint8_t, 12> bytes{};
    int32 count = 0;
    const auto result = stream->read(bytes.data(), 1, &count);
    if (count == 0 && (result == kResultOk || result == kResultFalse))
    {
        bypass = false;
        return kResultOk;
    }
    const std::array<std::uint8_t, 8> magic{'T', 'L', 'V', 'S', 'T', 'P', '0', '1'};
    if (result != kResultOk || count != 1 || !readExact(stream, bytes.data() + 1, 11) ||
        !std::equal(magic.begin(), magic.end(), bytes.begin()) || bytes[8] > 1 || bytes[9] != 0 ||
        bytes[10] != 0 || bytes[11] != 0)
        return kResultFalse;
    bypass = bytes[8] != 0;
    return kResultOk;
}

inline Steinberg::tresult writeInstrumentParameters(Steinberg::IBStream *stream, bool bypass)
{
    std::array<std::uint8_t, 12> bytes{
        'T', 'L', 'V', 'S', 'T', 'P', '0', '1', static_cast<std::uint8_t>(bypass), 0, 0, 0};
    Steinberg::int32 position = 0;
    while (position < static_cast<Steinberg::int32>(bytes.size()))
    {
        Steinberg::int32 written = 0;
        const auto remaining = static_cast<Steinberg::int32>(bytes.size()) - position;
        if (stream->write(bytes.data() + position, remaining, &written) != Steinberg::kResultOk ||
            written <= 0 || written > remaining)
            return Steinberg::kResultFalse;
        position += written;
    }
    return Steinberg::kResultOk;
}

class InstrumentParameters
{
    static_assert(std::atomic<double>::is_always_lock_free);

  public:
    void restore(bool value) noexcept
    {
        normalized.store(value ? 1.0 : 0.0);
    }
    bool bypassed() const noexcept
    {
        return normalized.load() >= 0.5;
    }

    void process(Steinberg::Vst::ProcessData &data) noexcept
    {
        using namespace Steinberg;
        using namespace Steinberg::Vst;
        double value = normalized.load();
        int32 previous = -1;
        int32 begin = 0;
        if (data.inputParameterChanges)
        {
            for (int32 index = 0; index < data.inputParameterChanges->getParameterCount(); ++index)
            {
                auto *queue = data.inputParameterChanges->getParameterData(index);
                if (!queue || queue->getParameterId() != bypassParameterId)
                    continue;
                for (int32 point = 0; point < queue->getPointCount(); ++point)
                {
                    int32 offset = 0;
                    double next = 0;
                    if (queue->getPoint(point, offset, next) != kResultOk || offset < begin ||
                        (data.numSamples == 0 ? offset != 0 : offset >= data.numSamples) ||
                        !std::isfinite(next) || next < 0 || next > 1)
                        continue;
                    apply(data, begin, offset + (data.numSamples > 0 ? 1 : 0), previous, value,
                          offset, next);
                    begin = offset + (data.numSamples > 0 ? 1 : 0);
                    previous = offset;
                    value = next;
                }
            }
        }
        apply(data, begin, data.numSamples, 0, value, 0, value);
        normalized.store(value);
        for (int32 bus = 0; bus < data.numOutputs; ++bus)
        {
            auto &output = data.outputs[bus];
            for (int32 channel = 0; channel < output.numChannels; ++channel)
            {
                bool silent = true;
                for (int32 sample = 0; sample < data.numSamples; ++sample)
                {
                    silent = silent && (data.symbolicSampleSize == kSample32
                                            ? output.channelBuffers32[channel][sample] == 0
                                            : output.channelBuffers64[channel][sample] == 0);
                }
                if (silent)
                    output.silenceFlags |= uint64(1) << channel;
            }
        }
    }

  private:
    static void apply(Steinberg::Vst::ProcessData &data, int begin, int end, int from, double value,
                      int to, double next) noexcept
    {
        for (int sample = begin; sample < end; ++sample)
        {
            const double current =
                to == from ? value : value + (next - value) * (sample - from) / (to - from);
            if (current < 0.5)
                continue;
            for (int bus = 0; bus < data.numOutputs; ++bus)
            {
                for (int channel = 0; channel < data.outputs[bus].numChannels; ++channel)
                {
                    if (data.symbolicSampleSize == Steinberg::Vst::kSample32)
                    {
                        data.outputs[bus].channelBuffers32[channel][sample] = 0;
                    }
                    else
                    {
                        data.outputs[bus].channelBuffers64[channel][sample] = 0;
                    }
                }
            }
        }
    }
    std::atomic<double> normalized{0};
};
} // namespace TuneLab::Cubase
