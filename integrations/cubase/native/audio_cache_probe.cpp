#include "audio_cache.h"
#include <array>
#include <iostream>
#include <thread>

std::unique_ptr<TuneLab::Cubase::AudioSnapshot> makeSnapshot(std::uint64_t generation, float value)
{
    auto snapshot = std::make_unique<TuneLab::Cubase::AudioSnapshot>();
    snapshot->generation = generation;
    snapshot->sampleRate = 48000;
    snapshot->interleaved.assign(128, value);
    return snapshot;
}

int main()
{
    TuneLab::Cubase::AudioCache cache;
    std::array<float, 16> left, right;
    cache.publish(makeSnapshot(1, 0.25f));
    if (!cache.copyStereo(left.data(), right.data(), 16, -2, 1, 48000) || left[0] != 0 ||
        left[1] != 0 || left[2] != 0.25f)
    {
        return 1;
    }
    if (cache.copyStereo(left.data(), right.data(), 16, 0, 2, 48000) || left[0] != 0 ||
        cache.copyStereo(left.data(), right.data(), 16, 0, 1, 44100))
    {
        return 2;
    }
    std::atomic<bool> stop{false};
    std::atomic<bool> failed{false};
    std::atomic<unsigned> readCount{0};
    std::thread reader([&] {
        std::array<double, 64> a, b;
        while (!stop.load())
        {
            if (cache.copyStereo(a.data(), b.data(), 64, 0, 1, 48000))
            {
                for (int i = 0; i < 64; ++i)
                {
                    if (a[i] != a[0] || b[i] != a[0])
                    {
                        failed.store(true);
                    }
                }
            }
            readCount.fetch_add(1);
        }
    });
    while (readCount.load() == 0)
    {
        std::this_thread::yield();
    }
    for (int index = 0; index < 10000; ++index)
    {
        cache.publish(makeSnapshot(1, index % 2 ? 0.5f : 0.25f));
    }
    stop.store(true);
    reader.join();
    cache.clear();
    if (failed.load() || cache.copyStereo(left.data(), right.data(), 16, 0, 1, 48000))
    {
        return 3;
    }
    std::cout
        << "Audio cache boundaries, generation/rate rejection and concurrent publication passed\n";
    return 0;
}
