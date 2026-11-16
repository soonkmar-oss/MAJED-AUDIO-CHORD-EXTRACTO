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

        // 50% overlap for smooth real-time tracking
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

        // Map frequency to 24-TET quarter-tone pitch class (0 = C, 1 = C+50c, 2 = C#, ...)
        double midiExact = 69.0 + 12.0 * std::log2 (freq / 440.0);
        int qBin = juce::roundToInt (midiExact * 2.0) % 24;
        if (qBin < 0) qBin += 24;

        chroma24[qBin] += power;

        // Dedicated Bass Tracker (45Hz - 220Hz) for Chord Inversions
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

    // Normalize & smooth 24-bin spectrum
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
    static const char* noteNames[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

    // Collapse 24-TET into 12-TET for chord recognition (absorbing quarter-tones into neighboring semitones)
    std::array<float, 12> chroma12 {};
    for (int i = 0; i < 12; ++i)
    {
        int prevQ = (i * 2 - 1 + 24) % 24;
        int exact = i * 2;
        int nextQ = (i * 2 + 1) % 24;
        chroma12[i] = smoothedChroma24[exact] + 0.5f * (smoothedChroma24[prevQ] + smoothedChroma24[nextQ]);
    }

    // Find strongest Bass note for Inversion detection
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

    // Chord Templates: { nameSuffix, intervals... }
    struct ChordTemplate { const char* suffix; std::vector<int> intervals; };
    static const std::vector<ChordTemplate> templates = {
        { "",      { 0, 4, 7 } },       // Major
        { "m",     { 0, 3, 7 } },       // Minor
        { "7",     { 0, 4, 7, 10 } },   // Dominant 7
        { "m7",    { 0, 3, 7, 10 } },   // Minor 7
        { "maj7",  { 0, 4, 7, 11 } },   // Major 7
        { "dim",   { 0, 3, 6 } },       // Diminished
        { "sus4",  { 0, 5, 7 } },       // Suspended 4
        { "sus2",  { 0, 2, 7 } }        // Suspended 2
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
                score += 0.12f; // Root position bonus

            if (score > bestChordScore)
            {
                bestChordScore = score;
                bestRoot = root;
                bestTemplateIdx = static_cast<int> (t);
            }
        }
    }

    // Build Chord Name + Inversion (e.g., Am/C, C/G)
    juce::String detectedChordStr = juce::String (noteNames[bestRoot]) + templates[bestTemplateIdx].suffix;
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
                detectedChordStr += "/" + juce::String (noteNames[bassNote]);
                // Place inversion bass note one octave lower in MIDI voicing
                midiVoicing.insert (midiVoicing.begin(), 48 + bassNote);
                break;
            }
        }
    }

    // Arabic Maqam Recognition using 24-TET Quarter-Tone Profiles (0..23 steps)
    struct MaqamProfile { const char* name; std::vector<int> steps24; };
    static const std::vector<MaqamProfile> maqamat = {
        { "Bayati (بيات)",      { 0, 3, 6, 10, 14, 16, 20 } },   // D - E half-flat - F - G - A - Bb - C
        { "Rast (راست)",        { 0, 4, 7, 10, 14, 18, 21 } },   // C - D - E half-flat - F - G - A - B half-flat
        { "Hijaz (حجاز)",       { 0, 2, 8, 10, 14, 16, 20 } },   // D - Eb - F# - G - A - Bb - C
        { "Saba (صبا)",         { 0, 3, 6, 8, 14, 16, 20 } },    // D - E half-flat - F - Gb - A - Bb - C
        { "Kurd (كرد)",         { 0, 2, 6, 10, 14, 16, 20 } },   // D - Eb - F - G - A - Bb - C
        { "Nahawand (نهاوند)",  { 0, 4, 6, 10, 14, 16, 22 } },   // C - D - Eb - F - G - Ab - B
        { "Sikah (سيكاه)",      { 0, 3, 7, 10, 13, 17, 20 } },   // E half-flat - F - G - A half-flat - Bb - C - D
        { "Ajam (عجم)",         { 0, 4, 8, 10, 14, 18, 22 } }    // C - D - E - F - G - A - B
    };

    float bestMaqamScore = -1.0f;
    int bestRukooz24 = 0;
    int bestMaqamIdx = 0;

    for (int rukooz = 0; rukooz < 24; rukooz += 2) // Test all 12 tonic degrees + quarter-tone Sikah
    {
        for (int shift = 0; shift <= 1; ++shift)
        {
            int tonic24 = (rukooz + shift) % 24;
            for (size_t m = 0; m < maqamat.size(); ++m)
            {
                float matchSum = 0.0f;
                for (int step : maqamat[m].steps24)
                    matchSum += maqamHistogram[(tonic24 + step) % 24];

                // Emphasize Rukooz (tonic) and 4th/5th scale degrees
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

    juce::String rukoozStr = juce::String (noteNames[(bestRukooz24 / 2) % 12]);
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
