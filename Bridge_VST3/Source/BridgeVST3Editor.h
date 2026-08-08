#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "BridgeVST3Processor.h"

namespace BridgeVST3 {

// M0 插件界面（JUCE）：极简状态页——连接状态、会话 id、版本标识。
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
