#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"

class MajedAudioProcessorEditor : public juce::AudioProcessorEditor,
                                  private juce::Timer
{
public:
    explicit MajedAudioProcessorEditor (MajedAudioProcessor&);
    ~MajedAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    MajedAudioProcessor& processorRef;
    juce::TextButton exportMidiButton { "Export Chord MIDI" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MajedAudioProcessorEditor)
};
