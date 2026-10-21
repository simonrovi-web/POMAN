// Measures how the amp models respond to playing dynamics and to PRESENCE / DEPTH.
// Prints one line per model; with --check it also enforces the expected behaviour.
//   touch:   THD for a soft (-30 dBFS) and a hard (-8 dBFS) note; a tube amp cleans up when played softly
//   attack:  how much the amp compresses a pick attack relative to the note's tail (dB)
//   presence/depth: change at 5 kHz / 80 Hz between knob 0 and 10, at a low level
#include "dsp/AmpModel.h"
#include <cstdio>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int block = 128;

std::vector<float> run (int model, const std::vector<float>& input, float gain, float presence = 5.0f, float depth = 5.0f)
{
    poman::AmpModel amp;
    amp.prepare (sampleRate, block);
    poman::AmpParams p;
    p.channel = model;
    p.gain = gain;
    p.presence = presence;
    p.depth = depth;
    amp.setParams (p);
    auto y = input;
    for (size_t pos = 0; pos < y.size(); pos += block)
    {
        float* ch[] { y.data() + pos };
        juce::dsp::AudioBlock<float> b (ch, 1, juce::jmin ((size_t) block, y.size() - pos));
        amp.process (b);
    }
    return y;
}

// Total harmonic distortion (%) of the tail of y for a bin-centred fundamental.
double thd (const std::vector<float>& y, double f0)
{
    constexpr int order = 15, size = 1 << order;
    juce::dsp::FFT fft (order);
    std::vector<float> data (size * 2, 0.0f);
    const size_t offset = y.size() - size;
    for (int i = 0; i < size; ++i)
    {
        const double a = juce::MathConstants<double>::twoPi * i / size;
        data[(size_t) i] = (float) (y[offset + (size_t) i] * (0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2 * a) - 0.01168 * std::cos (3 * a)));
    }
    fft.performFrequencyOnlyForwardTransform (data.data());
    const auto bandPower = [&] (double hz)
    {
        const int k = (int) std::round (hz * size / sampleRate);
        double p = 0.0;
        for (int j = k - 3; j <= k + 3; ++j)
            if (j > 0 && j < size / 2)
                p += (double) data[(size_t) j] * data[(size_t) j];
        return p;
    };
    const double fundamental = bandPower (f0);
    double harmonics = 0.0;
    for (int h = 2; h <= 12 && h * f0 < sampleRate * 0.45; ++h)
        harmonics += bandPower (h * f0);
    return 100.0 * std::sqrt (harmonics / fundamental);
}

double rmsDb (const std::vector<float>& y, double from, double to)
{
    double s = 0.0;
    const auto a = (size_t) (from * sampleRate), b = (size_t) (to * sampleRate);
    for (auto i = a; i < b; ++i)
        s += (double) y[i] * y[i];
    return 10.0 * std::log10 (s / (double) (b - a) + 1e-30);
}

// Response of a low-level sine at hz (dB).
double toneDb (int model, double hz, float presence, float depth)
{
    std::vector<float> x ((size_t) sampleRate);
    for (size_t i = 0; i < x.size(); ++i)
        x[i] = 0.003f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
    const auto y = run (model, x, 3.0f, presence, depth);
    return rmsDb (y, 0.5, 1.0) - rmsDb (x, 0.5, 1.0);
}
} // namespace

