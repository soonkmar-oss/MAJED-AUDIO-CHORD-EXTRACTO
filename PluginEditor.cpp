#include "PluginEditor.h"

MajedAudioEditor::MajedAudioEditor (MajedAudioProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setSize (520, 330);
    title.setText ("MAJED AUDIO", juce::dontSendNotification);
    title.setFont (juce::Font (28.0f, juce::Font::bold));
    title.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (title);

    auto setup = [this] (juce::Label& l, const juce::String& t)
    {
        l.setText (t, juce::dontSendNotification);
        l.setFont (juce::Font (18.0f));
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    };
    setup (note, "النغمة: --");
    setup (chord, "الكورد المقترح: --");
    setup (scale, "المقام / السلم: غير محدد");
    setup (confidence, "ثقة الكشف: --");

    midiButton.onClick = [this] { processor.requestMidiExport(); };
    addAndMakeVisible (midiButton);
    startTimerHz (8);
}

void MajedAudioEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colour (0xff10151d));
    g.setColour (juce::Colour (0xffc49b54));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (10.0f), 14.0f, 2.0f);
}

void MajedAudioEditor::resized()
{
    auto area = getLocalBounds().reduced (24);
    title.setBounds (area.removeFromTop (55));
    area.removeFromTop (12);
    note.setBounds (area.removeFromTop (42));
    chord.setBounds (area.removeFromTop (42));
    scale.setBounds (area.removeFromTop (42));
    confidence.setBounds (area.removeFromTop (36));
    midiButton.setBounds (area.removeFromTop (42).reduced (70, 2));
}

void MajedAudioEditor::timerCallback()
{
    note.setText ("النغمة المكتشفة: " + processor.getDetectedNote(), juce::dontSendNotification);
    chord.setText ("الكورد المقترح: " + processor.getSuggestedChord(), juce::dontSendNotification);
    scale.setText ("المقام / السلم: " + processor.getDetectedScale(), juce::dontSendNotification);
    confidence.setText ("ثقة الكشف: " + juce::String (processor.getConfidence() * 100.0f, 1) + "%", juce::dontSendNotification);
}
