#pragma once

#include <vector>
#include <cmath>
#include <algorithm>

//==============================================================================
// YIN monophonic pitch detection algorithm
// Cheveigné & Kawahara (2002) — optimized for guitar (60-400 Hz)
//==============================================================================
class YinPitchDetector
{
public:
    YinPitchDetector(int bufSize = 2048, float sr = 48000.0f, float thresh = 0.15f)
        : bufferSize(bufSize), sampleRate(sr), threshold(thresh)
    {
        halfBuffer = bufferSize / 2;
        tauMax = static_cast<int>(sampleRate / 60.0f) + 1; // detect down to ~60 Hz
        if (tauMax > halfBuffer)
            tauMax = halfBuffer;
        yinBuffer.resize(static_cast<size_t>(tauMax + 1), 0.0f);
    }

    void setSampleRate(float sr)
    {
        sampleRate = sr;
        tauMax = static_cast<int>(sampleRate / 60.0f) + 1;
        if (tauMax > halfBuffer)
            tauMax = halfBuffer;
        yinBuffer.resize(static_cast<size_t>(tauMax + 1), 0.0f);
    }

    // Returns detected frequency in Hz, or -1.0f if no valid pitch
    float detectPitch(const float* audio, int numSamples) const
    {
        if (numSamples < bufferSize)
            return -1.0f;

        // Silence detection: RMS check
        float sumSq = 0.0f;
        for (int i = 0; i < bufferSize; ++i)
            sumSq += audio[i] * audio[i];

        float rms = std::sqrt(sumSq / static_cast<float>(bufferSize));
        if (rms < 0.005f) // ~-46 dB
            return -1.0f;

        // Step 1 & 2: difference function + cumulative mean normalized
        auto& yb = const_cast<std::vector<float>&>(yinBuffer);
        yb[0] = 1.0f;
        float runningSum = 0.0f;

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            float diff = 0.0f;
            for (int j = 0; j < halfBuffer; ++j)
            {
                float delta = audio[j] - audio[j + tau];
                diff += delta * delta;
            }
            runningSum += diff;
            yb[static_cast<size_t>(tau)] = (runningSum > 0.0f)
                ? diff * static_cast<float>(tau) / runningSum
                : 1.0f;
        }

        // Step 3: absolute threshold — find first dip below threshold
        int tauEstimate = -1;
        for (int tau = 2; tau <= tauMax; ++tau)
        {
            if (yb[static_cast<size_t>(tau)] < threshold)
            {
                // Walk to local minimum
                while (tau + 1 <= tauMax
                       && yb[static_cast<size_t>(tau + 1)] < yb[static_cast<size_t>(tau)])
                    ++tau;
                tauEstimate = tau;
                break;
            }
        }

        if (tauEstimate < 0)
            return -1.0f;

        // Step 4: parabolic interpolation for sub-sample accuracy
        float betterTau = static_cast<float>(tauEstimate);
        if (tauEstimate > 0 && tauEstimate < tauMax)
        {
            float s0 = yb[static_cast<size_t>(tauEstimate - 1)];
            float s1 = yb[static_cast<size_t>(tauEstimate)];
            float s2 = yb[static_cast<size_t>(tauEstimate + 1)];
            float denom = 2.0f * (s0 - 2.0f * s1 + s2);
            if (std::abs(denom) > 1e-12f)
                betterTau += (s0 - s2) / denom;
        }

        // Step 5: frequency
        float freq = sampleRate / betterTau;

        // Reject out-of-guitar-range
        if (freq < 55.0f || freq > 420.0f)
            return -1.0f;

        return freq;
    }

private:
    int bufferSize;
    float sampleRate;
    float threshold;
    int tauMax;
    int halfBuffer;
    std::vector<float> yinBuffer;
};
