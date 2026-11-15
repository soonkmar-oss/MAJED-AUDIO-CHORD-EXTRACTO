#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MajedAudioEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MajedAudioEditor (MajedAudioProcessor&);
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    MajedAudioProcessor& processor;
    juce::Label title, note, chord, scale, confidence;
    juce::TextButton midiButton { "إرسال النغمة إلى MIDI" };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MajedAudioEditor)
};
