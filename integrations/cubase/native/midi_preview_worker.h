#pragma once

#include "audio_cache.h"
#include "managed_runtime.h"
#include "midi_input.h"
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <thread>

namespace TuneLab::Cubase
{
// Control thread owns score updates; audio thread produces MIDI; this worker
// alone synthesizes, publishes and reclaims the per-pitch preview caches.
class MidiPreviewWorker
{
  public:
    explicit MidiPreviewWorker(ManagedRuntime runtime) : runtime(std::move(runtime))
    {
        thread = std::thread([this] { run(); });
    }

    ~MidiPreviewWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            if (activeRequest)
            {
                runtime.cancelRenderRequest(activeRequest);
            }
        }
        changed.notify_all();
        thread.join();
    }

    void update(std::vector<std::uint8_t> state, std::int32_t rate)
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (activeRequest)
        {
            runtime.cancelRenderRequest(activeRequest);
        }
        score = std::move(state);
        sampleRate = rate;
        lastResult.store(-1);
        elapsed.store(0);
        generation.fetch_add(1);
        changed.notify_all();
    }

    std::uint64_t revision() const noexcept
    {
        return generation.load();
    }
    std::int32_t result() const noexcept
    {
        return lastResult.load();
    }
    std::int64_t renderMicroseconds() const noexcept
    {
        return elapsed.load();
    }

    template <typename Sample>
    bool copy(int pitch, Sample *left, Sample *right, int count, std::int64_t position,
              std::uint64_t revision, std::int32_t rate, bool &ready) const noexcept
    {
        return caches[pitch].copyStereo(left, right, count, position, revision, rate, &ready);
    }

    MidiInput<> input;

  private:
    std::unique_ptr<AudioSnapshot> render(const std::vector<std::uint8_t> &state,
                                          const MidiNoteEvent &event, int rate, int &result)
    {
        AudioHandle audio = 0;
        result = runtime.renderPreview(state.data(), static_cast<std::int32_t>(state.size()),
                                       event.pitch, rate, activeRequest, &audio);
        if (result != 0)
        {
            return nullptr;
        }
        struct Lease
        {
            ManagedRuntime &runtime;
            AudioHandle handle;
            ~Lease()
            {
                runtime.releaseRenderedAudio(handle);
            }
        } lease{runtime, audio};
        std::int32_t outputRate = 0, frames = 0;
        const float *samples = nullptr;
        result = runtime.getRenderedAudio(audio, &outputRate, &frames, &samples);
        if (result != 0 || !samples || frames <= 0 || rate != outputRate)
        {
            result = static_cast<int>(BridgeResult::Failed);
            return nullptr;
        }
        auto snapshot = std::make_unique<AudioSnapshot>();
        snapshot->generation = event.revision;
        snapshot->sampleRate = rate;
        snapshot->interleaved.assign(samples, samples + static_cast<std::size_t>(frames) * 2);
        return snapshot;
    }

    void run()
    {
        std::array<std::uint64_t, 128> prepared{};
        std::uint64_t observedLoss = 0;
        while (true)
        {
            std::unique_lock<std::mutex> lock(mutex);
            if (stopping)
            {
                return;
            }
            if (input.lostEvents() != observedLoss)
            {
                observedLoss = input.lostEvents();
                input.discardPending();
            }
            MidiNoteEvent event;
            if (!input.pop(event))
            {
                changed.wait_for(lock, std::chrono::milliseconds(2));
                continue;
            }
            if (!event.on || event.revision != generation.load() || score.empty() ||
                prepared[event.pitch] == event.revision)
            {
                continue;
            }
            auto state = score;
            const int rate = sampleRate;
            int result = runtime.createRenderRequest(&activeRequest);
            if (result != 0)
            {
                lastResult.store(result);
                continue;
            }
            lastResult.store(static_cast<int>(BridgeResult::Busy));
            const auto start = std::chrono::steady_clock::now();
            lock.unlock();
            std::unique_ptr<AudioSnapshot> snapshot;
            try
            {
                snapshot = render(state, event, rate, result);
            }
            catch (const std::exception &exception)
            {
                OutputDebugStringA(exception.what());
                result = static_cast<int>(BridgeResult::Failed);
            }
            lock.lock();
            runtime.releaseRenderRequest(activeRequest);
            activeRequest = 0;
            if (!stopping && event.revision == generation.load())
            {
                if (snapshot)
                {
                    caches[event.pitch].publish(std::move(snapshot));
                    prepared[event.pitch] = event.revision;
                }
                elapsed.store(std::chrono::duration_cast<std::chrono::microseconds>(
                                  std::chrono::steady_clock::now() - start)
                                  .count());
                lastResult.store(result);
            }
            for (auto &cache : caches)
            {
                cache.collectRetired();
            }
        }
    }

    ManagedRuntime runtime;
    std::array<AudioCache, 128> caches;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::int32_t> lastResult{-1};
    std::atomic<std::int64_t> elapsed{0};
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::uint8_t> score;
    std::int32_t sampleRate = 0;
    RenderRequestHandle activeRequest = 0;
    bool stopping = false;
    std::thread thread;
};
} // namespace TuneLab::Cubase
