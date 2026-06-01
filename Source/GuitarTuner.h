#pragma once

#include <cmath>
#include <array>
#include <atomic>
#include <vector>
#include <JuceHeader.h>
#include "YinPitchDetector.h"

//==============================================================================
// Note detection: frequency -> note name + cents deviation
//==============================================================================
struct NoteInfo
{
    const char* noteName = "";
    int octave = 0;
    float centsDeviation = 0.0f;   // negative=flat, positive=sharp
    float targetFrequency = 0.0f;
    int midiNote = -1;
    bool valid = false;
};

inline NoteInfo frequencyToNote(float frequency)
{
    static const char* noteNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    NoteInfo info;
    if (frequency <= 0.0f)
        return info;

    float semitonesFromA4 = 12.0f * std::log2(frequency / 440.0f);
    int nearestSemitone = static_cast<int>(std::round(semitonesFromA4));
    info.centsDeviation = (semitonesFromA4 - static_cast<float>(nearestSemitone)) * 100.0f;
    info.targetFrequency = 440.0f * std::pow(2.0f, nearestSemitone / 12.0f);
    info.midiNote = 69 + nearestSemitone;

    int noteIndex = ((info.midiNote % 12) + 12) % 12;
    info.octave = (info.midiNote / 12) - 1;
    info.noteName = noteNames[noteIndex];
    info.valid = true;
    return info;
}

// Convert MIDI note number to note name string (e.g., 40 -> "E2")
inline juce::String midiNoteToString(int midiNote)
{
    static const char* noteNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };
    int noteIndex = ((midiNote % 12) + 12) % 12;
    int octave = (midiNote / 12) - 1;
    return juce::String(noteNames[noteIndex]) + juce::String(octave);
}

// Cents deviation from a specific target MIDI note
inline float centsFromTarget(float frequency, int targetMidiNote)
{
    float targetFreq = 440.0f * std::pow(2.0f, (targetMidiNote - 69) / 12.0f);
    return 1200.0f * std::log2(frequency / targetFreq);
}

//==============================================================================
// Tuning presets
//==============================================================================
struct TuningPreset
{
    const char* name;
    std::array<int, 6> midiNotes;  // string 6 (low) to string 1 (high)
};

inline const std::vector<TuningPreset>& getTuningPresets()
{
    static const std::vector<TuningPreset> presets = {
        // name                    6     5     4     3     2     1
        { "Standard E",         {{ 40,   45,   50,   55,   59,   64  }} },
        { "Drop D",             {{ 38,   45,   50,   55,   59,   64  }} },
        { "Open G",             {{ 38,   43,   50,   55,   59,   62  }} },
        { "Open D",             {{ 38,   45,   50,   54,   57,   62  }} },
        { "DADGAD",             {{ 38,   45,   50,   55,   57,   62  }} },
        { "Open E",             {{ 40,   47,   52,   56,   59,   64  }} },
        { "Half Step Down",     {{ 39,   44,   49,   54,   58,   63  }} },
        { "Full Step Down",     {{ 38,   43,   48,   53,   57,   62  }} },
    };
    return presets;
}

// Free Chromatic is index -1 (no preset selected)
static constexpr int kFreeChromatic = -1;

//==============================================================================
// TunerEngine: runs on audio thread, accumulates samples, detects pitch
//==============================================================================
class TunerEngine
{
public:
    static constexpr int kRingSize = 4096;
    static constexpr int kAnalysisSize = 2048;
    static constexpr int kHopSize = 1024;

    void prepare(double sampleRate)
    {
        sr = static_cast<float>(sampleRate);
        detector = YinPitchDetector(kAnalysisSize, sr, 0.15f);
        ringBuffer.fill(0.0f);
        writePos = 0;
        hopCount = 0;
        silenceSamples = 0;
        silenceThresholdSamples = static_cast<int>(sr * 0.5f);
    }

    void process(const float* data, int numSamples)
    {
        if (!active.load(std::memory_order_relaxed))
            return;

        for (int i = 0; i < numSamples; ++i)
        {
            ringBuffer[static_cast<size_t>(writePos)] = data[i];
            writePos = (writePos + 1) % kRingSize;
            ++hopCount;

            if (std::abs(data[i]) < 0.003f)
                ++silenceSamples;
            else
                silenceSamples = 0;
        }

        if (hopCount >= kHopSize)
        {
            hopCount = 0;

            if (silenceSamples >= silenceThresholdSamples)
            {
                detectedFrequency.store(-1.0f, std::memory_order_relaxed);
                return;
            }

            int readPos = (writePos - kAnalysisSize + kRingSize) % kRingSize;
            for (int j = 0; j < kAnalysisSize; ++j)
                analysisBuffer[static_cast<size_t>(j)] =
                    ringBuffer[static_cast<size_t>((readPos + j) % kRingSize)];

            float freq = detector.detectPitch(analysisBuffer.data(), kAnalysisSize);
            detectedFrequency.store(freq, std::memory_order_relaxed);
        }
    }

    std::atomic<bool>  active{false};
    std::atomic<float> detectedFrequency{-1.0f};

private:
    YinPitchDetector detector{kAnalysisSize, 48000.0f};
    float sr = 48000.0f;

    std::array<float, kRingSize> ringBuffer{};
    std::array<float, kAnalysisSize> analysisBuffer{};
    int writePos = 0;
    int hopCount = 0;
    int silenceSamples = 0;
    int silenceThresholdSamples = 24000;
};
