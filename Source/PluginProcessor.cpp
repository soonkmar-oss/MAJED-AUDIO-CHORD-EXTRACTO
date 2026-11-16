#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>
#include <algorithm>

MajedAudioProcessor::MajedAudioProcessor()
    : AudioProcessor (BusesProperties()
                      .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                      .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      forwardFFT (fftOrder),
      window (fftSize, juce::dsp::WindowingFunction<float>::hann)
{
}

void MajedAudioProcessor::prepareToPlay (double sampleRate, int)
{
    currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    fifoIndex = 0;
    fifo.fill (0.0f);
    fftData.fill (0.0f);
    chroma24.fill (0.0f);
    smoothedChroma24.fill (0.0f);
    bassChroma12.fill (0.0f);
    maqamHistogram.fill (0.0f);
}

bool MajedAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
        && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return layouts.getMainOutputChannelSet() == layouts.getMainInputChannelSet();
}

void MajedAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;
    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    if (numChannels == 0 || numSamples == 0)
        return;

    for (int i = 0; i < numSamples; ++i)
    {
        float mono = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            mono += buffer.getReadPointer (ch)[i];

        mono /= static_cast<float> (numChannels);
        pushNextSampleIntoFifo (mono);
    }
}

void MajedAudioProcessor::pushNextSampleIntoFifo (float sample)
{
    fifo[fifoIndex++] = sample;

    if (fifoIndex == fftSize)
    {
        std::copy (fifo.begin(), fifo.end(), fftData.begin());
        processFFTFrame();

        std::move (fifo.begin() + fftSize / 2, fifo.end(), fifo.begin());
        fifoIndex = fftSize / 2;
    }
}

void MajedAudioProcessor::processFFTFrame()
{
    window.multiplyWithWindowingTable (fftData.data(), fftSize);
    forwardFFT.performFrequencyOnlyForwardTransform (fftData.data());

    chroma24.fill (0.0f);
    bassChroma12.fill (0.0f);

    float totalEnergy = 0.0f;
    const int maxBin = fftSize / 2;
    const double binWidth = currentSampleRate / static_cast<double> (fftSize);

    for (int bin = 1; bin < maxBin; ++bin)
    {
        double freq = bin * binWidth;
        if (freq < 45.0 || freq > 4200.0)
            continue;

        float mag = fftData[bin];
        if (mag < 0.001f)
            continue;

        float power = mag * mag;
        totalEnergy += power;

        double midiExact = 69.0 + 12.0 * std::log2 (freq / 440.0);
        int qBin = juce::roundToInt (midiExact * 2.0) % 24;
        if (qBin < 0) qBin += 24;

        chroma24[qBin] += power;

        if (freq >= 45.0 && freq <= 220.0)
        {
            int semiBin = juce::roundToInt (midiExact) % 12;
            if (semiBin < 0) semiBin += 12;
            bassChroma12[semiBin] += power * 1.5f;
        }
    }

    if (totalEnergy < 0.05f)
    {
        const juce::SpinLock::ScopedLockType lock (stateLock);
        currentConfidence *= 0.8f;
        return;
    }

    float maxVal = *std::max_element (chroma24.begin(), chroma24.end());
    if (maxVal > 0.0f)
    {
        for (int i = 0; i < 24; ++i)
        {
            float norm = chroma24[i] / maxVal;
            smoothedChroma24[i] = smoothedChroma24[i] * 0.72f + norm * 0.28f;
            maqamHistogram[i] = maqamHistogram[i] * 0.985f + norm * 0.015f;
        }
    }

    analyzeHarmonyAndMaqam();
}

