#pragma once

#include "DspCommon.h"
#include <complex>

namespace poman
{
// Ten octave-band graphic EQ (ISO centres) plus low cut, high cut and output level.
// designFilters() is the single owner of the filter maths: the audio path and the UI response
// curve both use it, so the drawn curve is exactly what is processed.
class GraphicEQ
{
public:
    static constexpr int numBands = 10;
    static constexpr std::array<float, numBands> bandFrequencies { 31.5f, 63.0f, 125.0f, 250.0f, 500.0f,
                                                                    1000.0f, 2000.0f, 4000.0f, 8000.0f, 16000.0f };
    static juce::StringArray bandLabels() { return { "31", "63", "125", "250", "500", "1k", "2k", "4k", "8k", "16k" }; }

    struct Settings
    {
        std::array<float, numBands> gainsDb {};
        float lowCutHz = 20.0f, highCutHz = 20000.0f, levelDb = 0.0f;

        bool operator== (const Settings& o) const
        {
            return gainsDb == o.gainsDb && lowCutHz == o.lowCutHz && highCutHz == o.highCutHz && levelDb == o.levelDb;
        }
        bool operator!= (const Settings& o) const { return ! (*this == o); }
    };

    static constexpr int numFilters = numBands + 2; // bands, low cut, high cut
    using Design = std::array<std::array<float, 6>, numFilters>;

    // Q of about 1.4 gives one-octave bands that overlap smoothly when neighbours move together.
    static Design designFilters (const Settings& s, double sampleRate)
    {
        constexpr std::array<float, 6> unity { 1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f };
        const float nyquistGuard = (float) sampleRate * 0.45f;
        Design d;

        for (int b = 0; b < numBands; ++b)
        {
            const float f = bandFrequencies[(size_t) b];
            const float g = s.gainsDb[(size_t) b];
            d[(size_t) b] = (f < nyquistGuard && std::abs (g) > 0.01f)
                                ? ArrayCoeffs::makePeakFilter (sampleRate, f, 1.41f, dbToGain (g))
                                : unity;
        }

        d[numBands] = s.lowCutHz > 20.5f ? ArrayCoeffs::makeHighPass (sampleRate, s.lowCutHz, 0.707f) : unity;
        d[numBands + 1] = s.highCutHz < juce::jmin (19900.0f, nyquistGuard) ? ArrayCoeffs::makeLowPass (sampleRate, s.highCutHz, 0.707f) : unity;
        return d;
    }

    // Response of a design (including level) in dB at `hz`.
    static float responseDb (const Design& d, const Settings& s, double sampleRate, double hz)
    {
        const auto w = juce::MathConstants<double>::twoPi * hz / sampleRate;
        const std::complex<double> z1 = std::polar (1.0, -w), z2 = z1 * z1;
        double magnitude = 1.0;
        for (const auto& c : d)
        {
            const auto k = [&c] (size_t i) { return (double) c[i]; };
            magnitude *= std::abs ((k (0) + k (1) * z1 + k (2) * z2) / (k (3) + k (4) * z1 + k (5) * z2));
        }
        return (float) juce::Decibels::gainToDecibels (magnitude, -120.0) + s.levelDb;
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        level.reset (sampleRate, 0.05);
        current.lowCutHz = -1.0f; // force a redesign
        setSettings (pending);
        for (auto& f : filters)
            f.reset();
    }

    void setSettings (const Settings& s)
    {
        pending = s;
        level.setTargetValue (dbToGain (s.levelDb));
        if (s == current)
            return;

        current = s;
        const auto design = designFilters (s, sampleRate);
        for (size_t i = 0; i < filters.size(); ++i)
            filters[i].set (design[i]);
    }

    void process (float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            float x = samples[i];
            for (auto& f : filters)
                x = f (x);
            samples[i] = x * level.getNextValue();
        }
    }

private:
    double sampleRate = 48000.0;
    Settings current, pending;
    std::array<Biquad, numFilters> filters;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> level { 1.0f };
};
} // namespace poman
