#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <string>

#include "BridgeVST3Shared.h"

namespace BridgeVST3 {

// M3：VSTi3 桥接处理器。创建共享内存会话等待 TuneLab 连接，按宿主轨道表动态启停输出总线，
// process() 从各总线共享环实时拉取音频。
class BridgeVST3Processor : public juce::AudioProcessor, public juce::Timer
{
public:
    BridgeVST3Processor();
    ~BridgeVST3Processor() override;

    // juce::AudioProcessor
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return "Bridge_VST3"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
    // JUCE 9：getLatencySamples 为非虚（默认 0，M0 无需补偿），经 setLatencySamples 设置。

    // juce::Timer（心跳）
    void timerCallback() override;

    // 供编辑器读取
    BridgeSession& session() noexcept { return mSession; }
    const juce::String& sessionId() const noexcept { return mSessionId; }
    bool lastConnected() const noexcept { return mLastConnected; }

private:
    static juce::AudioProcessor::BusesProperties makeBuses();
    void syncTrackBuses();

    BridgeSession mSession;
    juce::String mSessionId;
    std::atomic<bool> mLastConnected { false };
    uint32_t mLastLatency = 0;   // 已上报的延迟（避免每心跳重复 setLatencySamples）
    uint64_t mLastBusMask = 0;
    bool mHasBusMask = false;
};

} // namespace BridgeVST3
