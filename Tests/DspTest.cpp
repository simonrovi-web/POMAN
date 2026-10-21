// Offline render of every amp model and drive type: checks for NaN/silence/runaway levels
// and prints RMS so the models can be level-matched.
#include "dsp/ToneStack.h"
#include "dsp/Looper.h"
#include "dsp/CabSim.h"
#include "dsp/Resampler.h"
#include "dsp/Tempo.h"
#include "dsp/Compressor.h"
#include "dsp/AmpModel.h"
#include "dsp/Overdrive.h"
#include "dsp/DrumMachine.h"
#include "dsp/GraphicEQ.h"
#include "dsp/Harmonizer.h"
#include "dsp/Modulation.h"
#include <algorithm>
#include <cstdio>
#include <tuple>
#include <functional>

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
    std::setvbuf (stdout, nullptr, _IONBF, 0); // unbuffered: progress survives a crash
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

    std::printf ("-- Graphic EQ --\n");
    {
        const auto measureGainDb = [&] (const poman::GraphicEQ::Settings& s, double hz)
        {
            poman::GraphicEQ eq;
            eq.prepare (sampleRate);
            eq.setSettings (s);
            const int total = (int) sampleRate;
            std::vector<float> x ((size_t) total);
            for (int i = 0; i < total; ++i)
                x[(size_t) i] = 0.25f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);
            std::vector<float> y = x;
            for (int start = 0; start < total; start += blockSize)
                eq.process (y.data() + start, juce::jmin (blockSize, total - start));
            double sx = 0.0, sy = 0.0;
            for (int i = total / 4; i < total; ++i)
                sx += (double) x[(size_t) i] * x[(size_t) i], sy += (double) y[(size_t) i] * y[(size_t) i];
            return (float) (10.0 * std::log10 (sy / sx));
        };

        // Flat: nothing changes.
        {
            poman::GraphicEQ eq;
            eq.prepare (sampleRate);
            eq.setSettings ({});
            juce::Random rng (3);
            std::vector<float> x (48000), y;
            for (auto& v : x)
                v = rng.nextFloat() * 2.0f - 1.0f;
            y = x;
            eq.process (y.data(), (int) y.size());
            float maxDiff = 0.0f;
            for (size_t i = 0; i < x.size(); ++i)
                maxDiff = std::max (maxDiff, std::abs (x[i] - y[i]));
            const bool ok = maxDiff < 1.0e-6f;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s max difference %.2e\n", ok ? "PASS" : "FAIL", "flat EQ is transparent", maxDiff);
        }

        poman::GraphicEQ::Settings boost;
        boost.gainsDb[5] = 12.0f; // 1 kHz
        const float boostDb = measureGainDb (boost, 1000.0);
        const bool boostOk = std::abs (boostDb - 12.0f) < 0.3f;
        failures += boostOk ? 0 : 1;
        std::printf ("%s  %-24s %+5.2f dB at 1 kHz\n", boostOk ? "PASS" : "FAIL", "+12 dB on 1k band", boostDb);

        // Mixed settings: the drawn curve must match the measured response.
        poman::GraphicEQ::Settings mixed;
        mixed.gainsDb = { 3.0f, -4.0f, 6.0f, -2.0f, -6.0f, 4.0f, 2.0f, -3.0f, 5.0f, -8.0f };
        mixed.lowCutHz = 80.0f;
        mixed.highCutHz = 9000.0f;
        mixed.levelDb = -3.0f;
        const auto design = poman::GraphicEQ::designFilters (mixed, sampleRate);
        float worst = 0.0f;
        for (double hz : { 40.0, 100.0, 300.0, 700.0, 1500.0, 3000.0, 6000.0, 12000.0 })
        {
            const float measured = measureGainDb (mixed, hz);
            const float drawn = poman::GraphicEQ::responseDb (design, mixed, sampleRate, hz);
            worst = std::max (worst, std::abs (measured - drawn));
        }
        const bool curveOk = worst < 0.2f;
        failures += curveOk ? 0 : 1;
        std::printf ("%s  %-24s worst mismatch %.3f dB over 8 frequencies\n", curveOk ? "PASS" : "FAIL", "curve == measured", worst);

        poman::GraphicEQ::Settings cuts;
        cuts.lowCutHz = 200.0f;
        cuts.highCutHz = 2000.0f;
        const float low = measureGainDb (cuts, 50.0), high = measureGainDb (cuts, 8000.0), mid = measureGainDb (cuts, 630.0);
        const bool cutsOk = low < -20.0f && high < -20.0f && std::abs (mid) < 1.0f;
        failures += cutsOk ? 0 : 1;
        std::printf ("%s  %-24s 50 Hz %+.1f dB, 630 Hz %+.1f dB, 8 kHz %+.1f dB\n", cutsOk ? "PASS" : "FAIL", "low/high cut", low, mid, high);
    }

    std::printf ("-- Drum machine --\n");
    {
        using poman::DrumMachine;
        constexpr double bpm = 120.0;
        const int stepSamples = (int) (sampleRate * 60.0 / bpm / 4.0); // 6000 at 48 kHz
        const int bar = stepSamples * DrumMachine::numSteps;

        const auto render = [&] (DrumMachine& d, juce::int64 start, int length, int block)
        {
            d.prepare (sampleRate);
            juce::AudioBuffer<float> out (2, length);
            out.clear();
            for (int pos = 0; pos < length; pos += block)
            {
                const int n = juce::jmin (block, length - pos);
                d.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), n, start + pos, bpm);
            }
            return out;
        };
        const auto firstOnset = [] (const juce::AudioBuffer<float>& b)
        {
            for (int i = 0; i < b.getNumSamples(); ++i)
                if (std::abs (b.getSample (0, i)) > 1.0e-6f)
                    return i;
            return -1;
        };

        // 1) Same audio regardless of the device block size (both kits: room and bus are per-sample too).
        for (int kit = 0; kit < (int) DrumMachine::Kit::count; ++kit)
        {
            DrumMachine a, b, c;
            for (auto* d : { &a, &b, &c })
                d->setEnabled (true), d->loadPreset (1), d->setKit (kit); // Metal: busiest pattern
            const auto ra = render (a, 0, bar * 2, 128), rb = render (b, 0, bar * 2, 512), rc = render (c, 0, bar * 2, 37);
            float diff = 0.0f;
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < bar * 2; ++i)
                    diff = std::max ({ diff, std::abs (ra.getSample (ch, i) - rb.getSample (ch, i)), std::abs (ra.getSample (ch, i) - rc.getSample (ch, i)) });
            const float peak = ra.getMagnitude (0, bar * 2);
            bool finite = true;
            for (int i = 0; i < bar * 2; ++i)
                finite = finite && std::isfinite (ra.getSample (0, i));
            const bool ok = diff == 0.0f && finite && peak > 0.2f && peak < 1.0f;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s %s kit: blocks 128/512/37 differ by %.1e, peak %.2f\n", ok ? "PASS" : "FAIL", "block-size independent",
                         DrumMachine::kitNames()[kit].toRawUTF8(), diff, peak);
        }

        // 2) Hits land on the exact sample, also when starting mid-bar, and swing delays off-beats.
        {
            DrumMachine d;
            d.setEnabled (true);
            d.loadPreset (9); // Empty
            d.setStep (DrumMachine::kick, 0, 1);
            const int onBar = firstOnset (render (d, 0, bar, 256));
            const auto fromMid = render (d, 1000, bar, 256);
            const int nextBar = firstOnset (fromMid) + 1000;

            DrumMachine s;
            s.setEnabled (true);
            s.loadPreset (9);
            s.setStep (DrumMachine::kick, 1, 1);
            s.setSwing (0.5f);
            const int swung = firstOnset (render (s, 0, bar, 256));
            const bool ok = onBar == 0 && nextBar == bar && swung == stepSamples + stepSamples / 4;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s step 1 @%d, next bar @%d (want %d), swung 16th @%d (want %d)\n", ok ? "PASS" : "FAIL",
                         "sample-accurate timing", onBar, nextBar, bar, swung, stepSamples + stepSamples / 4);
        }

        // 3) Accents are louder; stepAt follows the playhead.
        {
            DrumMachine n, a;
            for (auto* d : { &n, &a })
                d->setEnabled (true), d->loadPreset (9), d->setKit ((int) DrumMachine::Kit::electronic); // dry kit: exact velocity ratio
            n.setStep (DrumMachine::snare, 0, 1);
            a.setStep (DrumMachine::snare, 0, 2);
            const float ratio = render (a, 0, 4000, 256).getMagnitude (0, 4000) / render (n, 0, 4000, 256).getMagnitude (0, 4000);
            const bool stepsOk = n.stepAt (0, bpm) == 0 && n.stepAt (stepSamples * 5 + 10, bpm) == 5 && n.stepAt (-1, bpm) == 15;
            const bool ok = ratio > 1.3f && ratio < 1.45f && stepsOk;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s accent/normal %.2f, step tracking %s\n", ok ? "PASS" : "FAIL", "accents and stepAt", ratio, stepsOk ? "ok" : "wrong");
        }

        // 3b) Studio kit voices sit in realistic spectral and decay ranges; no factory groove clips.
        {
            struct Range { const char* name; float centroidLo, centroidHi, decayLo, decayHi; };
            const Range ranges[] { { "kick", 0, 300, 0.4f, 1.2f },     { "snare", 2000, 6000, 0.3f, 1.0f },
                                   { "clap", 1200, 4000, 0.4f, 1.5f }, { "closed hat", 7000, 12000, 0.05f, 0.4f },
                                   { "open hat", 7000, 12000, 0.8f, 2.5f }, { "low tom", 0, 300, 0.6f, 1.6f },
                                   { "high tom", 0, 400, 0.5f, 1.3f }, { "crash", 5000, 10000, 2.0f, 4.0f } };
            juce::dsp::FFT fft (13);
            bool allOk = true;
            for (int inst = 0; inst < DrumMachine::numInstruments; ++inst)
            {
                DrumMachine d;
                d.setEnabled (true);
                d.loadPreset (9);
                d.setStep (inst, 0, 1);
                // 30 BPM: one bar lasts 8 s, so the 4 s render holds a single hit and its full decay.
                d.prepare (sampleRate);
                juce::AudioBuffer<float> out (2, (int) (sampleRate * 4.0));
                out.clear();
                for (int pos = 0; pos < out.getNumSamples(); pos += 256)
                    d.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), juce::jmin (256, out.getNumSamples() - pos), pos, 30.0);
                const int n = out.getNumSamples();
                const float peak = out.getMagnitude (0, n);
                float decay = 0.0f;
                for (int i = n - 1; i >= 0; --i)
                    if (std::abs (0.5f * (out.getSample (0, i) + out.getSample (1, i))) > peak * 0.01f)
                    {
                        decay = (float) (i / sampleRate);
                        break;
                    }
                std::vector<float> spec (8192 * 2, 0.0f);
                const int len = (int) (0.1 * sampleRate);
                for (int i = 0; i < len; ++i)
                    spec[(size_t) i] = 0.5f * (out.getSample (0, i) + out.getSample (1, i))
                                     * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (len - 1)));
                fft.performFrequencyOnlyForwardTransform (spec.data());
                double num = 0.0, den = 0.0;
                for (int k = 1; k < 4096; ++k)
                    num += k * sampleRate / 8192.0 * spec[(size_t) k] * spec[(size_t) k], den += spec[(size_t) k] * spec[(size_t) k];
                const float centroid = (float) (num / den);
                const auto& r = ranges[inst];
                const bool ok = centroid >= r.centroidLo && centroid <= r.centroidHi && decay >= r.decayLo && decay <= r.decayHi
                             && peak > 0.05f && peak < 0.9f;
                allOk = allOk && ok;
                if (! ok)
                    std::printf ("      %-10s centroid %.0f Hz decay %.2f s peak %.2f out of range\n", r.name, centroid, decay, peak);
            }
            float worstGroove = 0.0f;
            for (int kit = 0; kit < (int) DrumMachine::Kit::count; ++kit)
                for (int p = 0; p < (int) DrumMachine::patternPresets().size(); ++p)
                {
                    DrumMachine d;
                    d.setEnabled (true);
                    d.setKit (kit);
                    d.loadPreset (p);
                    const float groovePeak = render (d, 0, bar * 2, 256).getMagnitude (0, bar * 2);
                    if (groovePeak > 0.9f)
                        std::printf ("      %s kit, %s groove peaks at %.2f\n", DrumMachine::kitNames()[kit].toRawUTF8(),
                                     DrumMachine::patternPresets()[(size_t) p].name, groovePeak);
                    worstGroove = std::max (worstGroove, groovePeak);
                }
            allOk = allOk && worstGroove < 0.95f;
            failures += allOk ? 0 : 1;
            std::printf ("%s  %-24s 8 voices in range, loudest groove peak %.2f\n", allOk ? "PASS" : "FAIL", "studio kit character", worstGroove);
        }

        // 4) Pattern and mixer survive a save/restore.
        {
            DrumMachine d;
            d.loadPreset (3);
            d.setStep (DrumMachine::crash, 7, 2);
            d.setLevel (DrumMachine::clap, 0.33f);
            d.setSwing (0.4f);
            d.setEnabled (true);
            d.setKit ((int) DrumMachine::Kit::electronic);
            d.setRoom (0.6f);
            DrumMachine r;
            r.fromValueTree (d.toValueTree());
            bool same = r.isEnabled() && r.getKit() == (int) DrumMachine::Kit::electronic && std::abs (r.getRoom() - 0.6f) < 1.0e-4f
                        && std::abs (r.getSwing() - 0.4f) < 1.0e-4f && std::abs (r.getLevel (DrumMachine::clap) - 0.33f) < 1.0e-3f;
            for (int i = 0; i < DrumMachine::numInstruments; ++i)
                for (int s = 0; s < DrumMachine::numSteps; ++s)
                    same = same && r.getStep (i, s) == d.getStep (i, s);
            // A state saved by 0.9.0 (no "kit" property) opens with the Studio kit.
            auto legacy = d.toValueTree();
            legacy.removeProperty ("kit", nullptr);
            DrumMachine fromLegacy;
            fromLegacy.setKit ((int) DrumMachine::Kit::electronic);
            fromLegacy.fromValueTree (legacy);
            same = same && fromLegacy.getKit() == (int) DrumMachine::Kit::studio;
            failures += same ? 0 : 1;
            std::printf ("%s  %-24s\n", same ? "PASS" : "FAIL", "state round trip");
        }
    }

    std::printf ("-- Compressor, tempo sync, tap tempo, calibration --\n");
    {
        const auto sineAt = [&] (float amplitude, int n)
        {
            std::vector<float> x ((size_t) n);
            for (int i = 0; i < n; ++i)
                x[(size_t) i] = amplitude * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sampleRate);
            return x;
        };
        const auto run = [&] (poman::Compressor& c, std::vector<float> x)
        {
            for (int start = 0; start < (int) x.size(); start += blockSize)
                c.process (x.data() + start, juce::jmin (blockSize, (int) x.size() - start));
            return x;
        };
        const auto peakDb = [] (const std::vector<float>& y, size_t from)
        {
            float p = 0.0f;
            for (size_t i = from; i < y.size(); ++i)
                p = std::max (p, std::abs (y[i]));
            return juce::Decibels::gainToDecibels (p);
        };
        const int n = (int) sampleRate * 2;

        // Blend 0 is a true bypass of the compression (only the output trim remains).
        {
            poman::Compressor c;
            c.prepare (sampleRate);
            c.setParams (1.0f, 0.3f, 0.0f, 0.0f);
            const auto x = sineAt (0.5f, n);
            const auto y = run (c, x);
            float diff = 0.0f;
            for (size_t i = 0; i < x.size(); ++i)
                diff = std::max (diff, std::abs (x[i] - y[i]));
            const bool ok = diff < 1.0e-6f;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s max difference %.1e\n", ok ? "PASS" : "FAIL", "comp blend 0 = dry", diff);
        }

        // Max sustain: -40 dB threshold, 10:1, +18 dB make-up. A -6 dBFS sine should come out near
        // -6 - 0.9*34 + 18 = -18.6 dBFS, and +6 dB more input should add only ~0.6 dB.
        {
            poman::Compressor c;
            c.prepare (sampleRate);
            c.setParams (1.0f, 0.3f, 0.0f, 1.0f);
            const float quiet = peakDb (run (c, sineAt (0.5f, n)), (size_t) n / 2);
            const float reduction = c.getGainReductionDb();
            poman::Compressor c2;
            c2.prepare (sampleRate);
            c2.setParams (1.0f, 0.3f, 0.0f, 1.0f);
            const float loud = peakDb (run (c2, sineAt (1.0f, n)), (size_t) n / 2);
            const bool ok = std::abs (quiet - (-18.6f)) < 1.5f && (loud - quiet) < 1.5f && reduction < -28.0f && reduction > -33.0f;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s -6 dBFS in -> %.1f dBFS out, +6 dB in -> +%.2f dB out, GR %.1f dB\n", ok ? "PASS" : "FAIL",
                         "comp curve and ratio", quiet, loud - quiet, reduction);
        }

        {
            const bool ok = std::abs (poman::syncedDelayMs (120.0, 5, 999.0f) - 250.0f) < 0.01f      // 1/8 at 120
                         && std::abs (poman::syncedDelayMs (100.0, 4, 999.0f) - 450.0f) < 0.01f      // dotted 1/8 at 100
                         && std::abs (poman::syncedDelayMs (120.0, 6, 999.0f) - 166.667f) < 0.01f    // 1/8 triplet
                         && poman::syncedDelayMs (40.0, 1, 999.0f) == poman::maxDelayMs              // 3 s clamps to 2 s
                         && poman::syncedDelayMs (120.0, 0, 380.0f) == 380.0f;                        // free
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s\n", ok ? "PASS" : "FAIL", "delay note values");
        }

        {
            poman::TapTempo tap;
            double bpm = 0.0;
            for (double t : { 0.0, 0.5, 1.0, 1.5 })
                bpm = tap.tap (t);
            const double afterPause = tap.tap (4.0); // > 2 s gap starts over
            const double next = tap.tap (4.6);
            const bool ok = std::abs (bpm - 120.0) < 0.01 && afterPause == 0.0 && std::abs (next - 100.0) < 0.01;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s 4 taps -> %.1f BPM, restart -> %.1f BPM\n", ok ? "PASS" : "FAIL", "tap tempo", bpm, next);
        }

        {
            const bool ok = poman::calibrationGainDb (-21.0f) == 12.0f && poman::calibrationGainDb (-3.0f) == -6.0f
                         && poman::calibrationGainDb (-60.0f) == 24.0f;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s -21 dB peak -> +12 dB, -3 dB -> -6 dB, clamps at +24\n", ok ? "PASS" : "FAIL", "input calibration");
        }
    }

    std::printf ("-- Resampler (44.1 kHz host, 48 kHz inner, identity) --\n");
    {
        // Level error and residual (everything that is not the delayed sine) per frequency.
        const auto measure = [&] (auto& rs, double hz, float& gainDb, float& residualDb)
        {
            constexpr double host = 44100.0;
            rs.prepare (host, 48000.0, 256);
            const int n = 65536, latency = rs.getLatencySamples();
            std::vector<float> x ((size_t) n);
            for (int i = 0; i < n; ++i)
                x[(size_t) i] = 0.5f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / host);
            auto y = x;
            for (int pos = 0; pos < n; pos += 256)
                rs.process (y.data() + pos, juce::jmin (256, n - pos), [] (float*, int) {});
            // Least-squares fit of y against sin/cos at hz (absorbs any sub-sample delay), over the tail.
            double ss = 0, cc = 0, sc = 0, ys = 0, yc = 0, yy = 0;
            for (int i = n / 2; i < n; ++i)
            {
                const double s = std::sin (juce::MathConstants<double>::twoPi * hz * i / host), c = std::cos (juce::MathConstants<double>::twoPi * hz * i / host);
                ss += s * s, cc += c * c, sc += s * c, ys += y[(size_t) i] * s, yc += y[(size_t) i] * c, yy += (double) y[(size_t) i] * y[(size_t) i];
            }
            const double det = ss * cc - sc * sc, a = (ys * cc - yc * sc) / det, b = (yc * ss - ys * sc) / det;
            const double fitted = a * a * ss + b * b * cc + 2 * a * b * sc;
            gainDb = (float) (20.0 * std::log10 (std::sqrt (a * a + b * b) / 0.5));
            residualDb = (float) (10.0 * std::log10 (juce::jmax (1.0e-20, yy - fitted) / fitted));
            return latency;
        };

        for (int kind = 0; kind < 2; ++kind)
        {
            float worstGain = 0.0f, worstResidual = -200.0f;
            int latency = 0;
            juce::String detail;
            for (double hz : { 1000.0, 5000.0, 10000.0, 15000.0 })
            {
                float g = 0.0f, r = 0.0f;
                if (kind == 0)
                {
                    poman::ResampledProcess<juce::LagrangeInterpolator> rs;
                    latency = measure (rs, hz, g, r);
                }
                else
                {
                    poman::ResampledProcess<juce::WindowedSincInterpolator> rs;
                    latency = measure (rs, hz, g, r);
                }
                worstGain = std::max (worstGain, std::abs (g));
                worstResidual = std::max (worstResidual, r);
                detail << juce::String ((int) (hz / 1000)) << "k: " << juce::String (g, 2) << " dB/" << juce::String (r, 0) << " dB  ";
            }
            std::printf ("      %-14s latency %3d samples | %s\n", kind == 0 ? "Lagrange" : "WindowedSinc", latency, detail.toRawUTF8());
            if (kind == 1)
            {
                // The NAM path uses the windowed sinc: Lagrange is only clean to ~5 kHz (see line above).
                const bool ok = worstGain < 0.3f && worstResidual < -80.0f && latency < 260;
                failures += ok ? 0 : 1;
                std::printf ("%s  %-24s 1-15 kHz: level within %.2f dB, residual %.0f dB, latency %d samples\n",
                             ok ? "PASS" : "FAIL", "resampler (sinc)", worstGain, worstResidual, latency);
            }
        }
    }

    std::printf ("-- Dual-IR cabinet --\n");
    {
        // Uses the bundled 4x12 IR as slot A and a copy delayed by 23 samples (a mic ~16 cm further) as B.
        const auto irA = juce::File (POMAN_SOURCE_DIR).getChildFile ("portable/POMAN-Portable/IRs/POMAN 4x12 Modern.wav");
        const auto irB = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("poman_ir_b.wav");
        constexpr int lag = 23;
        {
            juce::AudioFormatManager fm;
            fm.registerBasicFormats();
            std::unique_ptr<juce::AudioFormatReader> reader (fm.createReaderFor (irA));
            juce::AudioBuffer<float> a (1, (int) reader->lengthInSamples), b (1, (int) reader->lengthInSamples + lag);
            reader->read (&a, 0, a.getNumSamples(), 0, true, false);
            b.clear();
            b.copyFrom (0, lag, a, 0, 0, a.getNumSamples());
            irB.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (irB);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (reader->sampleRate).withNumChannels (1).withBitsPerSample (24));
            writer->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }

        // The convolver swaps a new IR in on its own thread, then crossfades: feed silence until settled.
        const auto settle = [&] (poman::CabSim& cab)
        {
            std::vector<float> silence (256, 0.0f);
            for (int i = 0; i < 40; ++i)
            {
                juce::Thread::sleep (10);
                std::fill (silence.begin(), silence.end(), 0.0f);
                float* ch[] { silence.data() };
                juce::dsp::AudioBlock<float> blk (ch, 1, 256);
                cab.process (blk);
            }
        };
        const auto impulseResponse = [&] (poman::CabSim& cab)
        {
            std::vector<float> y (4096, 0.0f);
            y[0] = 1.0f;
            for (int pos = 0; pos < 4096; pos += 256)
            {
                float* ch[] { y.data() + pos };
                juce::dsp::AudioBlock<float> blk (ch, 1, 256);
                cab.process (blk);
            }
            return y;
        };
        const auto peakOf = [] (const std::vector<float>& y)
        {
            float p = 0.0f;
            for (auto v : y)
                p = std::max (p, std::abs (v));
            return p;
        };

        poman::CabSim single;
        single.prepare (sampleRate, 256);
        const bool loadA = single.loadImpulseResponse (0, irA).wasOk();
        settle (single);
        const float singlePeak = peakOf (impulseResponse (single));

        poman::CabSim dual;
        dual.prepare (sampleRate, 256);
        dual.setMix (0.5f);
        const bool loadBoth = dual.loadImpulseResponse (0, irA).wasOk() && dual.loadImpulseResponse (1, irB).wasOk();
        settle (dual);
        const int alignment = dual.getAlignmentSamples();
        const float dualPeak = peakOf (impulseResponse (dual));

        // Equal-power mix of two aligned, identical IRs sums coherently: cos45 + sin45 = 1.414x.
        const float ratio = dualPeak / singlePeak;
        const bool ok = loadA && loadBoth && alignment == lag && singlePeak > 0.05f && ratio > 1.35f && ratio < 1.45f;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s detected lag %d (want %d), A+B peak / A peak = %.3f (want 1.414)\n", ok ? "PASS" : "FAIL",
                     "IR alignment and blend", alignment, lag, ratio);

        // Low cut on the cab section.
        poman::CabSim cut;
        cut.prepare (sampleRate, 256);
        cut.setCuts (300.0f, 20000.0f);
        const auto toneThrough = [&] (poman::CabSim& cab, double hz)
        {
            std::vector<float> x (48000);
            for (int i = 0; i < 48000; ++i)
                x[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * i / sampleRate);
            for (int pos = 0; pos < 48000; pos += 256)
            {
                float* ch[] { x.data() + pos };
                juce::dsp::AudioBlock<float> blk (ch, 1, (size_t) juce::jmin (256, 48000 - pos)); // last block is partial
                cab.process (blk);
            }
            float p = 0.0f;
            for (int i = 24000; i < 48000; ++i)
                p = std::max (p, std::abs (x[(size_t) i]));
            return juce::Decibels::gainToDecibels (p);
        };
        poman::CabSim plain;
        plain.prepare (sampleRate, 256);
        const float drop = toneThrough (plain, 60.0) - toneThrough (cut, 60.0);
        const bool cutOk = drop > 20.0f;
        failures += cutOk ? 0 : 1;
        std::printf ("%s  %-24s 60 Hz down %.1f dB with LOW CUT at 300 Hz\n", cutOk ? "PASS" : "FAIL", "cab low cut", drop);
        irB.deleteFile();
    }

    std::printf ("-- Looper --\n");
    {
        using LS = poman::Looper::State;
        using Cmd = poman::Looper::Command;
        constexpr int loopLen = 48000; // 1 s
        const auto tone = [] (double hz, float amp)
        {
            return [hz, amp] (juce::int64 i) { return amp * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / 48000.0); };
        };
        const auto silence = [] (juce::int64) { return 0.0f; };

        // Feeds `n` samples (generator by loop-relative index) and returns what the looper outputs.
        const auto feed = [] (poman::Looper& l, int n, auto&& gen)
        {
            std::vector<float> outL ((size_t) n), outR ((size_t) n);
            for (int pos = 0; pos < n; pos += 256)
            {
                const int count = juce::jmin (256, n - pos); // the last block may be partial
                for (int i = 0; i < count; ++i)
                    outL[(size_t) (pos + i)] = outR[(size_t) (pos + i)] = gen (pos + i);
                l.process (outL.data() + pos, outR.data() + pos, count);
            }
            return outL;
        };
        // Largest difference between a pass of loop playback and the expected sum of generators.
        const auto playbackError = [&] (poman::Looper& l, std::initializer_list<std::function<float (juce::int64)>> layers)
        {
            const auto out = feed (l, loopLen, silence);
            float worst = 0.0f;
            for (int i = 0; i < loopLen; ++i)
            {
                float expected = 0.0f;
                for (const auto& f : layers)
                    expected += f (i);
                worst = std::max (worst, std::abs (out[(size_t) i] - expected));
            }
            return worst;
        };

        poman::Looper l;
        l.prepare (48000.0);
        const auto a = tone (200.0, 0.3f), b = tone (330.0, 0.2f), c = tone (500.0, 0.1f);

        l.post (Cmd::main);
        feed (l, loopLen, a);
        l.post (Cmd::main); // close: plays from the start on the next block
        const float errA = playbackError (l, { a });
        const bool recordOk = l.getState() == LS::playing && std::abs (l.getLengthSeconds() - 1.0) < 1.0e-6 && errA < 1.0e-6f;

        l.post (Cmd::main); // overdub one pass of b
        feed (l, loopLen, b);
        l.post (Cmd::main);
        const float errAB = playbackError (l, { a, b });

        l.post (Cmd::undo);
        const float errUndo = playbackError (l, { a });
        const bool redoAvailable = l.canRedo();
        l.post (Cmd::undo); // redo
        const float errRedo = playbackError (l, { a, b });
        l.post (Cmd::undo);  // undo again, then a new overdub replaces b for good
        l.post (Cmd::main);  // both apply at the next block, in order
        feed (l, loopLen, c);
        l.post (Cmd::main);
        const float errAC = playbackError (l, { a, c });

        const bool ok = recordOk && errAB < 1.0e-6f && errUndo < 1.0e-6f && redoAvailable && errRedo < 1.0e-6f && errAC < 1.0e-6f;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s play %.1e, +dub %.1e, undo %.1e, redo %.1e, new dub after undo %.1e\n", ok ? "PASS" : "FAIL",
                     "record/overdub/undo/redo", errA, errAB, errUndo, errRedo, errAC);

        l.post (Cmd::stop);
        const auto stopped = feed (l, 4096, silence);
        l.post (Cmd::main);
        const float errRestart = playbackError (l, { a, c });
        l.post (Cmd::clear);
        const auto cleared = feed (l, 4096, a); // empty looper passes the input through untouched
        float stoppedPeak = 0.0f, passDiff = 0.0f;
        for (int i = 0; i < 4096; ++i)
            stoppedPeak = std::max (stoppedPeak, std::abs (stopped[(size_t) i])), passDiff = std::max (passDiff, std::abs (cleared[(size_t) i] - a (i)));
        const bool ctlOk = stoppedPeak == 0.0f && errRestart < 1.0e-6f && l.getState() == LS::empty && passDiff == 0.0f;
        failures += ctlOk ? 0 : 1;
        std::printf ("%s  %-24s stop silent, restart from top, clear passes input\n", ctlOk ? "PASS" : "FAIL", "stop/restart/clear");

        poman::Looper shortLoop;
        shortLoop.prepare (48000.0);
        shortLoop.post (Cmd::main);
        feed (shortLoop, 2048, a); // 43 ms: an accidental double tap
        shortLoop.post (Cmd::main);
        feed (shortLoop, 256, silence);
        poman::Looper full;
        full.prepare (48000.0);
        full.post (Cmd::main);
        feed (full, (int) (48000 * poman::Looper::maxSeconds) + 48000, a); // a second past the limit
        const bool limitOk = shortLoop.getState() == LS::empty && full.getState() == LS::playing
                          && std::abs (full.getLengthSeconds() - poman::Looper::maxSeconds) < 1.0e-6;
        failures += limitOk ? 0 : 1;
        std::printf ("%s  %-24s accidental tap discarded (state %d), closes itself at %.0f s (state %d)\n", limitOk ? "PASS" : "FAIL",
                     "loop length limits", (int) shortLoop.getState(), full.getLengthSeconds(), (int) full.getState());
    }

    std::printf ("-- Passive tone stack (Yeh/Smith model) --\n");
    {
        using TS = poman::ToneStack;
        const auto db = [] (double g) { return 20.0 * std::log10 (g); };
        const auto mag = [] (const TS::Components& c, float t, float m, float b, double hz)
        {
            return TS::analogMagnitude (TS::analog (c, TS::treblePosition (t), TS::midPosition (m), TS::bassPosition (b)), hz);
        };

        // 1) The discretised filter matches the circuit (at the amp's 4x oversampled rate).
        double worst = 0.0;
        const double osRate = 192000.0;
        for (const auto* c : { &TS::fender, &TS::marshall })
            for (auto [t, m, b] : { std::tuple { 5.0f, 5.0f, 5.0f }, std::tuple { 10.0f, 0.0f, 10.0f }, std::tuple { 0.0f, 10.0f, 2.0f } })
            {
                const auto analog = TS::analog (*c, TS::treblePosition (t), TS::midPosition (m), TS::bassPosition (b));
                const auto digital = TS::discretise (analog, osRate);
                for (double hz : { 60.0, 250.0, 1000.0, 3000.0, 8000.0 })
                    worst = std::max (worst, std::abs (db (TS::digitalMagnitude (digital, hz, osRate)) - db (TS::analogMagnitude (analog, hz))));
            }
        const bool matchOk = worst < 0.2;
        failures += matchOk ? 0 : 1;
        std::printf ("%s  %-24s digital vs circuit: worst %.3f dB\n", matchOk ? "PASS" : "FAIL", "tone stack accuracy", worst);

        // 2) Classic behaviour.
        const auto scoopDepth = [&] (const TS::Components& c)
        {
            double lowestMid = 1.0e9;
            for (double hz = 200.0; hz <= 2000.0; hz *= 1.05)
                lowestMid = std::min (lowestMid, mag (c, 5, 5, 5, hz));
            return db (std::max (mag (c, 5, 5, 5, 80.0), mag (c, 5, 5, 5, 5000.0))) - db (lowestMid);
        };
        const double fenderScoop = scoopDepth (TS::fender), marshallScoop = scoopDepth (TS::marshall);
        const double midRange = db (mag (TS::fender, 5, 10, 5, 500.0)) - db (mag (TS::fender, 5, 0, 5, 500.0));
        const double trebleRange = db (mag (TS::fender, 10, 5, 5, 5000.0)) - db (mag (TS::fender, 0, 5, 5, 5000.0));
        const double bassRange = db (mag (TS::fender, 5, 5, 10, 80.0)) - db (mag (TS::fender, 5, 5, 0, 80.0));
        const double interaction = std::abs (db (mag (TS::fender, 10, 5, 5, 500.0)) - db (mag (TS::fender, 0, 5, 5, 500.0)));
        // The Bassman's 25k mid pot is famously subtle (~6 dB at 500 Hz); treble and bass have more range.
        const bool behaviourOk = fenderScoop > 4.0 && marshallScoop < fenderScoop && midRange > 4.0 && trebleRange > 6.0
                              && bassRange > 6.0 && interaction > 0.5;
        failures += behaviourOk ? 0 : 1;
        std::printf ("%s  %-24s scoop Fender %.1f dB > Marshall %.1f dB; mid %.1f, treble %.1f, bass %.1f dB ranges; treble moves 500 Hz by %.1f dB\n",
                     behaviourOk ? "PASS" : "FAIL", "tone stack behaviour", fenderScoop, marshallScoop, midRange, trebleRange, bassRange, interaction);
    }

    std::printf ("-- Modulation --\n");
    {
        const int total = (int) sampleRate * 3;
        const auto sine = [&] (int i) { return 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / sampleRate); };
        const auto render = [&] (poman::Modulation::Type type, float rate, float depth, float mix, float fb, auto&& source)
        {
            poman::Modulation mod;
            mod.prepare (sampleRate, blockSize);
            mod.setParams (type, rate, depth, mix, fb);
            juce::AudioBuffer<float> buf (2, total);
            for (int i = 0; i < total; ++i)
                buf.setSample (0, i, source (i)), buf.setSample (1, i, source (i));
            for (int start = 0; start < total; start += blockSize)
                mod.process (buf.getWritePointer (0, start), buf.getWritePointer (1, start), juce::jmin (blockSize, total - start));
            return buf;
        };
        const auto finite = [] (const juce::AudioBuffer<float>& b)
        {
            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < b.getNumSamples(); ++i)
                    if (! std::isfinite (b.getSample (ch, i)))
                        return false;
            return true;
        };
        const float inRms = 0.3f / std::sqrt (2.0f);

        for (auto [type, name] : { std::pair { poman::Modulation::Type::chorus, "chorus" }, std::pair { poman::Modulation::Type::flanger, "flanger" } })
        {
            auto out = render (type, 0.8f, 0.6f, 0.5f, 0.3f, sine);
            const float rmsDb = juce::Decibels::gainToDecibels (out.getRMSLevel (0, 4800, total - 4800) / inRms);
            float maxDiff = 0.0f;
            for (int i = 4800; i < total; ++i)
                maxDiff = std::max (maxDiff, std::abs (out.getSample (0, i) - out.getSample (1, i)));
            const bool ok = finite (out) && std::abs (rmsDb) < 4.0f && maxDiff > 0.01f;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s level %+5.1f dB vs dry, L/R differ by up to %.2f\n", ok ? "PASS" : "FAIL", name, rmsDb, maxDiff);
        }

        juce::Random rng (7);
        auto worst = render (poman::Modulation::Type::flanger, 0.1f, 1.0f, 1.0f, 1.0f, [&] (int) { return 0.5f * (rng.nextFloat() * 2.0f - 1.0f); });
        const float worstPeak = worst.getMagnitude (0, 0, total);
        const bool stable = finite (worst) && worstPeak < 3.0f;
        failures += stable ? 0 : 1;
        std::printf ("%s  %-24s peak %.2f with max feedback on noise\n", stable ? "PASS" : "FAIL", "flanger stability", worstPeak);

        auto trem = render (poman::Modulation::Type::tremolo, 5.0f, 1.0f, 0.0f, 1.0f, [] (int) { return 0.5f; });
        const auto range = juce::FloatVectorOperations::findMinAndMax (trem.getReadPointer (0, 9600), total - 9600);
        float sumSpread = 0.0f;
        for (int i = 9600; i < total; ++i)
            sumSpread = std::max (sumSpread, std::abs (trem.getSample (0, i) + trem.getSample (1, i) - 0.5f));
        const bool tremOk = range.getStart() < 0.01f && range.getEnd() > 0.49f && sumSpread < 0.02f;
        failures += tremOk ? 0 : 1;
        std::printf ("%s  %-24s gain %.3f..%.3f, auto-pan L+R constant within %.3f\n", tremOk ? "PASS" : "FAIL", "tremolo depth/stereo",
                     range.getStart() * 2.0f, range.getEnd() * 2.0f, sumSpread);
    }

    return failures;
}
