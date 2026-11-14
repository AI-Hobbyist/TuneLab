#include "probe_state.h"
#include "public.sdk/source/common/memorystream.h"
#include <iostream>

int main()
{
    Steinberg::MemoryStream stream;
    if (TuneLab::Cubase::writeProbeState(&stream) != Steinberg::kResultOk)
    {
        std::cerr << "State write failed\n";
        return 1;
    }
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    if (TuneLab::Cubase::readProbeState(&stream) != Steinberg::kResultOk)
    {
        std::cerr << "State round trip failed\n";
        return 2;
    }

    stream.getData()[7] = 2;
    stream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    if (TuneLab::Cubase::readProbeState(&stream) == Steinberg::kResultOk)
    {
        std::cerr << "Unknown version was accepted\n";
        return 3;
    }

    Steinberg::MemoryStream truncated(stream.getData(), 3);
    if (TuneLab::Cubase::readProbeState(&truncated) == Steinberg::kResultOk)
    {
        std::cerr << "Truncated state was accepted\n";
        return 4;
    }
    std::cout << "State round trip, unknown version and truncation checks passed\n";
    return 0;
}
