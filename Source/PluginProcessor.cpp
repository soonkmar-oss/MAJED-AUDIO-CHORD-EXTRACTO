#include "PluginProcessor.h"
#include "PluginEditor.h"

MajedAudioProcessor::MajedAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    analysisBuffer.setSize (1, 4096);
    analysisBuffer.clear();
}

void MajedAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    analysisBuffer.setSize (1, 4096);
    analysisBuffer.clear();
    writePosition = 0;
}

bool MajedAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    if (layouts.getMainOutputChannelSet() != layouts.getMainInputChannelSet())
        return false;

    return true;
}

void MajedAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels > 0 && numSamples > 0)
    {
        auto* dest = analysisBuffer.getWritePointer (0);
        const int bufSize = analysisBuffer.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
        {
            float mono = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                mono += buffer.getReadPointer (ch)[i];

            mono /= static_cast<float> (numChannels);
            dest[writePosition] = mono;
            writePosition = (writePosition + 1) % bufSize;
        }

        analyseFrame();
    }

    if (midiExportRequested.exchange (false))
    {
        int root = suggestedRoot.load();
        int quality = suggestedQuality.load();
        if (root >= 0)
        {
            int baseNote = 60 + root;
            int third = baseNote + (quality == 1 ? 3 : 4);
            int fifth = baseNote + 7;

            midiMessages.addEvent (juce::MidiMessage::noteOn (1, baseNote, (juce::uint8) 100), 0);
            midiMessages.addEvent (juce::MidiMessage::noteOn (1, third,    (juce::uint8) 100), 0);
            midiMessages.addEvent (juce::MidiMessage::noteOn (1, fifth,    (juce::uint8) 100), 0);
        }
    }
}

void MajedAudioProcessor::analyseFrame()
{
    const float* data = analysisBuffer.getReadPointer (0);
    const int n = analysisBuffer.getNumSamples();

    float hz = estimatePitchHz (data, n);
    if (hz > 50.0f && hz < 2000.0f)
    {
        int midi = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
        detectedMidi.store (midi);

        int pc = ((midi % 12) + 12) % 12;
        pitchClassWeights = pitchClassWeights * 0.85f + 0.15f;
        suggestedRoot.store (pc);
        suggestedQuality.store ((pc == 2 || pc == 4 || pc == 9) ? 1 : 0);
    }
}

float MajedAudioProcessor::estimatePitchHz (const float* data, int n)
{
    float rms = 0.0f;
    for (int i = 0; i < n; ++i)
        rms += data[i] * data[i];

    rms = std::sqrt (rms / static_cast<float> (n));
    if (rms < 0.01f)
    {
        confidence.store (0.0f);
        return 0.0f;
    }

    int minLag = static_cast<int> (currentSampleRate / 1000.0);
    int maxLag = static_cast<int> (currentSampleRate / 60.0);
    if (maxLag >= n / 2) maxLag = n / 2 - 1;

    float bestCorr = 0.0f;
    int bestLag = 0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        float corr = 0.0f;
        float norm1 = 0.0f;
        float norm2 = 0.0f;

        for (int i = 0; i < n - lag; ++i)
        {
            corr  += data[i] * data[i + lag];
            norm1 += data[i] * data[i];
            norm2 += data[i + lag] * data[i + lag];
        }

        float denom = std::sqrt (norm1 * norm2) + 1e-9f;
        float r = corr / denom;

        if (r > bestCorr)
        {
            bestCorr = r;
            bestLag = lag;
        }
    }

    confidence.store (bestCorr);
    if (bestLag > 0 && bestCorr > 0.5f)
        return static_cast<float> (currentSampleRate / static_cast<double> (bestLag));

    return 0.0f;
}

juce::String MajedAudioProcessor::noteNameFromMidi (int midi) const
{
    if (midi < 0) return "--";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    int pc = ((midi % 12) + 12) % 12;
    int octave = (midi / 12) - 1;
    return juce::String (names[pc]) + juce::String (octave);
}

juce::String MajedAudioProcessor::getDetectedNote() const
{
    return noteNameFromMidi (detectedMidi.load());
}

juce::String MajedAudioProcessor::getSuggestedChord() const
{
    int root = suggestedRoot.load();
    if (root < 0) return "--";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[root % 12]) + (suggestedQuality.load() == 1 ? "m" : "");
}

juce::String MajedAudioProcessor::getDetectedScale() const
{
    int root = suggestedRoot.load();
    if (root < 0) return "Listening...";
    static const char* names[] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[root % 12]) + (suggestedQuality.load() == 1 ? " Bayati / Minor" : " Rast / Major");
}

float MajedAudioProcessor::getConfidence() const
{
    return confidence.load();
}

void MajedAudioProcessor::requestMidiExport()
{
    midiExportRequested.store (true);
}

juce::AudioProcessorEditor* MajedAudioProcessor::createEditor()
{
    return new MajedAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MajedAudioProcessor();
}
