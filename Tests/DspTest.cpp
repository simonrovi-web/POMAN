// Offline render of every amp model and drive type: checks for NaN/silence/runaway levels
// and prints RMS so the models can be level-matched.
#include "dsp/AmpModel.h"
#include "dsp/Overdrive.h"
#include <cstdio>

namespace
{
// Decaying plucked chord (E power chord) at a DI-like level.
std::vector<float> makePluck (double sampleRate, int numSamples)
{
    std::vector<float> out ((size_t) numSamples);
    const float freqs[] { 82.41f, 123.47f, 164.81f };
    for (int i = 0; i < numSamples; ++i)
    {
        const double t = i / sampleRate;
        float s = 0.0f;
        for (auto f : freqs)
            for (int h = 1; h <= 6; ++h)
                s += (float) (std::sin (juce::MathConstants<double>::twoPi * f * h * t) / (h * h));
        out[(size_t) i] = 0.12f * s * (float) std::exp (-2.5 * t);
    }
    return out;
}

struct Stats { float rmsDb, peak; bool finite; };

Stats measure (const std::vector<float>& x, int skip)
{
    double sum = 0.0;
    float peak = 0.0f;
    bool finite = true;
    for (size_t i = (size_t) skip; i < x.size(); ++i)
    {
        finite = finite && std::isfinite (x[i]);
        sum += (double) x[i] * x[i];
        peak = std::max (peak, std::abs (x[i]));
    }
    return { juce::Decibels::gainToDecibels ((float) std::sqrt (sum / (double) (x.size() - (size_t) skip)), -120.0f), peak, finite };
}

bool check (const char* label, const Stats& s)
{
    const bool ok = s.finite && s.rmsDb > -45.0f && s.peak < 2.0f;
    std::printf ("%s  %-22s rms %6.1f dB  peak %5.2f\n", ok ? "PASS" : "FAIL", label, s.rmsDb, s.peak);
    return ok;
}
} // namespace

int main()
{
    constexpr double sampleRate = 48000.0;
    constexpr int blockSize = 128;
    const auto input = makePluck (sampleRate, (int) sampleRate * 2);
    int failures = 0;

    std::printf ("-- Amp models (gain 6, tone flat, master 5) --\n");
    const auto models = poman::AmpModel::modelNames();
    for (int m = 0; m < models.size(); ++m)
    {
        poman::AmpModel amp;
        poman::AmpParams p;
        p.channel = m;
        p.gain = 6.0f;
        amp.prepare (sampleRate, blockSize);
        amp.setParams (p);

        auto buffer = input;
        for (size_t start = 0; start < buffer.size(); start += blockSize)
        {
            float* channels[] { buffer.data() + start };
            juce::dsp::AudioBlock<float> block (channels, 1, (size_t) juce::jmin<size_t> (blockSize, buffer.size() - start));
            amp.process (block);
        }
        failures += check (models[m].toRawUTF8(), measure (buffer, 2400)) ? 0 : 1;
    }

    std::printf ("-- Drive types (drive 0.5, tone 0.5, level 0 dB) --\n");
    const auto types = poman::Overdrive::typeNames();
    for (int t = 0; t < types.size(); ++t)
    {
        poman::Overdrive od;
        od.prepare (sampleRate);
        od.setParams ((poman::Overdrive::Type) t, 0.5f, 0.5f, 0.0f);
        auto buffer = input;
        od.process (buffer.data(), (int) buffer.size());
        failures += check (types[t].toRawUTF8(), measure (buffer, 2400)) ? 0 : 1;
    }

    std::printf ("-- Dry input --\n");
    check ("input", measure (input, 2400));
    return failures;
}
