#include "managed_runtime.h"
#include <array>
#include <atomic>
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <thread>

using namespace TuneLab::Cubase;

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: tunelab_concurrent_render_probe <managed-directory> <project-state>\n";
        return 1;
    }
    std::ifstream input(std::filesystem::path(argv[2]), std::ios::binary);
    std::vector<std::uint8_t> state((std::istreambuf_iterator<char>(input)), {});
    ManagedRuntime runtime;
    if (state.empty() || !runtime.initialize(argv[1]))
        return 2;
    struct Resources
    {
        ManagedRuntime &runtime;
        std::array<RenderRequestHandle, 2> requests{};
        std::array<AudioHandle, 2> audio{};
        ~Resources()
        {
            for (auto handle : audio)
                if (handle)
                    runtime.releaseRenderedAudio(handle);
            for (auto handle : requests)
                if (handle)
                    runtime.releaseRenderRequest(handle);
        }
    } resources{runtime};
    for (auto &request : resources.requests)
        if (runtime.createRenderRequest(&request) != 0)
            return 3;
    std::atomic<int> ready{0};
    std::atomic<bool> start{false};
    std::array<int, 2> results{-1, -1};
    const auto render = [&](int index) {
        ready.fetch_add(1);
        while (!start.load())
            std::this_thread::yield();
        if (index == 0)
        {
            results[index] =
                runtime.renderSnapshot(state.data(), static_cast<std::int32_t>(state.size()), 48000,
                                       resources.requests[index], &resources.audio[index]);
        }
        else
        {
            results[index] =
                runtime.renderPreview(state.data(), static_cast<std::int32_t>(state.size()), 69,
                                      48000, resources.requests[index], &resources.audio[index]);
        }
    };
    std::thread song(render, 0);
    std::thread preview(render, 1);
    while (ready.load() != 2)
        std::this_thread::yield();
    start.store(true);
    song.join();
    preview.join();
    for (int index = 0; index < 2; ++index)
    {
        if (results[index] != 0)
        {
            std::cerr << "Concurrent render " << index << " failed: " << results[index] << '\n';
            return 4;
        }
        std::int32_t rate = 0, frames = 0;
        const float *samples = nullptr;
        if (runtime.getRenderedAudio(resources.audio[index], &rate, &frames, &samples) != 0 ||
            rate != 48000 || frames <= 0 || !samples)
            return 5;
        double peak = 0;
        for (std::int64_t position = 0; position < static_cast<std::int64_t>(frames) * 2;
             ++position)
        {
            if (!std::isfinite(samples[position]))
                return 6;
            peak = (std::max)(peak, std::abs(static_cast<double>(samples[position])));
        }
        if (peak == 0)
            return 7;
        std::cout << (index == 0 ? "Song" : "MIDI69 preview") << ": frames=" << frames
                  << " peak=" << peak << '\n';
    }
    std::cout << "Concurrent native-to-managed song/preview synthesis calls passed (no GUI)\n";
    return 0;
}
