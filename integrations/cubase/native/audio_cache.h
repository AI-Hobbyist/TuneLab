#pragma once

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <memory>
#include <vector>

namespace TuneLab::Cubase
{
struct AudioSnapshot
{
    std::uint64_t generation = 0;
    std::int32_t sampleRate = 0;
    std::vector<float> interleaved;
};

// One non-audio publisher/reclaimer, any number of audio readers. All atomics
// use sequential consistency so an observed zero reader count after exchange
// proves that no reader can still acquire the retired pointer.
class AudioCache
{
  public:
    static_assert(std::atomic<const AudioSnapshot *>::is_always_lock_free);
    static_assert(std::atomic<std::uint32_t>::is_always_lock_free);

    void publish(std::unique_ptr<AudioSnapshot> snapshot)
    {
        const auto *pointer = snapshot.get();
        owned.push_back(std::move(snapshot));
        active.store(pointer);
        collectRetired();
    }

    void clear()
    {
        active.store(nullptr);
        collectRetired();
    }

    void collectRetired()
    {
        if (readers.load() != 0)
        {
            return;
        }
        const auto *current = active.load();
        owned.erase(
            std::remove_if(owned.begin(), owned.end(),
                           [current](const auto &snapshot) { return snapshot.get() != current; }),
            owned.end());
    }

    // No allocations, locks, managed calls or reclamation on this path.
    template <typename Sample>
    bool copyStereo(Sample *left, Sample *right, int count, std::int64_t startFrame,
                    std::uint64_t generation, std::int32_t sampleRate,
                    bool *ready = nullptr) const noexcept
    {
        if (ready)
        {
            *ready = false;
        }
        if (count <= 0)
        {
            return false;
        }
        std::fill_n(left, count, Sample(0));
        std::fill_n(right, count, Sample(0));
        readers.fetch_add(1);
        struct ReadScope
        {
            std::atomic<std::uint32_t> &readers;
            ~ReadScope()
            {
                readers.fetch_sub(1);
            }
        } scope{readers};
        const auto *snapshot = active.load();
        if (!snapshot || snapshot->generation != generation || snapshot->sampleRate != sampleRate)
        {
            return false;
        }
        if (ready)
        {
            *ready = true;
        }
        const auto available = static_cast<std::int64_t>(snapshot->interleaved.size() / 2);
        if (startFrame <= -static_cast<std::int64_t>(count) || startFrame >= available)
        {
            return false;
        }
        const int begin = startFrame < 0 ? static_cast<int>(-startFrame) : 0;
        const int end =
            static_cast<int>((std::min)(static_cast<std::int64_t>(count), available - startFrame));
        bool audible = false;
        for (int index = begin; index < end; ++index)
        {
            const auto source = static_cast<std::size_t>(startFrame + index) * 2;
            left[index] = static_cast<Sample>(snapshot->interleaved[source]);
            right[index] = static_cast<Sample>(snapshot->interleaved[source + 1]);
            audible = audible || left[index] != 0 || right[index] != 0;
        }
        return audible;
    }

  private:
    // Destroy only after the processor has stopped and its worker has joined.
    std::vector<std::unique_ptr<AudioSnapshot>> owned;
    std::atomic<const AudioSnapshot *> active{nullptr};
    mutable std::atomic<std::uint32_t> readers{0};
};
} // namespace TuneLab::Cubase
