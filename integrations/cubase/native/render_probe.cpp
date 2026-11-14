#include "managed_runtime.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>
#include <limits>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3 && argc != 4 && argc != 5)
    {
        std::cerr << "Usage: tunelab_render_probe <managed-directory> <project-state> "
                     "[sample-rate] [preview-pitch]\n";
        return 1;
    }
    std::ifstream stream(std::filesystem::path(argv[2]), std::ios::binary);
    std::vector<std::uint8_t> state((std::istreambuf_iterator<char>(stream)), {});
    if (state.empty() || state.size() > (std::numeric_limits<std::int32_t>::max)())
    {
        return 2;
    }
    TuneLab::Cubase::ManagedRuntime runtime;
    if (!runtime.initialize(argv[1]))
    {
        return 3;
    }
    TuneLab::Cubase::AudioHandle audio = 0;
    int outputRate = argc >= 4 ? _wtoi(argv[3]) : 44100;
    const auto render = [&](auto request, auto audio) {
        if (argc == 5)
        {
            return runtime.renderPreview(state.data(), static_cast<std::int32_t>(state.size()),
                                         _wtoi(argv[4]), outputRate, request, audio);
        }
        return runtime.renderSnapshot(state.data(), static_cast<std::int32_t>(state.size()),
                                      outputRate, request, audio);
    };
    TuneLab::Cubase::RenderRequestHandle request = 0;
    if (runtime.createRenderRequest(&request) != 0 || runtime.cancelRenderRequest(request) != 0 ||
        render(request, &audio) != 6 || audio != 0 || runtime.releaseRenderRequest(request) != 0 ||
        runtime.releaseRenderRequest(request) != 2)
    {
        return 9;
    }
    if (runtime.createRenderRequest(&request) != 0)
    {
        return 10;
    }
    if (render(request, &audio) != 0)
    {
        std::cerr << "Snapshot render failed\n";
        return 4;
    }
    if (runtime.releaseRenderRequest(request) != 0)
    {
        return 11;
    }
    std::cout << "Cancelled request produced no audio; request release checks passed\n";
    std::int32_t rate = 0;
    std::int32_t frames = 0;
    const float *samples = nullptr;
    if (runtime.getRenderedAudio(audio, &rate, &frames, &samples) != 0 || rate != outputRate ||
        frames <= 0 || !samples)
    {
        runtime.releaseRenderedAudio(audio);
        return 5;
    }
    double peak = 0;
    double squares = 0;
    bool finite = true;
    for (std::int64_t index = 0; index < static_cast<std::int64_t>(frames) * 2; ++index)
    {
        const double sample = samples[index];
        finite = finite && std::isfinite(sample);
        peak = (std::max)(peak, std::abs(sample));
        squares += sample * sample;
    }
    const double rms = std::sqrt(squares / (static_cast<double>(frames) * 2));
    std::cout << "Snapshot cache: rate=" << rate << " frames=" << frames << " peak=" << peak
              << " rms=" << rms << " finite=" << finite << '\n';
    if (runtime.releaseRenderedAudio(audio) != 0 || runtime.releaseRenderedAudio(audio) != 2)
    {
        return 6;
    }
    if (runtime.getRenderedAudio(audio, &rate, &frames, &samples) != 2 || samples != nullptr)
    {
        return 7;
    }
    return finite && peak > 0 && rms > 0 ? 0 : 8;
}
