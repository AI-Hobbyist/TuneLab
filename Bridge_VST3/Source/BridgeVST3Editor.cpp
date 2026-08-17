#include "BridgeVST3Editor.h"

namespace BridgeVST3 {

BridgeVST3Editor::BridgeVST3Editor (BridgeVST3Processor& p)
    : AudioProcessorEditor (&p), mProcessor (p)
{
    setSize (360, 180);

    addAndMakeVisible (mStatusLabel);
    mStatusLabel.setJustificationType (juce::Justification::centred);
    mStatusLabel.setFont (juce::Font (18.0f, juce::Font::bold));

    addAndMakeVisible (mDetailLabel);
    mDetailLabel.setJustificationType (juce::Justification::centred);
    mDetailLabel.setFont (juce::Font (13.0f));

    addAndMakeVisible (mVersionLabel);
    mVersionLabel.setJustificationType (juce::Justification::centred);
    mVersionLabel.setFont (juce::Font (11.0f));
    mVersionLabel.setText ("TuneLab VST Bridge  (M3)", juce::dontSendNotification);

    startTimer (TL_BRIDGE_HEARTBEAT_MS);
    timerCallback();
}

BridgeVST3Editor::~BridgeVST3Editor()
{
    stopTimer();
}

void BridgeVST3Editor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff1e1e2e));
}

void BridgeVST3Editor::resized()
{
    auto area = getLocalBounds().reduced (20);
    mVersionLabel.setBounds (area.removeFromBottom (20));
    mStatusLabel.setBounds (area.removeFromTop (60));
    mDetailLabel.setBounds (area);
}

void BridgeVST3Editor::timerCallback()
{
    auto& session = mProcessor.session();
    if (!session.isCreated())
    {
        mStatusLabel.setText ("Bridge session unavailable", juce::dontSendNotification);
        mDetailLabel.setText ({}, juce::dontSendNotification);
    }
    else
    {
        mStatusLabel.setText (mProcessor.lastConnected() ? "Connected" : "Waiting for TuneLab...", juce::dontSendNotification);

        // M1：会话 id + DAW 音频配置（采样率/块大小/激活总线）。
        juce::String detail = "Session: " + mProcessor.sessionId();
        const uint32_t sr = session.readSampleRate();
        if (sr != 0)
        {
            detail += "\nDAW: " + juce::String (sr) + " Hz, block " + juce::String (session.readBlockSize())
                    + ", buses " + juce::String (session.readActiveBuses());

            // 诊断（M1 音频链路）：DAW 播放头位置 + 总线 0 的宿主写位/插件读位/下溢。
            const uint64_t dawPos  = session.readSamplePos();
            const uint64_t writePos = session.ringWritePos (0);
            const uint64_t readPos  = session.ringReadPos (0);
            const uint64_t underflow = session.ringState (0) != nullptr ? session.ringState (0)->underflow : 0;
            detail += "\npos " + juce::String (dawPos) + "  W " + juce::String (writePos)
                    + "  R " + juce::String (readPos) + "  uf " + juce::String (underflow);
        }
        mDetailLabel.setText (detail, juce::dontSendNotification);
    }
}

} // namespace BridgeVST3
