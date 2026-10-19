#pragma once

#include <juce_core/juce_core.h>
#include <cmath>
#include <vector>

namespace poman
{
// YIN fundamental-frequency estimator over a mono window (usually decimated). Owns its scratch
// buffer: call reserve() with the largest window first and detect() never allocates, so the
// harmonizer can run it on the audio thread while the tuner runs it on the UI thread.
class YinDetector
{
public:
    void reserve (int maxLength) { difference.reserve ((size_t) maxLength + 2); }

    // Hz of the strongest periodicity in [minHz, maxHz], or 0 when the window is too quiet or aperiodic.
    float detect (const float* x, int length, double rate, float minHz, float maxHz,
                  float threshold = 0.15f, float minRms = 0.002f)
    {
        double energy = 0.0;
        for (int i = 0; i < length; ++i)
            energy += (double) x[i] * x[i];

        if (length <= 0 || std::sqrt (energy / length) < minRms)
            return 0.0f;

        const int tauMin = juce::jmax (2, (int) (rate / maxHz));
        const int tauMax = juce::jmin (length / 2, (int) (rate / minHz));
        const int integration = length - tauMax;
        if (tauMax <= tauMin + 1 || integration <= 0)
            return 0.0f;

        difference.assign ((size_t) tauMax + 2, 0.0f);
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < integration; ++j)
            {
                const float d = x[j] - x[j + tau];
                sum += d * d;
            }
            difference[(size_t) tau] = sum;
        }

        // Cumulative mean normalised difference.
        float running = 0.0f;
        difference[0] = 1.0f;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            running += difference[(size_t) tau];
            difference[(size_t) tau] = running > 0.0f ? difference[(size_t) tau] * (float) tau / running : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (difference[(size_t) tau] < threshold)
            {
                while (tau + 1 <= tauMax && difference[(size_t) tau + 1] < difference[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
            return 0.0f;

        const float a = difference[(size_t) best - 1], b = difference[(size_t) best], c = difference[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        const float refined = (float) best + (std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f);
        return (float) (rate / refined);
    }

private:
    std::vector<float> difference;
};

inline float frequencyToMidi (float hz) { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }

inline juce::String midiNoteName (int midiNote)
{
    static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
    return juce::String (names[((midiNote % 12) + 12) % 12]) + juce::String (midiNote / 12 - 1);
}
} // namespace poman
