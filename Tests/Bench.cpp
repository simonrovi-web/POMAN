// Performance and aliasing measurements for each DSP module (offline, single thread).
// CPU is reported as % of one core needed for real time at 48 kHz.
#include "dsp/AmpModel.h"
#include "dsp/CabSim.h"
#include "dsp/DrumMachine.h"
#include "dsp/GraphicEQ.h"
#include "dsp/Harmonizer.h"
#include "dsp/Modulation.h"
#include "dsp/Overdrive.h"
#include <chrono>
#include <cstdio>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int block = 128;
constexpr int seconds = 10;

template <typename Fn>
double cpuPercent (Fn&& processBlock)
{
    const int blocks = (int) (sampleRate * seconds / block);
    const auto start = std::chrono::high_resolution_clock::now();
    for (int b = 0; b < blocks; ++b)
        processBlock (b);
    const double elapsed = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - start).count();
    return 100.0 * elapsed / seconds;
}

std::vector<float> guitarSignal (int n)
{
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
        x[(size_t) i] = 0.25f * (float) (std::sin (juce::MathConstants<double>::twoPi * 82.41 * i / sampleRate)
                                         + 0.5 * std::sin (juce::MathConstants<double>::twoPi * 123.47 * i / sampleRate));
    return x;
}

// Energy that is not on a harmonic of `f0` (aliasing + noise), in dB relative to total, for a sine at f0.
float inharmonicDb (const std::vector<float>& y, double f0)
{
    constexpr int order = 15, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (size * 2, 0.0f);
    const size_t offset = y.size() - size;
    for (int i = 0; i < size; ++i)
        data[(size_t) i] = y[offset + (size_t) i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (size - 1)));
    fft.performFrequencyOnlyForwardTransform (data.data());
    double harmonic = 0.0, other = 0.0;
    const double binHz = sampleRate / size;
    for (int k = 2; k < size / 2; ++k)
    {
        const double hz = k * binHz;
        const double nearest = std::round (hz / f0) * f0;
        const double e = (double) data[(size_t) k] * data[(size_t) k];
        (std::abs (hz - nearest) < 4.0 * binHz ? harmonic : other) += e;
    }
    return (float) (10.0 * std::log10 (other / (harmonic + other)));
}
} // namespace

