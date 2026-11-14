#include "render_worker.h"
#include <cmath>
#include <fstream>
#include <iostream>
#include <iterator>

int wmain(int argc, wchar_t **argv)
{
    if (argc != 3)
    {
        std::cerr << "Usage: tunelab_render_worker_probe <managed-directory> <project-state>\n";
        return 1;
    }
    std::ifstream stream(std::filesystem::path(argv[2]), std::ios::binary);
    std::vector<std::uint8_t> state((std::istreambuf_iterator<char>(stream)), {});
    TuneLab::Cubase::ManagedRuntime runtime;
    if (state.empty() || !runtime.initialize(argv[1]))
    {
        return 2;
    }
    TuneLab::Cubase::RenderWorker worker(std::move(runtime));
    const auto first = worker.submit(state, 44100);
    const auto latest = worker.submit(state, 48000);
    if (latest <= first || !worker.wait(latest, std::chrono::minutes(5)) || worker.result() != 0)
    {
        std::cerr << "Latest render generation failed or timed out\n";
        return 3;
    }
    std::vector<double> left(1024), right(1024);
    double peak = 0;
    bool finite = true;
    for (int position = 0; position < 144000; position += 1024)
    {
        worker.copyStereo(left.data(), right.data(), 1024, position, 48000);
        for (int index = 0; index < 1024; ++index)
        {
            finite = finite && std::isfinite(left[index]) && std::isfinite(right[index]);
            peak = (std::max)(peak, (std::max)(std::abs(left[index]), std::abs(right[index])));
        }
    }
    if (!finite || peak <= 0 || worker.copyStereo(left.data(), right.data(), 1024, 0, 44100))
    {
        return 4;
    }
    std::cout << "Latest asynchronous generation published; native block reads peak=" << peak
              << "; stale rate rejected\n";
    worker.invalidate();
    if (worker.copyStereo(left.data(), right.data(), 1024, 0, 48000))
    {
        return 5;
    }
    // Destruction cancels a queued/in-flight request and joins its worker.
    worker.submit(std::move(state), 48000);
    return 0;
}
