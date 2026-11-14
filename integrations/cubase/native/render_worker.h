#pragma once

#include "audio_cache.h"
#include "managed_runtime.h"
#include <chrono>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <thread>

namespace TuneLab::Cubase
{
class RenderWorker
{
  public:
    static_assert(std::atomic<std::uint64_t>::is_always_lock_free);
    explicit RenderWorker(ManagedRuntime runtime) : runtime(std::move(runtime))
    {
        thread = std::thread([this] { run(); });
    }

    // The host must stop its process callbacks before destroying this worker.
    ~RenderWorker()
    {
        {
            std::lock_guard<std::mutex> lock(mutex);
            stopping = true;
            if (activeRequest != 0)
            {
                runtime.cancelRenderRequest(activeRequest);
            }
            if (pending)
            {
                runtime.releaseRenderRequest(pending->request);
                pending.reset();
            }
        }
        changed.notify_all();
        thread.join();
    }

    // Control/document thread only. Replaces pending work and invalidates the
    // previous cache immediately through its generation, without touching it.
    std::uint64_t submit(std::vector<std::uint8_t> state, std::int32_t sampleRate)
    {
        if (state.empty() || state.size() > (std::numeric_limits<std::int32_t>::max)() ||
            sampleRate <= 0)
        {
            throw std::invalid_argument("Invalid render snapshot or sample rate");
        }
        std::lock_guard<std::mutex> lock(mutex);
        RenderRequestHandle request = 0;
        if (runtime.createRenderRequest(&request) != 0)
        {
            throw std::runtime_error("Cannot create render request");
        }
        if (pending)
        {
            runtime.releaseRenderRequest(pending->request);
        }
        if (activeRequest != 0)
        {
            runtime.cancelRenderRequest(activeRequest);
        }
        const auto next = generation.fetch_add(1) + 1;
        lastResult.store(static_cast<std::int32_t>(BridgeResult::Busy));
        pending = Job{std::move(state), sampleRate, next, request};
        changed.notify_all();
        return next;
    }

    template <typename Sample>
    bool copyStereo(Sample *left, Sample *right, int count, std::int64_t position,
                    std::int32_t sampleRate) const noexcept
    {
        return cache.copyStereo(left, right, count, position, generation.load(), sampleRate);
    }

    // Non-audio wait, for offline rendering and command-line validation.
    bool wait(std::uint64_t requested, std::chrono::milliseconds timeout)
    {
        std::unique_lock<std::mutex> lock(mutex);
        return changed.wait_for(lock, timeout,
                                [&] { return completedGeneration >= requested || stopping; });
    }

    void invalidate()
    {
        std::lock_guard<std::mutex> lock(mutex);
        if (activeRequest != 0)
        {
            runtime.cancelRenderRequest(activeRequest);
        }
        if (pending)
        {
            runtime.releaseRenderRequest(pending->request);
            pending.reset();
        }
        completedGeneration = generation.fetch_add(1) + 1;
        lastResult.store(0);
        changed.notify_all();
    }

    std::int32_t result() const noexcept
    {
        return lastResult.load();
    }

  private:
    struct Job
    {
        std::vector<std::uint8_t> state;
        std::int32_t sampleRate;
        std::uint64_t generation;
        RenderRequestHandle request;
    };

    std::unique_ptr<AudioSnapshot> render(const Job &job, std::int32_t &result)
    {
        AudioHandle audio = 0;
        result =
            runtime.renderSnapshot(job.state.data(), static_cast<std::int32_t>(job.state.size()),
                                   job.sampleRate, job.request, &audio);
        if (result != 0)
        {
            return nullptr;
        }
        struct AudioScope
        {
            ManagedRuntime &runtime;
            AudioHandle audio;
            ~AudioScope()
            {
                runtime.releaseRenderedAudio(audio);
            }
        } scope{runtime, audio};
        std::int32_t rate = 0, frames = 0;
        const float *samples = nullptr;
        result = runtime.getRenderedAudio(audio, &rate, &frames, &samples);
        if (result != 0 || !samples || frames <= 0 || rate != job.sampleRate)
        {
            result = static_cast<std::int32_t>(BridgeResult::Failed);
            return nullptr;
        }
        auto snapshot = std::make_unique<AudioSnapshot>();
        snapshot->generation = job.generation;
        snapshot->sampleRate = rate;
        snapshot->interleaved.assign(samples, samples + static_cast<std::size_t>(frames) * 2);
        return snapshot;
    }

    void run()
    {
        while (true)
        {
            std::unique_lock<std::mutex> lock(mutex);
            changed.wait(lock, [&] { return stopping || pending.has_value(); });
            if (stopping)
            {
                return;
            }
            Job job = std::move(*pending);
            pending.reset();
            activeRequest = job.request;
            lock.unlock();
            std::unique_ptr<AudioSnapshot> snapshot;
            std::int32_t renderResult = static_cast<std::int32_t>(BridgeResult::Failed);
            try
            {
                snapshot = render(job, renderResult);
            }
            catch (const std::exception &exception)
            {
                renderResult = static_cast<std::int32_t>(BridgeResult::Failed);
                OutputDebugStringA(exception.what());
            }
            lock.lock();
            runtime.releaseRenderRequest(job.request);
            activeRequest = 0;
            if (!stopping && generation.load() == job.generation)
            {
                if (snapshot)
                {
                    cache.publish(std::move(snapshot));
                }
                // Observing success must also observe the published samples.
                lastResult.store(renderResult);
            }
            cache.collectRetired();
            completedGeneration = (std::max)(completedGeneration, job.generation);
            changed.notify_all();
        }
    }

    ManagedRuntime runtime;
    AudioCache cache;
    std::atomic<std::uint64_t> generation{0};
    std::atomic<std::int32_t> lastResult{static_cast<std::int32_t>(BridgeResult::Busy)};
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<Job> pending;
    RenderRequestHandle activeRequest = 0;
    std::uint64_t completedGeneration = 0;
    bool stopping = false;
    std::thread thread;
};
} // namespace TuneLab::Cubase