void MajedAudioProcessor::analyzeHarmonyAndMaqam()
{
    const juce::StringArray noteNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    std::array<float, 12> chroma12 {};
    for (int i = 0; i < 12; ++i)
    {
        int prevQ = (i * 2 - 1 + 24) % 24;
        int exact = i * 2;
        int nextQ = (i * 2 + 1) % 24;
        chroma12[i] = smoothedChroma24[exact] + 0.5f * (smoothedChroma24[prevQ] + smoothedChroma24[nextQ]);
    }

    int bassNote = -1;
    float maxBass = 0.0f;
    for (int i = 0; i < 12; ++i)
    {
        if (bassChroma12[i] > maxBass)
        {
            maxBass = bassChroma12[i];
            bassNote = i;
        }
    }

    struct ChordTemplate { const char* suffix; std::vector<int> intervals; };
    static const std::vector<ChordTemplate> templates = {
        { "",      { 0, 4, 7 } },
        { "m",     { 0, 3, 7 } },
        { "7",     { 0, 4, 7, 10 } },
        { "m7",    { 0, 3, 7, 10 } },
        { "maj7",  { 0, 4, 7, 11 } },
        { "dim",   { 0, 3, 6 } },
        { "sus4",  { 0, 5, 7 } },
        { "sus2",  { 0, 2, 7 } }
    };

    float bestChordScore = -1.0f;
    int bestRoot = 0;
    int bestTemplateIdx = 0;

    for (int root = 0; root < 12; ++root)
    {
        for (size_t t = 0; t < templates.size(); ++t)
        {
            float inChord = 0.0f;
            float outChord = 0.0f;
            std::array<bool, 12> active {};

            for (int interval : templates[t].intervals)
            {
                int pc = (root + interval) % 12;
                active[pc] = true;
                inChord += chroma12[pc];
            }

            for (int i = 0; i < 12; ++i)
                if (! active[i])
                    outChord += chroma12[i];

            float score = (inChord / templates[t].intervals.size()) - (outChord / (12.0f - templates[t].intervals.size())) * 0.65f;
            if (bassNote == root)
                score += 0.12f;

            if (score > bestChordScore)
            {
                bestChordScore = score;
                bestRoot = root;
                bestTemplateIdx = static_cast<int> (t);
            }
        }
    }

    juce::String detectedChordStr = noteNames[bestRoot] + templates[bestTemplateIdx].suffix;
    std::vector<int> midiVoicing;

    int rootMidi = 60 + bestRoot;
    for (int interval : templates[bestTemplateIdx].intervals)
        midiVoicing.push_back (rootMidi + interval);

    if (bassNote >= 0 && bassNote != bestRoot && maxBass > 0.1f)
    {
        for (int interval : templates[bestTemplateIdx].intervals)
        {
            if ((bestRoot + interval) % 12 == bassNote)
            {
                detectedChordStr += "/" + noteNames[bassNote];
                midiVoicing.insert (midiVoicing.begin(), 48 + bassNote);
                break;
            }
        }
    }

    struct MaqamProfile { const char* name; std::vector<int> steps24; };
    static const std::vector<MaqamProfile> maqamat = {
        { "Bayati (بيات)",      { 0, 3, 6, 10, 14, 16, 20 } },
        { "Rast (راست)",        { 0, 4, 7, 10, 14, 18, 21 } },
        { "Hijaz (حجاز)",       { 0, 2, 8, 10, 14, 16, 20 } },
        { "Saba (صبا)",         { 0, 3, 6, 8, 14, 16, 20 } },
        { "Kurd (كرد)",         { 0, 2, 6, 10, 14, 16, 20 } },
        { "Nahawand (نهاوند)",  { 0, 4, 6, 10, 14, 16, 22 } },
        { "Sikah (سيكاه)",      { 0, 3, 7, 10, 13, 17, 20 } },
        { "Ajam (عجم)",         { 0, 4, 8, 10, 14, 18, 22 } }
    };

    float bestMaqamScore = -1.0f;
    int bestRukooz24 = 0;
    int bestMaqamIdx = 0;

    for (int rukooz = 0; rukooz < 24; rukooz += 2)
    {
        for (int shift = 0; shift <= 1; ++shift)
        {
            int tonic24 = (rukooz + shift) % 24;
            for (size_t m = 0; m < maqamat.size(); ++m)
            {
                float matchSum = 0.0f;
                for (int step : maqamat[m].steps24)
                    matchSum += maqamHistogram[(tonic24 + step) % 24];

                matchSum += maqamHistogram[tonic24] * 0.45f;
                matchSum += maqamHistogram[(tonic24 + 10) % 24] * 0.25f;

                if (matchSum > bestMaqamScore)
                {
                    bestMaqamScore = matchSum;
                    bestRukooz24 = tonic24;
                    bestMaqamIdx = static_cast<int> (m);
                }
            }
        }
    }

    juce::String rukoozStr = noteNames[(bestRukooz24 / 2) % 12];
    if (bestRukooz24 % 2 != 0)
        rukoozStr += " (Quarter-Flat / نصف بيمول)";

    float conf = juce::jlimit (0.0f, 1.0f, bestChordScore * 1.3f);

    const juce::SpinLock::ScopedLockType lock (stateLock);
    currentChord = detectedChordStr;
    currentMaqam = maqamat[bestMaqamIdx].name;
    rukoozNote = rukoozStr;
    currentConfidence = conf;
    currentMidiNotes = midiVoicing;

    if (conf > 0.35f)
    {
        if (chordHistory.empty() || chordHistory.back().chordName != detectedChordStr)
        {
            chordHistory.push_back ({ detectedChordStr, midiVoicing });
            if (chordHistory.size() > 16)
                chordHistory.erase (chordHistory.begin());
        }
    }
}

juce::String MajedAudioProcessor::getCurrentChordName() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return currentChord;
}

juce::String MajedAudioProcessor::getCurrentMaqamName() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return currentMaqam;
}

juce::String MajedAudioProcessor::getRukoozNoteName() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return rukoozNote;
}

float MajedAudioProcessor::getConfidence() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return currentConfidence;
}

std::array<float, 24> MajedAudioProcessor::getQuarterToneSpectrum() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return smoothedChroma24;
}

std::vector<DetectedChordEvent> MajedAudioProcessor::getChordHistory() const
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    return chordHistory;
}

void MajedAudioProcessor::clearHistory()
{
    const juce::SpinLock::ScopedLockType lock (stateLock);
    chordHistory.clear();
    maqamHistogram.fill (0.0f);
}

juce::File MajedAudioProcessor::generateMidiFileForDrag()
{
