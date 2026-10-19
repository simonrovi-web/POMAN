// Offline render of every amp model and drive type: checks for NaN/silence/runaway levels
// and prints RMS so the models can be level-matched.
#include "dsp/AmpModel.h"
#include "dsp/Overdrive.h"
#include "dsp/Harmonizer.h"
#include <algorithm>
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

    std::printf ("-- Diatonic intervals --\n");
    const auto& scales = poman::harmonizerScales();
    struct IntervalCase { const char* label; int note, key, scale, steps, expected; };
    const IntervalCase intervals[] {
        { "E minor: E +3rd = G",     52, 4, 1, 2, 3 },  // minor third
        { "E minor: G +3rd = B",     55, 4, 1, 2, 4 },  // major third
        { "E minor: B +5th = F#",    59, 4, 1, 4, 7 },
        { "E minor: F# +5th = C",    54, 4, 1, 4, 6 },  // diminished fifth stays in key
        { "C major: B +3rd = D",     59, 0, 0, 2, 3 },
        { "C major: C -3rd = A",     60, 0, 0, -2, -3 },
        { "C major: E +oct",         64, 0, 0, 7, 12 },
        { "C major: C# (out) +3rd",  61, 0, 0, 2, 4 },  // measured from C below
        { "A min pent: A +1 = C",    57, 9, 6, 1, 3 },
    };
    for (const auto& c : intervals)
    {
        const int got = poman::diatonicShift (c.note, c.key, scales[(size_t) c.scale], c.steps);
        const bool ok = got == c.expected;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s got %+d expected %+d\n", ok ? "PASS" : "FAIL", c.label, got, c.expected);
    }

    std::printf ("-- Harmonizer pitch (wet only, measured with YIN) --\n");
    struct ShiftCase { const char* label; float inHz; bool diatonic; int steps; float expectedHz; };
    const ShiftCase shifts[] {
        { "chromatic +7 st @220",  220.0f,  false, 7,   329.63f },
        { "chromatic +12 st @110", 110.0f,  false, 12,  220.0f },
        { "chromatic -12 st @330", 329.63f, false, -12, 164.81f },
        { "E min +3rd @E3",        164.81f, true,  2,   196.0f },  // E -> G
        { "E min +3rd @G3",        196.0f,  true,  2,   246.94f }, // G -> B
    };
    for (const auto& c : shifts)
    {
        poman::Harmonizer h;
        h.prepare (sampleRate);
        poman::Harmonizer::Params hp;
        hp.mode = c.diatonic ? poman::Harmonizer::Mode::diatonic : poman::Harmonizer::Mode::chromatic;
        hp.key = 4;
        hp.scale = 1;
        hp.steps = { c.steps, 0 };
        hp.level = { 1.0f, 0.0f };
        hp.dry = 0.0f;
        h.setParams (hp);

        const int total = (int) sampleRate;
        std::vector<float> in ((size_t) total), left ((size_t) total), right ((size_t) total);
        for (int i = 0; i < total; ++i)
            in[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * c.inHz * i / sampleRate);

        for (int start = 0; start < total; start += blockSize)
        {
            const int n = juce::jmin (blockSize, total - start);
            h.track (in.data() + start, n);
            h.process (in.data() + start, left.data() + start, right.data() + start, n);
        }

        // Short windows (about one grain) so splice phase jumps do not bias the estimate; take the median.
        std::vector<float> mono ((size_t) total);
        for (int i = 0; i < total; ++i)
            mono[(size_t) i] = left[(size_t) i] + right[(size_t) i];
        poman::YinDetector yin;
        std::vector<float> estimates;
        for (int start = total / 2; start + 1024 <= total; start += 480)
            if (const float hz = yin.detect (mono.data() + start, 1024, sampleRate, 100.0f, 2000.0f); hz > 0.0f)
                estimates.push_back (hz);
        std::sort (estimates.begin(), estimates.end());
        const float outHz = estimates.empty() ? 0.0f : estimates[estimates.size() / 2];
        const float cents = outHz > 0.0f ? 1200.0f * std::log2 (outHz / c.expectedHz) : 9999.0f;
        const bool ok = std::abs (cents) < 5.0f;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s out %7.2f Hz  expected %7.2f Hz  (%+.1f cents)\n", ok ? "PASS" : "FAIL", c.label, outHz, c.expectedHz, cents);
    }

    return failures;
}
