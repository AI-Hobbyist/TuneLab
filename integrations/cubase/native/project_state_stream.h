#pragma once

#include "probe_state.h"
#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>

namespace TuneLab::Cubase
{
inline bool readExact(Steinberg::IBStream *stream, void *data, Steinberg::int32 length)
{
    auto *bytes = static_cast<std::uint8_t *>(data);
    while (length > 0)
    {
        Steinberg::int32 count = 0;
        if (stream->read(bytes, length, &count) != Steinberg::kResultOk || count <= 0 ||
            count > length)
        {
            return false;
        }
        bytes += count;
        length -= count;
    }
    return true;
}

inline Steinberg::tresult readProjectState(Steinberg::IBStream *stream,
                                           std::vector<std::uint8_t> &state)
{
    if (!stream)
    {
        return Steinberg::kInvalidArgument;
    }
    std::array<std::uint8_t, 8> magic{};
    if (!readExact(stream, magic.data(), 8))
    {
        return Steinberg::kResultFalse;
    }
    const std::array<std::uint8_t, 8> probe{'T', 'L', 'S', 'T', 'A', 'T', 'E', 1};
    if (magic == probe)
    {
        state.clear();
        return Steinberg::kResultOk;
    }
    const std::array<std::uint8_t, 8> expected{'T', 'L', 'C', 'U', 'B', 'A', 'S', 'E'};
    std::array<std::uint8_t, 8> fields{};
    if (magic != expected || !readExact(stream, fields.data(), 8) || fields[0] != 1 ||
        fields[1] != 0 || fields[2] != 0 || fields[3] != 0)
    {
        return Steinberg::kResultFalse;
    }
    const std::uint32_t length = std::uint32_t(fields[4]) | (std::uint32_t(fields[5]) << 8) |
                                 (std::uint32_t(fields[6]) << 16) |
                                 (std::uint32_t(fields[7]) << 24);
    if (length == 0 || length > (std::numeric_limits<Steinberg::int32>::max)() - 16)
    {
        return Steinberg::kResultFalse;
    }
    std::vector<std::uint8_t> candidate(16 + length);
    std::copy(magic.begin(), magic.end(), candidate.begin());
    std::copy(fields.begin(), fields.end(), candidate.begin() + 8);
    if (!readExact(stream, candidate.data() + 16, static_cast<Steinberg::int32>(length)))
    {
        return Steinberg::kResultFalse;
    }
    state = std::move(candidate);
    return Steinberg::kResultOk;
}

inline Steinberg::tresult writeProjectState(Steinberg::IBStream *stream,
                                            const std::vector<std::uint8_t> &state)
{
    if (state.empty())
    {
        return writeProbeState(stream);
    }
    if (!stream || state.size() > (std::numeric_limits<Steinberg::int32>::max)())
    {
        return Steinberg::kInvalidArgument;
    }
    auto *bytes = const_cast<std::uint8_t *>(state.data());
    auto remaining = static_cast<Steinberg::int32>(state.size());
    while (remaining > 0)
    {
        Steinberg::int32 count = 0;
        if (stream->write(bytes, remaining, &count) != Steinberg::kResultOk || count <= 0 ||
            count > remaining)
        {
            return Steinberg::kResultFalse;
        }
        bytes += count;
        remaining -= count;
    }
    return Steinberg::kResultOk;
}
} // namespace TuneLab::Cubase