int main()
{
    const auto input = guitarSignal ((int) sampleRate * seconds + block);
    std::vector<float> buf (block);
    std::printf ("CPU at 48 kHz, %d-sample blocks (%% of one core)\n", block);

    const auto models = poman::AmpModel::modelNames();
    for (int m = 0; m < models.size(); ++m)
    {
        poman::AmpModel amp;
        amp.prepare (sampleRate, block);
        poman::AmpParams p;
        p.channel = m;
        p.gain = 7.0f;
        amp.setParams (p);
        const double cpu = cpuPercent ([&] (int b)
        {
            std::copy (input.begin() + b * block, input.begin() + (b + 1) * block, buf.begin());
            float* ch[] { buf.data() };
            juce::dsp::AudioBlock<float> blk (ch, 1, (size_t) block);
            amp.process (blk);
        });
        std::printf ("  amp %-16s %5.2f %%\n", models[m].toRawUTF8(), cpu);
    }

    const auto types = poman::Overdrive::typeNames();
    for (int t = 0; t < types.size(); ++t)
    {
        poman::Overdrive od;
        od.prepare (sampleRate);
        od.setParams ((poman::Overdrive::Type) t, 0.8f, 0.5f, 0.0f);
        std::printf ("  drive %-14s %5.2f %%\n", types[t].toRawUTF8(), cpuPercent ([&] (int b)
        {
            std::copy (input.begin() + b * block, input.begin() + (b + 1) * block, buf.begin());
            od.process (buf.data(), block);
        }));
    }

    {
        poman::CabSim cab;
        cab.prepare (sampleRate, block);
        std::printf ("  cab (built-in)         %5.2f %%\n", cpuPercent ([&] (int b)
        {
            std::copy (input.begin() + b * block, input.begin() + (b + 1) * block, buf.begin());
            float* ch[] { buf.data() };
            juce::dsp::AudioBlock<float> blk (ch, 1, (size_t) block);
            cab.process (blk);
        }));
    }
    {
        poman::GraphicEQ eq;
        eq.prepare (sampleRate);
        poman::GraphicEQ::Settings s;
        s.gainsDb = { 3, -2, 1, -1, 2, -3, 1, 2, -1, 1 };
        s.lowCutHz = 80.0f;
        s.highCutHz = 9000.0f;
        eq.setSettings (s);
        std::printf ("  graphic EQ             %5.2f %%\n", cpuPercent ([&] (int b)
        {
            std::copy (input.begin() + b * block, input.begin() + (b + 1) * block, buf.begin());
            eq.process (buf.data(), block);
        }));
    }
    {
        poman::Harmonizer h;
        h.prepare (sampleRate);
        poman::Harmonizer::Params hp;
        hp.level = { 0.8f, 0.6f };
        h.setParams (hp);
        std::vector<float> l (block), r (block);
        std::printf ("  harmonizer (2 voices)  %5.2f %%\n", cpuPercent ([&] (int b)
        {
            std::copy (input.begin() + b * block, input.begin() + (b + 1) * block, buf.begin());
            h.track (buf.data(), block);
            h.process (buf.data(), l.data(), r.data(), block);
        }));
    }
    for (int t = 0; t < 3; ++t)
    {
        poman::Modulation mod;
        mod.prepare (sampleRate, block);
        mod.setParams ((poman::Modulation::Type) t, 1.0f, 0.6f, 0.5f, 0.4f);
        std::vector<float> l (block), r (block);
        std::printf ("  mod %-18s %5.2f %%\n", poman::Modulation::typeNames()[t].toRawUTF8(), cpuPercent ([&] (int b)
        {
            std::copy (input.begin() + b * block, input.begin() + (b + 1) * block, l.begin());
            std::copy (l.begin(), l.end(), r.begin());
            mod.process (l.data(), r.data(), block);
        }));
    }
    for (int kit = 0; kit < 2; ++kit)
    {
        poman::DrumMachine d;
        d.setEnabled (true);
        d.setKit (kit);
        d.loadPreset (1);
        d.prepare (sampleRate);
        std::vector<float> l (block), r (block);
        std::printf ("  drums Metal (%s)%s %5.2f %%\n", poman::DrumMachine::kitNames()[kit].toRawUTF8(), kit == 0 ? "     " : " ", cpuPercent ([&] (int b)
        {
            std::fill (l.begin(), l.end(), 0.0f);
            std::fill (r.begin(), r.end(), 0.0f);
            d.process (l.data(), r.data(), block, (juce::int64) b * block, 160.0);
        }));
    }

    std::printf ("\nAliasing: inharmonic energy for a 3.1 kHz sine (lower is cleaner; < -60 dB is inaudible)\n");
    const double f0 = 3100.0;
    const int n = 1 << 16;
    std::vector<float> sine ((size_t) n);
    for (int i = 0; i < n; ++i)
        sine[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * f0 * i / sampleRate);

    for (int t = 0; t < types.size(); ++t)
    {
        poman::Overdrive od;
        od.prepare (sampleRate);
        od.setParams ((poman::Overdrive::Type) t, 0.8f, 1.0f, 0.0f);
        auto y = sine;
        od.process (y.data(), n);
        std::printf ("  drive %-14s %6.1f dB   (base rate)\n", types[t].toRawUTF8(), inharmonicDb (y, f0));
    }
    for (int m : { 0, 2, 9 })
    {
        poman::AmpModel amp;
        amp.prepare (sampleRate, n);
        poman::AmpParams p;
        p.channel = m;
        p.gain = 8.0f;
        amp.setParams (p);
        auto y = sine;
        float* ch[] { y.data() };
        juce::dsp::AudioBlock<float> blk (ch, 1, (size_t) n);
        amp.process (blk);
        std::printf ("  amp %-16s %6.1f dB   (4x oversampled)\n", models[m].toRawUTF8(), inharmonicDb (y, f0));
    }
    return 0;
}
