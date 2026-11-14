#pragma once

#include "pluginterfaces/base/ibstream.h"
#include <array>

namespace TuneLab::Cubase
{

inline Steinberg::tresult writeProbeState(Steinberg::IBStream *stream)
{
    if (!stream)
    {
        return Steinberg::kInvalidArgument;
    }
    std::array<unsigned char, 8> marker{'T', 'L', 'S', 'T', 'A', 'T', 'E', 1};
    Steinberg::int32 written = 0;
    const auto result = stream->write(marker.data(), marker.size(), &written);
    return result == Steinberg::kResultOk && written == marker.size() ? Steinberg::kResultOk
                                                                      : Steinberg::kResultFalse;
}

inline Steinberg::tresult readProbeState(Steinberg::IBStream *stream)
{
    if (!stream)
    {
        return Steinberg::kInvalidArgument;
    }
    const std::array<unsigned char, 8> expected{'T', 'L', 'S', 'T', 'A', 'T', 'E', 1};
    std::array<unsigned char, 8> marker{};
    Steinberg::int32 read = 0;
    const auto result = stream->read(marker.data(), marker.size(), &read);
    return result == Steinberg::kResultOk && read == marker.size() && marker == expected
               ? Steinberg::kResultOk
               : Steinberg::kResultFalse;
}

} // namespace TuneLab::Cubase
