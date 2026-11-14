#include "probe_state.h"
#include "project_state_stream.h"
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
    std::vector<std::uint8_t> full{'T', 'L', 'C', 'U', 'B', 'A', 'S', 'E', 1,
                                   0,   0,   0,   1,   0,   0,   0,   0xA0};
    Steinberg::MemoryStream projectStream;
    std::vector<std::uint8_t> restored;
    if (TuneLab::Cubase::writeProjectState(&projectStream, full) != Steinberg::kResultOk)
    {
        return 5;
    }
    projectStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    if (TuneLab::Cubase::readProjectState(&projectStream, restored) != Steinberg::kResultOk ||
        restored != full)
    {
        return 6;
    }
    projectStream.getData()[8] = 3;
    projectStream.seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    if (TuneLab::Cubase::readProjectState(&projectStream, restored) == Steinberg::kResultOk ||
        restored != full)
    {
        return 7;
    }
    Steinberg::MemoryStream shortProject(full.data(),
                                         static_cast<Steinberg::TSize>(full.size() - 1));
    if (TuneLab::Cubase::readProjectState(&shortProject, restored) == Steinberg::kResultOk ||
        restored != full)
    {
        return 8;
    }
    std::cout << "Probe and full state framing round trips and invalid-state preservation passed\n";
    return 0;
}
