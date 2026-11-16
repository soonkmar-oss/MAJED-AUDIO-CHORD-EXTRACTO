#pragma once
#include <JuceHeader.h>
#include <array>
#include <vector>

struct DetectedChordEvent
{
    juce::String chordName;
    std::vector<int> midiNotes;
};

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

    const juce::String getName() const override { return "MAJED AUDIO Pro Chord & Maqam"; }
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

    // Getters for UI
    juce::String getCurrentChordName() const;
    juce::String getCurrentMaqamName() const;
    juce::String getRukoozNoteName() const;
    float getConfidence() const;
    std::array<float, 24> getQuarterToneSpectrum() const;
    std::vector<DetectedChordEvent> getChordHistory() const;
    void clearHistory();
    juce::File generateMidiFileForDrag();

    static constexpr int fftOrder = 12; // 4096 samples for high bass resolution
    static constexpr int fftSize = 1 << fftOrder;

private:
    void pushNextSampleIntoFifo (float sample);
    void processFFTFrame();
    void analyzeHarmonyAndMaqam();

    juce::dsp::FFT forwardFFT;
    juce::dsp::WindowingFunction<float> window;

    std::array<float, fftSize> fifo {};
    std::array<float, fftSize * 2> fftData {};
    int fifoIndex = 0;
    double currentSampleRate = 44100.0;

    // 24-bin quarter-tone chroma & 12-bin bass chroma
    std::array<float, 24> chroma24 {};
    std::array<float, 24> smoothedChroma24 {};
    std::array<float, 12> bassChroma12 {};
    std::array<float, 24> maqamHistogram {};

    mutable juce::SpinLock stateLock;
    juce::String currentChord { "--" };
    juce::String currentMaqam { "Listening..." };
    juce::String rukoozNote { "--" };
    float currentConfidence { 0.0f };
    std::vector<int> currentMidiNotes;
    std::vector<DetectedChordEvent> chordHistory;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MajedAudioProcessor)
};
