#include "PluginProcessor.h"
#include "PluginEditor.h"

MajedAudioProcessorEditor::MajedAudioProcessorEditor (MajedAudioProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setSize (460, 300);

    addAndMakeVisible (exportMidiButton);
    exportMidiButton.onClick = [this] { processorRef.requestMidiExport(); };

    startTimerHz (15);
}

void MajedAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff141824));

    g.setColour (juce::Colours::white);
    g.setFont (juce::Font (20.0f, juce::Font::bold));
    g.drawText ("MAJED AUDIO - Chord & Maqam Extractor", 20, 15, getWidth() - 40, 30, juce::Justification::centred);

    g.setColour (juce::Colour (0xff1f2638));
    g.fillRoundedRectangle (20.0f, 60.0f, (float) getWidth() - 40.0f, 165.0f, 10.0f);

    g.setColour (juce::Colours::lightgrey);
    g.setFont (16.0f);
    g.drawText ("Detected Note: " + processorRef.getDetectedNote(), 35, 75, 380, 28, juce::Justification::left);

    g.setColour (juce::Colour (0xff4ee4a0));
    g.setFont (juce::Font (22.0f, juce::Font::bold));
    g.drawText ("Suggested Chord: " + processorRef.getSuggestedChord(), 35, 112, 380, 32, juce::Justification::left);

    g.setColour (juce::Colours::skyblue);
    g.setFont (17.0f);
    g.drawText ("Maqam / Scale: " + processorRef.getDetectedScale(), 35, 152, 380, 28, juce::Justification::left);

    int confPct = juce::roundToInt (processorRef.getConfidence() * 100.0f);
    g.setColour (juce::Colours::grey);
    g.setFont (14.0f);
    g.drawText ("Confidence: " + juce::String (confPct) + "%", 35, 185, 380, 24, juce::Justification::left);
}

void MajedAudioProcessorEditor::resized()
{
    exportMidiButton.setBounds (getWidth() / 2 - 90, 242, 180, 38);
}

void MajedAudioProcessorEditor::timerCallback()
{
    repaint();
}
