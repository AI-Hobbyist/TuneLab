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
    mVersionLabel.setText ("TuneLab VST Bridge  (M0)", juce::dontSendNotification);

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
    if (!mProcessor.session().isCreated())
    {
        mStatusLabel.setText ("Bridge session unavailable", juce::dontSendNotification);
    }
    else
    {
        mStatusLabel.setText (mProcessor.lastConnected() ? "Connected" : "Waiting for TuneLab...", juce::dontSendNotification);
        mDetailLabel.setText ("Session: " + mProcessor.sessionId(), juce::dontSendNotification);
    }
}

} // namespace BridgeVST3
