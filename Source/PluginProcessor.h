#pragma once
#include <JuceHeader.h>

class MajedAudioProcessor : public juce::AudioProcessor
{
public:
    MajedAudioProcessor();
    ~MajedAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "MAJED AUDIO Arabic Chord Extractor"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}

    juce::String getDetectedNote() const;
    juce::String getSuggestedChord() const;
    juce::String getDetectedScale() const;
    float getConfidence() const;
    void requestMidiExport();

private:
    void analyseFrame();
    float estimatePitchHz (const float* data, int n);
    juce::String noteNameFromMidi (int midi) const;

    juce::AudioBuffer<float> analysisBuffer;
    int writePosition = 0;
    double currentSampleRate = 44100.0;
    std::atomic<int> detectedMidi { -1 };
    std::atomic<int> suggestedRoot { -1 };
    std::atomic<int> suggestedQuality { 0 };
    std::atomic<float> confidence { 0.0f };
    std::atomic<bool> midiExportRequested { false };
    float pitchClassWeights = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MajedAudioProcessor)
};