int main (int argc, char** argv)
{
    const bool check = argc > 1 && juce::String (argv[1]) == "--check";
    const double f0 = 150.0 * sampleRate / 32768.0 * 1.0; // ~220 Hz, bin-centred (150 bins)
    const int n = 1 << 16;

    const auto sine = [&] (float amplitude)
    {
        std::vector<float> x ((size_t) n);
        for (int i = 0; i < n; ++i)
            x[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * f0 * i / sampleRate);
        return x;
    };

    // A picked low-E power chord after 100 ms of silence, decaying like a string.
    std::vector<float> pluck ((size_t) sampleRate);
    for (size_t i = (size_t) (0.1 * sampleRate); i < pluck.size(); ++i)
    {
        const double t = (double) i / sampleRate - 0.1;
        pluck[i] = (float) (0.35 * std::exp (-t / 0.25) * (std::sin (juce::MathConstants<double>::twoPi * 82.41 * t)
                                                         + 0.6 * std::sin (juce::MathConstants<double>::twoPi * 123.47 * t)));
    }
    const double inputDrop = rmsDb (pluck, 0.10, 0.12) - rmsDb (pluck, 0.30, 0.40);

    std::printf ("%-15s %10s %10s %9s %10s %9s\n", "model", "THD soft", "THD hard", "attack", "presence", "depth");
    int failures = 0;
    const auto models = poman::AmpModel::modelNames();
    for (int m = 0; m < models.size(); ++m)
    {
        const double soft = thd (run (m, sine (0.03f), 5.0f), f0);
        const double hard = thd (run (m, sine (0.4f), 5.0f), f0);
        const auto y = run (m, pluck, 5.0f);
        const double attackCompression = inputDrop - (rmsDb (y, 0.10, 0.12) - rmsDb (y, 0.30, 0.40));
        const double presence = toneDb (m, 5000.0, 10.0f, 5.0f) - toneDb (m, 5000.0, 0.0f, 5.0f);
        const double depth = toneDb (m, 80.0, 5.0f, 10.0f) - toneDb (m, 80.0, 5.0f, 0.0f);
        std::printf ("%-15s %9.1f%% %9.1f%% %7.1f dB %7.1f dB %6.1f dB\n", models[m].toRawUTF8(), soft, hard, attackCompression, presence, depth);

        // Worst case for the feedback loop: every knob at 10 on loud white noise must stay finite and bounded.
        double peak = 0.0;
        {
            poman::AmpModel amp;
            amp.prepare (sampleRate, block);
            poman::AmpParams p;
            p.channel = m;
            p.gain = p.presence = p.depth = p.master = p.bass = p.treble = 10.0f;
            amp.setParams (p);
            juce::Random rng (1);
            std::vector<float> x ((size_t) sampleRate);
            for (auto& s : x)
                s = rng.nextFloat() * 2.0f - 1.0f;
            for (size_t pos = 0; pos < x.size(); pos += block)
            {
                float* ch[] { x.data() + pos };
                juce::dsp::AudioBlock<float> b (ch, 1, juce::jmin ((size_t) block, x.size() - pos));
                amp.process (b);
            }
            for (auto s : x)
                peak = std::isfinite (s) ? std::max (peak, (double) std::abs (s)) : 1e9;

            // Then silence: an oscillating loop would keep ringing on its own.
            std::fill (x.begin(), x.end(), 0.0f);
            for (size_t pos = 0; pos < x.size(); pos += block)
            {
                float* ch[] { x.data() + pos };
                juce::dsp::AudioBlock<float> b (ch, 1, juce::jmin ((size_t) block, x.size() - pos));
                amp.process (b);
            }
            for (size_t i = x.size() / 2; i < x.size(); ++i)
                if (! std::isfinite (x[i]) || std::abs (x[i]) > 1e-3f)
                    peak = 1e9;
        }

        if (check)
        {
            // Playing softly must clean up (clearly on the low/mid-gain models; high-gain ones saturate even
            // at -30 dBFS, like the real thing); knobs must do something audible; the loop must be stable.
            const bool ok = soft < hard * (hard > 20.0 || soft < 5.0 ? 0.6 : 0.9) && presence > 3.0 && depth > 3.0 && peak < 4.0;
            failures += ok ? 0 : 1;
            if (! ok)
                std::printf ("FAIL  %s (peak %.2f)\n", models[m].toRawUTF8(), peak);
        }
    }
    return failures;
}
