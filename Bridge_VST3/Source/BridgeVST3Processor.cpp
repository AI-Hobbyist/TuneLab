#include "BridgeVST3Processor.h"
#include "BridgeVST3Editor.h"

namespace BridgeVST3 {

BridgeVST3Processor::BridgeVST3Processor()
    : AudioProcessor (makeBuses())
{
    mSessionId = "default";
    if (mSession.init (mSessionId.toStdString()))
        startTimer (TL_BRIDGE_HEARTBEAT_MS);
}

BridgeVST3Processor::~BridgeVST3Processor()
{
    stopTimer();
    mSession.shutdown();
}

juce::AudioProcessor::BusesProperties BridgeVST3Processor::makeBuses()
{
    // 声明 TL_BRIDGE_MAX_TRACKS 条可选立体声输出总线（默认不激活），M3 按轨激活/停用。
    auto buses = juce::AudioProcessor::BusesProperties();
    for (int i = 0; i < static_cast<int> (TL_BRIDGE_MAX_TRACKS); ++i)
        buses = buses.withOutput ("Track " + juce::String (i + 1), juce::AudioChannelSet::stereo(), false);

    return buses;
}

void BridgeVST3Processor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    // M0：仅把 DAW 音频配置写进控制块（M1 采样率协商的基础）。
    mSession.writeAudioConfig (static_cast<uint32_t> (sampleRate), static_cast<uint32_t> (samplesPerBlock), 0, 0);
}

void BridgeVST3Processor::releaseResources()
{
}

void BridgeVST3Processor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    // M0：空壳，无音频拉取（M1 从共享环拉取逐轨音频）。
    buffer.clear();
}

void BridgeVST3Processor::timerCallback()
{
    if (!mSession.isCreated())
        return;

    mSession.tick();

    const bool connected = mSession.isConnected();
    if (connected != mLastConnected.load())
    {
        mLastConnected.store (connected);
        // 编辑器通过自身 Timer 轮询 lastConnected，无需主动通知。
    }
}

juce::AudioProcessorEditor* BridgeVST3Processor::createEditor()
{
    return new BridgeVST3Editor (*this);
}

} // namespace BridgeVST3

// VST3 包装层入口：返回插件实例（CMake 构建需手动提供，见 juce_AudioProcessor.h）。
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new BridgeVST3::BridgeVST3Processor();
}
