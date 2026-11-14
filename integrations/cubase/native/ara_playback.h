#pragma once

#include "render_worker.h"
#include <cmath>

namespace TuneLab::Cubase
{
// Control thread publishes immutable region plans; audio readers retain the
// plan through a lock-free reader count. Workers are reclaimed off audio only.
class AraPlayback
{
  public:
    void prepare(int maximumBlock, std::int32_t rate)
    {
        clear();
        beginUpdate();
        sampleRate = rate;
        left.resize(maximumBlock);
        right.resize(maximumBlock);
    }

    void beginUpdate()
    {
        pending = std::make_unique<Plan>();
    }

    void publish()
    {
        const auto *pointer = pending.get();
        owned.push_back(std::move(pending));
        active.store(pointer);
        collectRetired();
    }

    void clear()
    {
        active.store(nullptr);
        pending.reset();
        collectRetired();
    }

    void add(ManagedRuntime runtime, std::vector<std::uint8_t> state, double sourceStart,
             double songStart, double duration, bool offline, std::shared_ptr<void> region = {})
    {
        Entry entry;
        entry.sourceStart = frame(sourceStart);
        entry.songStart = frame(songStart);
        entry.songEnd = frame(songStart + duration);
        entry.region = std::move(region);
        if (entry.region)
        {
            if (const auto *previous = active.load())
            {
                for (const auto &candidate : previous->entries)
                {
                    if (candidate.region == entry.region)
                    {
                        entry.worker = candidate.worker;
                        break;
                    }
                }
            }
        }
        if (!entry.worker)
            entry.worker = std::make_shared<RenderWorker>(std::move(runtime));
        const auto generation = entry.worker->submit(std::move(state), sampleRate);
        entry.generation = generation;
        if (offline && (!entry.worker->wait(generation, std::chrono::minutes(5)) ||
                        entry.worker->result() != 0))
            throw std::runtime_error("ARA offline region synthesis failed");
        pending->entries.push_back(std::move(entry));
    }

    // Host control thread only. Completion becomes visible through each
    // worker's atomic result; do not invoke host callbacks on worker threads.
    bool ready() const noexcept
    {
        const auto *plan = active.load();
        return plan && !plan->entries.empty() &&
               std::all_of(plan->entries.begin(), plan->entries.end(),
                           [](const auto &entry) { return entry.worker->ready(entry.generation); });
    }

    template <typename Sample>
    bool mix(Sample *outputLeft, Sample *outputRight, int count, std::int64_t songPosition) noexcept
    {
        if (count <= 0 || static_cast<std::size_t>(count) > left.size())
            return false;
        readers.fetch_add(1);
        struct ReadScope
        {
            std::atomic<std::uint32_t> &readers;
            ~ReadScope()
            {
                readers.fetch_sub(1);
            }
        } scope{readers};
        const auto *plan = active.load();
        if (!plan)
            return false;
        bool audible = false;
        for (const auto &entry : plan->entries)
        {
            if (entry.songEnd <= songPosition)
                continue;
            int begin = 0;
            if (entry.songStart > songPosition)
            {
                const auto distance = static_cast<std::uint64_t>(entry.songStart) -
                                      static_cast<std::uint64_t>(songPosition);
                if (distance >= static_cast<std::uint64_t>(count))
                    continue;
                begin = static_cast<int>(distance);
            }
            const auto remaining = static_cast<std::uint64_t>(entry.songEnd) -
                                   static_cast<std::uint64_t>(songPosition);
            const auto end =
                static_cast<int>((std::min)(remaining, static_cast<std::uint64_t>(count)));
            const auto elapsed = songPosition < entry.songStart
                                     ? std::uint64_t(0)
                                     : static_cast<std::uint64_t>(songPosition) -
                                           static_cast<std::uint64_t>(entry.songStart);
            // Bound the elapsed conversion before adding a possibly negative
            // source offset. AudioCache handles leading negative source frames.
            if (elapsed > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()))
                continue;
            const auto delta = static_cast<std::int64_t>(elapsed);
            if (entry.sourceStart > (std::numeric_limits<std::int64_t>::max)() - delta)
                continue;
            const auto position = entry.sourceStart + delta;
            if (!entry.worker->copyRevision(left.data(), right.data(), end - begin, position,
                                            sampleRate, entry.generation))
                continue;
            for (int index = begin; index < end; ++index)
            {
                const auto l = left[index - begin];
                const auto r = right[index - begin];
                if (outputRight)
                {
                    outputLeft[index] += static_cast<Sample>(l);
                    outputRight[index] += static_cast<Sample>(r);
                }
                else
                    outputLeft[index] += static_cast<Sample>((l + r) * 0.5f);
                audible = audible || l != 0 || r != 0;
            }
        }
        return audible;
    }

  private:
    std::int64_t frame(double seconds) const
    {
        const double value = seconds * sampleRate;
        if (!std::isfinite(value) ||
            value <= static_cast<double>((std::numeric_limits<std::int64_t>::min)()) ||
            value >= static_cast<double>((std::numeric_limits<std::int64_t>::max)()))
            throw std::invalid_argument("ARA region frame is out of range");
        return static_cast<std::int64_t>(std::llround(value));
    }

    struct Entry
    {
        std::shared_ptr<RenderWorker> worker;
        // Retain the region lifetime token so a recycled object address cannot match.
        std::shared_ptr<void> region;
        std::uint64_t generation = 0;
        std::int64_t sourceStart = 0;
        std::int64_t songStart = 0;
        std::int64_t songEnd = 0;
    };
    struct Plan
    {
        std::vector<Entry> entries;
    };

    void collectRetired()
    {
        if (readers.load() != 0)
            return;
        const auto *current = active.load();
        owned.erase(std::remove_if(owned.begin(), owned.end(),
                                   [current](const auto &plan) { return plan.get() != current; }),
                    owned.end());
    }

    static_assert(std::atomic<const Plan *>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);
    std::unique_ptr<Plan> pending;
    std::vector<std::unique_ptr<Plan>> owned;
    std::atomic<const Plan *> active{nullptr};
    std::atomic<std::uint32_t> readers{0};
    std::vector<float> left, right;
    std::int32_t sampleRate = 0;
};
} // namespace TuneLab::Cubase
