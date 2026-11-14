#pragma once

#include "midi_preview_player.h"

namespace TuneLab::Cubase
{
// Host-thread producer, audio-thread consumer. The latest key state supersedes
// intermediate mouse moves; note identity zero makes this a monophonic preview.
class AraKeyPreview
{
  public:
    void prepare(int maximumBlock, int rate)
    {
        clear();
        sampleRate = rate;
        left.resize(maximumBlock);
        right.resize(maximumBlock);
    }
    void clear()
    {
        published.store(nullptr);
        player.reset();
        state.clear();
        command.store(0);
        observedCommand = 0;
        lastPitch = 0;
    }
    void start(ManagedRuntime runtime, std::vector<std::uint8_t> snapshot, int pitch)
    {
        if (pitch < 0 || pitch > 127)
            return;
        if (!player)
            player = std::make_unique<MidiPreviewPlayer>(std::move(runtime));
        if (snapshot != state)
        {
            player->update(snapshot, sampleRate);
            state = std::move(snapshot);
        }
        published.store(player.get());
        publishCommand(static_cast<std::uint64_t>(pitch + 1));
    }
    void stop() noexcept
    {
        publishCommand(0);
    }

    template <typename Sample> bool mix(Sample *outputLeft, Sample *outputRight, int count) noexcept
    {
        auto *current = published.load();
        if (!current || count < 0 || static_cast<std::size_t>(count) > left.size())
            return false;
        KeyEvent events;
        const auto next = command.load();
        if (next != observedCommand)
        {
            observedCommand = next;
            const int pitch = static_cast<int>(next & 255) - 1;
            events.present = true;
            if (pitch >= 0)
            {
                events.event.type = Steinberg::Vst::Event::kNoteOnEvent;
                events.event.noteOn = {0, static_cast<Steinberg::int16>(pitch), 0, 1, 0, 0};
                lastPitch = pitch;
            }
            else
            {
                events.event.type = Steinberg::Vst::Event::kNoteOffEvent;
                events.event.noteOff = {0, static_cast<Steinberg::int16>(lastPitch), 0, 0, 0};
            }
        }
        if (!outputLeft)
            return current->process<float>(&events, nullptr, nullptr, count, sampleRate);
        std::fill_n(left.data(), count, 0.0f);
        std::fill_n(right.data(), count, 0.0f);
        const bool audible =
            current->process(&events, left.data(), right.data(), count, sampleRate);
        for (int index = 0; index < count; ++index)
        {
            if (outputRight)
            {
                outputLeft[index] += static_cast<Sample>(left[index]);
                outputRight[index] += static_cast<Sample>(right[index]);
            }
            else
                outputLeft[index] += static_cast<Sample>((left[index] + right[index]) * 0.5f);
        }
        return audible;
    }

  private:
    void publishCommand(std::uint64_t key) noexcept
    {
        auto previous = command.load();
        while (!command.compare_exchange_weak(previous, (((previous >> 8) + 1) << 8) | key))
        {
        }
    }

    // Borrowed synchronously by MidiPreviewPlayer; never published to the host.
    class KeyEvent final : public Steinberg::Vst::IEventList
    {
      public:
        Steinberg::Vst::Event event{};
        bool present = false;
        Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID, void **object) override
        {
            *object = nullptr;
            return Steinberg::kNoInterface;
        }
        Steinberg::uint32 PLUGIN_API addRef() override
        {
            return 1;
        }
        Steinberg::uint32 PLUGIN_API release() override
        {
            return 1;
        }
        Steinberg::int32 PLUGIN_API getEventCount() override
        {
            return present ? 1 : 0;
        }
        Steinberg::tresult PLUGIN_API getEvent(Steinberg::int32 index,
                                               Steinberg::Vst::Event &result) override
        {
            if (!present || index != 0)
                return Steinberg::kInvalidArgument;
            result = event;
            return Steinberg::kResultOk;
        }
        Steinberg::tresult PLUGIN_API addEvent(Steinberg::Vst::Event &) override
        {
            return Steinberg::kResultFalse;
        }
    };
    std::unique_ptr<MidiPreviewPlayer> player;
    std::atomic<MidiPreviewPlayer *> published{nullptr};
    std::vector<std::uint8_t> state;
    std::vector<float> left, right;
    std::atomic<std::uint64_t> command{0};
    std::uint64_t observedCommand = 0;
    int sampleRate = 0, lastPitch = 0;
};
} // namespace TuneLab::Cubase
