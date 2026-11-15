#include "PluginProcessor.h"
#include "PluginEditor.h"

MajedAudioProcessor::MajedAudioProcessor()
    : AudioProcessor (BusesProperties()
        .withInput ("Input", juce::AudioChannelSet::stereo(), true)
        .withOutput ("Output", juce::AudioChannelSet::stereo(), true))
{
    analysisBuffer.setSize (1, 4096);
    analysisBuffer.clear();
}

void MajedAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate;
    analysisBuffer.clear();
    writePosition = 0;
}

bool MajedAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    return layouts.getMainInputChannelSet() == juce::AudioChannelSet::mono()
        || layouts.getMainInputChannelSet() == juce::AudioChannelSet::stereo();
}

void MajedAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    // Pass audio through unchanged. Analyse a mono sum in the background buffer.
    const int channels = buffer.getNumChannels();
    const int samples = buffer.getNumSamples();

    for (int i = 0; i < samples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < channels; ++ch)
            mono += buffer.getReadPointer(ch)[i];
        mono /= (float) juce::jmax(1, channels);

        analysisBuffer.setSample (0, writePosition++, mono);

        if (writePosition >= analysisBuffer.getNumSamples())
        {
            analyseFrame();
            writePosition = 0;
        }
    }

    if (midiExportRequested.exchange(false))
    {
        // MIDI note suggestion is emitted as a short note event.
        const int note = detectedMidi.load();
        if (note >= 0)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            midi.addEvent (juce::MidiMessage::noteOff (1, note), juce::jmin (samples - 1, 2400));
        }
    }
}

float MajedAudioProcessor::estimatePitchHz (const float* data, int n) const
{
    // Lightweight autocorrelation pitch estimator for a monophonic vocal.
    // This is a prototype; production version will add YIN/FFT and voiced/unvoiced handling.
    double energy = 0.0;
    for (int i = 0; i < n; ++i) energy += data[i] * data[i];
    if (energy / n < 0.000002) return 0.0f;

    const int minLag = (int) (currentSampleRate / 1000.0); // up to ~1000 Hz
    const int maxLag = juce::jmin (n / 2, (int) (currentSampleRate / 65.0));
    float best = 0.0f;
    int bestLag = 0;

    for (int lag = minLag; lag <= maxLag; ++lag)
    {
        double corr = 0.0, normA = 0.0, normB = 0.0;
        for (int i = 0; i < n - lag; i += 2)
        {
            const float a = data[i], b = data[i + lag];
            corr += a * b; normA += a * a; normB += b * b;
        }
        const double denom = std::sqrt (normA * normB) + 1.0e-12;
        const float score = (float) (corr / denom);
        if (score > best) { best = score; bestLag = lag; }
    }

    confidence.store (best);
    if (bestLag == 0 || best < 0.55f) return 0.0f;
    return (float) currentSampleRate / (float) bestLag;
}

void MajedAudioProcessor::analyseFrame()
{
    const float hz = estimatePitchHz (analysisBuffer.getReadPointer (0), analysisBuffer.getNumSamples());
    if (hz <= 0.0f) return;

    const int midiNote = juce::roundToInt (69.0 + 12.0 * std::log2 (hz / 440.0));
    detectedMidi.store (midiNote);

    // Accumulate a decaying pitch-class profile from the monophonic melody.
    // Rank all 24 major/minor triads against the observed melody-note evidence.
    // This is a chord-candidate heuristic, not full harmonic or maqam recognition.
    for (auto& weight : pitchClassWeights)
        weight *= 0.985f;

    const int pc = ((midiNote % 12) + 12) % 12;
    pitchClassWeights[pc] += juce::jlimit (0.1f, 1.0f, confidence.load());

    float bestScore = -1.0e9f;
    int bestRoot = pc;
    int bestMinor = 0;

    for (int root = 0; root < 12; ++root)
    {
        for (int minor = 0; minor <= 1; ++minor)
        {
            const int third = (root + (minor ? 3 : 4)) % 12;
            const int fifth = (root + 7) % 12;
            float score = 2.0f * pitchClassWeights[root]
                        + 1.5f * pitchClassWeights[third]
                        + 1.2f * pitchClassWeights[fifth];

            // Mildly penalize evidence outside the candidate triad.
            float total = 0.0f;
            for (float w : pitchClassWeights) total += w;
            score -= 0.12f * juce::jmax (0.0f, total
                     - pitchClassWeights[root]
                     - pitchClassWeights[third]
                     - pitchClassWeights[fifth]);

            if (score > bestScore)
            {
                bestScore = score;
                bestRoot = root;
                bestMinor = minor;
            }
        }
    }

    suggestedRoot.store (bestRoot);
    suggestedQuality.store (bestMinor);
}

juce::String MajedAudioProcessor::noteNameFromMidi (int midi) const
{
    static const char* names[] = { "C", "C#/Db", "D", "D#/Eb", "E", "F",
                                   "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
    return names[((midi % 12) + 12) % 12] + juce::String (midi / 12 - 1);
}

juce::String MajedAudioProcessor::getDetectedNote() const
{
    const int n = detectedMidi.load();
    return n < 0 ? "--" : noteNameFromMidi (n);
}

juce::String MajedAudioProcessor::getSuggestedChord() const
{
    const int root = suggestedRoot.load();
    if (root < 0) return "بانتظار الفوكل...";
    static const char* names[] = { "C", "C#/Db", "D", "D#/Eb", "E", "F",
                                   "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B" };
    return juce::String (names[root]) + (suggestedQuality.load() ? "m" : "");
}

juce::String MajedAudioProcessor::getDetectedScale() const
{
    return detectedMidi.load() < 0 ? "غير محدد" : "تقدير أولي — يحتاج تحليل اللحن كاملاً";
}

float MajedAudioProcessor::getConfidence() const { return confidence.load(); }
void MajedAudioProcessor::requestMidiExport() { midiExportRequested.store (true); }

juce::AudioProcessorEditor* MajedAudioProcessor::createEditor()
{
    return new MajedAudioEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MajedAudioProcessor();
}
