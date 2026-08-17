#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BridgeVST3Processor.h"

namespace BridgeVST3 {

// M3 插件界面（JUCE）：连接状态、会话 id、采样率/块大小与实际启用总线数。
class BridgeVST3Editor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit BridgeVST3Editor (BridgeVST3Processor& p);
    ~BridgeVST3Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    BridgeVST3Processor& mProcessor;
    juce::Label mStatusLabel;
    juce::Label mDetailLabel;
    juce::Label mVersionLabel;
};

} // namespace BridgeVST3
