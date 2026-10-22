// Offline render of every amp model and drive type: checks for NaN/silence/runaway levels
// and prints RMS so the models can be level-matched.
#include "InputSelect.h"
#include "dsp/BackingTrack.h"
#include "dsp/PitchShift.h"
#include "dsp/Tubes.h"
#include "dsp/Wah.h"
#include "dsp/NoiseGate.h"
#include "dsp/Ambience.h"
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
        { "E lydian: E +4th = A#",   52, 4, 8, 3, 6 },  // the raised fourth that defines the mode
        { "E lydian: B +3rd = D#",   59, 4, 8, 2, 4 },
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


    std::printf ("-- Studio reverb (FDN) --\n");
    {
        constexpr int seconds = 8;
        const int total = (int) sampleRate * seconds;
        // Wet-only impulse response (dry removed) of one type/size/tone, stereo.
        const auto impulse = [&] (poman::Ambience::Type type, float size, float tone)
        {
            poman::Ambience rev;
            rev.prepare (sampleRate, blockSize);
            rev.setParams (size, 1.0f, type, tone);
            juce::AudioBuffer<float> buf (2, total);
            buf.clear();
            buf.setSample (0, 0, 1.0f), buf.setSample (1, 0, 1.0f);
            for (int start = 0; start < total; start += blockSize)
            {
                auto b = juce::dsp::AudioBlock<float> (buf).getSubBlock ((size_t) start, (size_t) juce::jmin (blockSize, total - start));
                rev.process (b);
            }
            buf.setSample (0, 0, buf.getSample (0, 0) - 0.65f), buf.setSample (1, 0, buf.getSample (1, 0) - 0.65f); // dry 1 - 0.35 mix
            return buf;
        };
        // RT60 from the Schroeder backward integral (-5 to -25 dB, extrapolated), optionally band-limited.
        const auto rt60 = [&] (const juce::AudioBuffer<float>& ir, double bandHz)
        {
            std::vector<float> x (ir.getReadPointer (0), ir.getReadPointer (0) + total);
            if (bandHz > 0.0)
            {
                poman::Biquad bp1, bp2;
                bp1.set (poman::ArrayCoeffs::makeBandPass (sampleRate, bandHz, 1.4));
                bp2.set (poman::ArrayCoeffs::makeBandPass (sampleRate, bandHz, 1.4));
                for (auto& v : x)
                    v = bp2 (bp1 (v));
            }
            std::vector<double> energy ((size_t) total);
            double acc = 0.0;
            for (int i = total - 1; i >= 0; --i)
                energy[(size_t) i] = (acc += (double) x[(size_t) i] * x[(size_t) i]);
            const auto timeAt = [&] (double db)
            {
                for (int i = 0; i < total; ++i)
                    if (10.0 * std::log10 (energy[(size_t) i] / energy[0]) < db)
                        return i / sampleRate;
                return (double) seconds;
            };
            return 3.0 * (timeAt (-25.0) - timeAt (-5.0));
        };

        for (int t = 0; t < (int) poman::Ambience::Type::count; ++t)
        {
            const auto type = (poman::Ambience::Type) t;
            const auto name = poman::Ambience::typeNames()[t];
            const auto small = impulse (type, 0.2f, 0.5f), large = impulse (type, 0.9f, 0.5f);
            const double rtSmall = rt60 (small, 0.0), rtLarge = rt60 (large, 0.0);
            const double rtLow = rt60 (large, 500.0), rtHigh = rt60 (large, 6000.0);

            // Stereo: correlation of the two tails after the first 50 ms.
            double sll = 0.0, srr = 0.0, slr = 0.0;
            for (int i = (int) (0.05 * sampleRate); i < total; ++i)
            {
                const double l = large.getSample (0, i), r = large.getSample (1, i);
                sll += l * l, srr += r * r, slr += l * r;
            }
            const double correlation = slr / std::sqrt (sll * srr + 1e-30);
            // Silence at the end (no ringing loop), and finite everywhere.
            const float tailPeak = large.getMagnitude (0, total - (int) sampleRate, (int) sampleRate);
            bool finiteAll = true;
            for (int i = 0; i < total; ++i)
                finiteAll = finiteAll && std::isfinite (large.getSample (0, i)) && std::isfinite (large.getSample (1, i));

            const bool ok = finiteAll && rtLarge > rtSmall * 1.5 && rtHigh < rtLow && std::abs (correlation) < 0.5
                         && tailPeak < 1.0e-3f;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s RT60 %.2f s (size 0.2) .. %.2f s (0.9); 500 Hz %.2f s > 6 kHz %.2f s; L/R corr %+.2f\n",
                         ok ? "PASS" : "FAIL", ("reverb " + name).toRawUTF8(), rtSmall, rtLarge, rtLow, rtHigh, correlation);
        }

        // Wet/dry balance against the previous (JUCE Freeverb) reverb with the old settings, on a guitar-like
        // pluck: the wet energy over 2 s relative to the dry path. (The old one also doubled the dry signal,
        // so switching it on raised the level by ~5 dB; the balance is what presets rely on.)
        {
            const auto pluck = makePluck (sampleRate, (int) sampleRate);
            const int span = 2 * (int) sampleRate;
            const auto wetToDryDb = [&] (auto&& processBlock, float dryGain)
            {
                juce::AudioBuffer<float> buf (2, span);
                buf.clear();
                for (int i = 0; i < (int) pluck.size(); ++i)
                    buf.setSample (0, i, pluck[(size_t) i]), buf.setSample (1, i, pluck[(size_t) i]);
                for (int start = 0; start < span; start += blockSize)
                {
                    auto b = juce::dsp::AudioBlock<float> (buf).getSubBlock ((size_t) start, (size_t) juce::jmin (blockSize, span - start));
                    processBlock (b);
                }
                double wet = 0.0, dry = 0.0;
                for (int i = 0; i < span; ++i)
                {
                    const double in = i < (int) pluck.size() ? pluck[(size_t) i] : 0.0;
                    const double w = buf.getSample (0, i) - dryGain * in;
                    wet += w * w, dry += dryGain * dryGain * in * in;
                }
                return (float) (10.0 * std::log10 (wet / dry));
            };
            juce::dsp::Reverb old;
            old.prepare ({ sampleRate, (juce::uint32) blockSize, 2 });
            juce::dsp::Reverb::Parameters p;
            p.roomSize = 0.35f + 0.6f * 0.5f, p.damping = 0.55f, p.width = 1.0f, p.wetLevel = 0.2f * 0.6f, p.dryLevel = 1.0f - 0.35f * 0.2f;
            old.setParameters (p);
            old.reset();
            const float oldDb = wetToDryDb ([&] (juce::dsp::AudioBlock<float>& b) { old.process (juce::dsp::ProcessContextReplacing<float> (b)); }, 2.0f * p.dryLevel);
            juce::String line;
            bool ok = true;
            for (int t = 0; t < (int) poman::Ambience::Type::count; ++t)
            {
                poman::Ambience rev;
                rev.prepare (sampleRate, blockSize);
                rev.setParams (0.5f, 0.2f, (poman::Ambience::Type) t, 0.5f);
                const float db = wetToDryDb ([&] (juce::dsp::AudioBlock<float>& b) { rev.process (b); }, 1.0f - 0.35f * 0.2f);
                ok = ok && std::abs (db - oldDb) < 2.0f;
                line << poman::Ambience::typeNames()[t] << " " << juce::String (db - oldDb, 1) << " dB  ";
            }
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s wet/dry vs previous reverb: %s\n", ok ? "PASS" : "FAIL", "reverb balance", line.toRawUTF8());
        }
    }

    std::printf ("-- Noise gate (detect on DI, cut after the amp) --\n");
    {
        // DI: -70 dB pickup noise, a muted chug at 0.5 s stopped dead at 0.7 s, a ringing note at 1.5 s
        // fading out. "Amp": 24 samples of latency, +30 dB, and its own -40 dB hiss (not in the DI).
        const int total = (int) sampleRate * 4, latency = 24;
        juce::Random rng (3);
        std::vector<float> di ((size_t) total), amp ((size_t) total);
        for (int i = 0; i < total; ++i)
        {
            const double t = i / sampleRate;
            float s = 0.0003f * (rng.nextFloat() * 2.0f - 1.0f);
            const double tone = std::sin (juce::MathConstants<double>::twoPi * 110.0 * t);
            if (t >= 0.5 && t < 0.7) // low B of a 7-string: the slowest ripple the stop detector must ride
                s += (float) (0.2 * std::sin (juce::MathConstants<double>::twoPi * 61.74 * t));
            if (t >= 1.5)
                s += (float) (0.2 * std::exp (-(t - 1.5) / 0.35) * tone);
            di[(size_t) i] = s;
        }
        for (int i = 0; i < total; ++i)
            amp[(size_t) i] = 31.6f * (i >= latency ? di[(size_t) (i - latency)] : 0.0f) + 0.01f * (rng.nextFloat() * 2.0f - 1.0f);
        const auto ungated = amp;

        poman::NoiseGate gate;
        gate.prepare (sampleRate);
        gate.setThreshold (-50.0f);
        gate.setPathLatency (latency);
        for (int start = 0; start < total; start += blockSize)
        {
            const int n = juce::jmin (blockSize, total - start);
            gate.detect (di.data() + start, n);
            gate.apply (amp.data() + start, n);
        }

        const auto rms = [&] (const std::vector<float>& x, double from, double to)
        {
            double s = 0.0;
            const int a = (int) (from * sampleRate), b = (int) (to * sampleRate);
            for (int i = a; i < b; ++i)
                s += (double) x[(size_t) i] * x[(size_t) i];
            return 10.0 * std::log10 (s / (b - a) + 1e-30);
        };
        const double hissCut = rms (ungated, 0.1, 0.45) - rms (amp, 0.1, 0.45);
        const double onset = 0.5 + latency / sampleRate;
        const double attackLoss = rms (ungated, onset, onset + 0.005) - rms (amp, onset, onset + 0.005);
        // Time until the gate's own gain (gated vs ungated output) is down 20 dB, after a stop / a fade.
        const auto closeTime = [&] (double from)
        {
            for (double t = from; t < from + 1.5; t += 0.001)
                if (rms (amp, t, t + 0.002) - rms (ungated, t, t + 0.002) < -20.0)
                    return (t - from) * 1000.0;
            return 1500.0;
        };
        const double abruptMs = closeTime (0.7 + latency / sampleRate);
        const double sustainLoss = rms (ungated, 0.52, 0.69) - rms (amp, 0.52, 0.69);
        // The ringing note crosses the close threshold where its envelope reaches -56 dB.
        const double crossing = 1.5 + 0.35 * std::log (0.2 / std::pow (10.0, -56.0 / 20.0)) + latency / sampleRate;
        const double ringMs = closeTime (crossing);

        const bool ok = hissCut > 40.0 && attackLoss < 0.5 && sustainLoss < 0.1 && abruptMs < 60.0 && ringMs > abruptMs * 1.5;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s amp hiss -%.0f dB when silent, attack loss %.2f dB, held note loss %.2f dB, closes %.0f ms after a stop vs %.0f ms on a fade\n",
                     ok ? "PASS" : "FAIL", "gate", hissCut, attackLoss, sustainLoss, abruptMs, ringMs);
    }

    std::printf ("-- Wah and expression pedal --\n");
    {
        // Frequency response of the pedal wah at heel / middle / toe: the peak must sweep 400 Hz..2.2 kHz.
        const auto response = [&] (float position, float reso, double hz)
        {
            poman::Wah w;
            w.prepare (sampleRate);
            w.setParams (poman::Wah::Mode::pedal, position, reso);
            w.reset();
            std::vector<float> x ((size_t) sampleRate / 2);
            for (size_t i = 0; i < x.size(); ++i)
                x[i] = 0.1f * (float) std::sin (juce::MathConstants<double>::twoPi * hz * (double) i / sampleRate);
            w.process (x.data(), (int) x.size());
            double s = 0.0;
            for (size_t i = x.size() / 2; i < x.size(); ++i)
                s += (double) x[i] * x[i];
            return 10.0 * std::log10 (s / (double) (x.size() / 2)) - 10.0 * std::log10 (0.005);
        };
        const auto peakHz = [&] (float position)
        {
            double best = 0.0, bestDb = -1e9;
            for (double hz = 200.0; hz < 4000.0; hz *= 1.03)
                if (const double db = response (position, 0.5f, hz); db > bestDb)
                    bestDb = db, best = hz;
            return std::pair { best, bestDb };
        };
        const auto [heelHz, heelDb] = peakHz (0.0f);
        const auto [toeHz, toeDb] = peakHz (1.0f);
        const double resoLow = response (0.5f, 0.0f, 400.0 * std::sqrt (5.5)) - response (0.5f, 0.0f, 300.0);
        const double resoHigh = response (0.5f, 1.0f, 400.0 * std::sqrt (5.5)) - response (0.5f, 1.0f, 300.0);
        const bool sweepOk = std::abs (heelHz / 400.0 - 1.0) < 0.1 && std::abs (toeHz / 2200.0 - 1.0) < 0.1
                          && std::abs (heelDb - toeDb) < 3.0 && heelDb > 3.0 && resoHigh > resoLow + 4.0;
        failures += sweepOk ? 0 : 1;
        std::printf ("%s  %-24s peak %.0f Hz (heel, %+.1f dB) .. %.0f Hz (toe, %+.1f dB); RESO peak-to-300 Hz %.1f -> %.1f dB\n",
                     sweepOk ? "PASS" : "FAIL", "wah sweep", heelHz, heelDb, toeHz, toeDb, resoLow, resoHigh);

        // A pedal moving in 100 Hz steps must not click: no sample-to-sample jump beyond a steady sweep's.
        {
            poman::Wah w;
            w.prepare (sampleRate);
            const int total = (int) sampleRate;
            std::vector<float> x ((size_t) total);
            for (int i = 0; i < total; ++i)
                x[(size_t) i] = 0.2f * (float) std::sin (juce::MathConstants<double>::twoPi * 330.0 * i / sampleRate);
            float maxStep = 0.0f, last = 0.0f;
            for (int start = 0; start < total; start += 480) // 10 ms per MIDI update
            {
                w.setParams (poman::Wah::Mode::pedal, (float) start / (float) total, 0.7f);
                w.process (x.data() + start, juce::jmin (480, total - start));
                for (int i = start; i < juce::jmin (start + 480, total); ++i)
                    maxStep = std::max (maxStep, std::abs (x[(size_t) i] - last)), last = x[(size_t) i];
            }
            const bool smooth = maxStep < 0.12f; // a 330 Hz sine of this level moves up to ~0.05 per sample
            failures += smooth ? 0 : 1;
            std::printf ("%s  %-24s largest sample step %.3f while sweeping in 10 ms steps\n", smooth ? "PASS" : "FAIL", "wah zipper", maxStep);
        }

        // Toe switch: on when the pedal moves, off after 0.5 s at the heel; manual on is left alone.
        {
            using C = poman::PedalSwitch::Change;
            poman::PedalSwitch sw;
            bool on = false;
            const bool engage = sw.pedalMoved (0.4f, on, true, 0.0) == C::switchOn;
            on = engage;
            const bool stayAtToe = sw.tick (0.8f, on, 2000.0) == C::none;
            const bool notYet = sw.tick (0.0f, on, 2100.0) == C::none && sw.tick (0.0f, on, 2450.0) == C::none;
            const bool off = sw.tick (0.0f, on, 2601.0) == C::switchOff;
            poman::PedalSwitch manual;
            const bool manualStays = manual.tick (0.0f, true, 0.0) == C::none && manual.tick (0.0f, true, 9000.0) == C::none
                                  && manual.pedalMoved (0.5f, true, true, 9000.0) == C::none;
            poman::PedalSwitch disabled;
            const bool optOut = disabled.pedalMoved (0.5f, false, false, 0.0) == C::none;
            const bool ok = engage && stayAtToe && notYet && off && manualStays && optOut;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s on by pedal, off after 0.5 s at heel, manual on kept, PEDAL ON off respected\n", ok ? "PASS" : "FAIL", "wah toe switch");
        }
    }

    std::printf ("-- Looper tempo sync (120 BPM, 4/4) --\n");
    {
        constexpr int bar = 96000, beat = 24000; // at 48 kHz
        using LState = poman::Looper::State;
        using LCommand = poman::Looper::Command;
        // Runs n samples of input through the looper with a transport, returning the output. The
        // input encodes the transport sample (value = sample / 1e7) so the output shows what was looped.
        struct Rig
        {
            poman::Looper looper;
            juce::int64 transportSample = 0;
            bool running = false;
            std::vector<float> run (int n, bool silentInput = false)
            {
                std::vector<float> l ((size_t) n), r ((size_t) n);
                for (int start = 0; start < n; start += 256)
                {
                    const int m = juce::jmin (256, n - start);
                    for (int i = 0; i < m; ++i)
                        l[(size_t) (start + i)] = r[(size_t) (start + i)] = silentInput ? 0.0f : (float) ((transportSample + i) * 1.0e-7);
                    looper.process (l.data() + start, r.data() + start, m, { running, transportSample, 120.0 });
                    transportSample += m;
                }
                return l;
            }
        };

        // Late close without transport: 2.3 bars recorded -> 2 bars, playback continues in phase.
        {
            Rig rig;
            rig.looper.prepare (48000.0);
            rig.looper.setSync (true);
            rig.looper.post (LCommand::main);
            rig.run (bar * 23 / 10);
            rig.looper.post (LCommand::main);
            rig.run (256);
            const bool lengthOk = std::abs (rig.looper.getLengthSeconds() * 48000.0 - 2 * bar) < 1.0 && rig.looper.getBars() == 2;
            // Phase: 0.3 bar + 256 samples into the second cycle.
            const double expectedPos = (bar * 23 / 10 - 2 * bar + 256) / 48000.0;
            const bool phaseOk = std::abs (rig.looper.getPositionSeconds() - expectedPos) < 1.0e-6 && rig.looper.getState() == LState::playing;

            Rig early;
            early.looper.prepare (48000.0);
            early.looper.setSync (true);
            early.looper.post (LCommand::main);
            early.run (bar * 18 / 10);
            early.looper.post (LCommand::main);
            early.run (256);
            const bool stillRecording = early.looper.getState() == LState::recording;
            early.run (2 * bar - bar * 18 / 10); // reaches the bar line
            const bool earlyOk = stillRecording && early.looper.getState() == LState::playing && early.looper.getBars() == 2
                              && std::abs (early.looper.getLengthSeconds() * 48000.0 - 2 * bar) < 1.0;

            Rig plain; // sync off: unchanged behaviour
            plain.looper.prepare (48000.0);
            plain.looper.post (LCommand::main);
            plain.run (bar * 23 / 10);
            plain.looper.post (LCommand::main);
            plain.run (256);
            const bool plainOk = std::abs (plain.looper.getLengthSeconds() * 48000.0 - bar * 23 / 10) < 1.0 && plain.looper.getBars() == 0;

            const bool ok = lengthOk && phaseOk && earlyOk && plainOk;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s 2.3 bars -> %d bars (in phase), 1.8 bars -> records on to %d bars, sync off keeps %.2f s\n",
                         ok ? "PASS" : "FAIL", "loop length to bars", rig.looper.getBars(), early.looper.getBars(), plain.looper.getLengthSeconds());
        }

        // Transport running: REC at sample 10000 waits for the beat at 24000; a stopped loop restarts on its grid.
        {
            Rig rig;
            rig.running = true;
            rig.looper.prepare (48000.0);
            rig.looper.setSync (true);
            rig.run (10000);
            rig.looper.post (LCommand::main);
            rig.run (1000);
            const bool waited = rig.looper.getState() == LState::waiting;
            rig.run (beat - 11000 + bar - 500); // recording ~1 bar
            rig.looper.post (LCommand::main);   // early by 500: closes on the bar line
            rig.run (1000);
            // Silence in: the output is the loop. Its first sample must be what came in at transport 24000.
            const auto out = rig.run (2 * bar, true);
            const juce::int64 loopStartTransport = rig.transportSample - 2 * bar;
            const int offsetIntoLoop = (int) ((loopStartTransport - beat) % bar);
            const int firstOfLoop = (bar - offsetIntoLoop) % bar; // where in `out` the loop wraps to its start
            const bool startOk = std::abs (out[(size_t) firstOfLoop] - (float) (beat * 1.0e-7)) < 1.0e-6f;

            rig.looper.post (LCommand::stop);
            rig.run (bar / 3, true);
            rig.looper.post (LCommand::main);
            const auto again = rig.run (2 * bar, true);
            // After the restart, the loop's start must again land on transport samples 24000 + k * bar.
            int restartAt = -1;
            for (int i = 0; i < (int) again.size(); ++i)
                if (std::abs (again[(size_t) i] - (float) (beat * 1.0e-7)) < 1.0e-6f) { restartAt = i; break; }
            const juce::int64 restartTransport = rig.transportSample - 2 * bar + restartAt;
            const bool gridOk = restartAt >= 0 && (restartTransport - beat) % bar == 0;

            const bool ok = waited && startOk && gridOk;
            failures += ok ? 0 : 1;
            std::printf ("%s  %-24s waits for the beat, loop starts at transport %lld, restart after STOP lands on its grid\n",
                         ok ? "PASS" : "FAIL", "loop locked to drums", (long long) (startOk ? beat : -1));
        }
    }

    std::printf ("-- Delay / reverb trails (spillover) --\n");
    {
        // A hit at 0, effect switched off at 0.2 s, a second hit at 0.3 s. With trails, the first hit's
        // tail keeps ringing after 0.2 s, and the second hit comes out dry (no new echoes or reverb).
        const int total = (int) sampleRate, offAt = (int) (0.2 * sampleRate), hit2 = (int) (0.3 * sampleRate);
        const auto runBoth = [&] (bool trails)
        {
            poman::Ambience rev;
            rev.prepare (sampleRate, blockSize);
            rev.setParams (0.6f, 0.5f, poman::Ambience::Type::hall, 0.5f);
            poman::PingPongDelay dly;
            dly.prepare (sampleRate, blockSize);
            dly.setParams (120.0f, 0.5f, 0.5f);
            juce::AudioBuffer<float> buf (2, total);
            buf.clear();
            for (int ch = 0; ch < 2; ++ch)
                buf.setSample (ch, 0, 1.0f), buf.setSample (ch, hit2, 1.0f);
            for (int start = 0; start < total; start += blockSize)
            {
                const int n = juce::jmin (blockSize, total - start);
                const bool on = start < offAt;
                if (on || trails)
                {
                    dly.process (buf.getWritePointer (0, start), buf.getWritePointer (1, start), n, on);
                    auto b = juce::dsp::AudioBlock<float> (buf).getSubBlock ((size_t) start, (size_t) n);
                    rev.process (b, on);
                }
            }
            return buf;
        };
        const auto withTrails = runBoth (true), cut = runBoth (false);
        // Tail energy between 0.2 and 0.3 s (before the second hit).
        const float tail = withTrails.getRMSLevel (0, offAt + blockSize, hit2 - offAt - blockSize);
        const float cutTail = cut.getRMSLevel (0, offAt + blockSize, hit2 - offAt - blockSize);
        // After the second hit: the trailing output minus the first hit's tail must be just the dry hit.
        bool secondDry = std::abs (withTrails.getSample (0, hit2) - cut.getSample (0, hit2) - 0.0f) < 0.5f;
        // No echo of the second hit 120 ms later beyond the old tail's level.
        const float echoZone = withTrails.getMagnitude (0, hit2 + (int) (0.115 * sampleRate), (int) (0.01 * sampleRate));
        const float tailZone = withTrails.getMagnitude (0, hit2 - (int) (0.02 * sampleRate), (int) (0.01 * sampleRate));
        secondDry = secondDry && echoZone < tailZone * 2.0f + 1.0e-4f;
        const bool ok = tail > 1.0e-3f && cutTail == 0.0f && secondDry;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s tail after switch-off %.1f dB (cut: silent), new notes stay dry\n", ok ? "PASS" : "FAIL", "trails",
                     juce::Decibels::gainToDecibels (tail));
    }

    std::printf ("-- Drum kits: voicings and user samples --\n");
    {
        using poman::DrumMachine;
        const double bpm = 120.0;
        // One hit of an instrument (accent or normal) rendered dry (no room), left channel.
        const auto hit = [&] (DrumMachine& d, int instrument, bool accent, int length)
        {
            d.prepare (sampleRate);
            d.setEnabled (true);
            d.loadPreset (9); // empty
            d.setRoom (0.0f);
            d.setStep (instrument, 0, accent ? 2 : 1);
            std::vector<float> l ((size_t) length, 0.0f), r ((size_t) length, 0.0f);
            for (int start = 0; start < length; start += blockSize)
                d.process (l.data() + start, r.data() + start, juce::jmin (blockSize, length - start), start, bpm);
            return l;
        };
        const auto decayMs = [&] (const std::vector<float>& x) // time until the envelope stays below -40 dB of its peak
        {
            float peak = 0.0f;
            for (auto v : x)
                peak = std::max (peak, std::abs (v));
            int last = 0;
            for (int i = 0; i < (int) x.size(); ++i)
                if (std::abs (x[(size_t) i]) > peak * 0.01f)
                    last = i;
            return 1000.0 * last / sampleRate;
        };
        const auto highShare = [&] (const std::vector<float>& x) // energy above 7 kHz / total
        {
            poman::Biquad hp1, hp2;
            hp1.set (poman::ArrayCoeffs::makeHighPass (sampleRate, 7000.0, 0.7));
            hp2.set (poman::ArrayCoeffs::makeHighPass (sampleRate, 7000.0, 0.7));
            double all = 0.0, high = 0.0;
            for (auto v : x)
            {
                const float h = hp2 (hp1 (v));
                all += (double) v * v, high += (double) h * h;
            }
            return high / (all + 1e-30);
        };
        const int quarterBar = (int) sampleRate / 2; // fits in one 16-step bar at 120 BPM before the next hit
        DrumMachine rock, metal, studio, vintage;
        rock.setKit ((int) DrumMachine::Kit::rock), metal.setKit ((int) DrumMachine::Kit::metal);
        studio.setKit ((int) DrumMachine::Kit::studio), vintage.setKit ((int) DrumMachine::Kit::vintage);
        const double rockKick = decayMs (hit (rock, DrumMachine::kick, false, quarterBar * 3));
        const double metalKick = decayMs (hit (metal, DrumMachine::kick, false, quarterBar * 3));
        const double studioCrash = highShare (hit (studio, DrumMachine::crash, false, quarterBar));
        const double vintageCrash = highShare (hit (vintage, DrumMachine::crash, false, quarterBar));
        const bool character = metalKick < rockKick * 0.7 && vintageCrash < studioCrash * 0.8;
        failures += character ? 0 : 1;
        std::printf ("%s  %-24s kick -40 dB: Metal %.0f ms < Rock %.0f ms; crash >7 kHz share: Vintage %.2f < Studio %.2f\n",
                     character ? "PASS" : "FAIL", "kit voicings", metalKick, rockKick, vintageCrash, studioCrash);

        // File names -> instruments.
        const std::pair<const char*, int> names[] {
            { "Kick_01", DrumMachine::kick }, { "BD2", DrumMachine::kick }, { "Bombo fuerte", DrumMachine::kick },
            { "snare-hard", DrumMachine::snare }, { "SD_03", DrumMachine::snare }, { "caja", DrumMachine::snare },
            { "HH closed", DrumMachine::closedHat }, { "hihat", DrumMachine::closedHat }, { "Charles", DrumMachine::closedHat },
            { "Open Hat", DrumMachine::openHat }, { "OHH", DrumMachine::openHat }, { "clap", DrumMachine::clap },
            { "Floor Tom", DrumMachine::lowTom }, { "tom low", DrumMachine::lowTom }, { "Rack Tom", DrumMachine::highTom },
            { "tom1", DrumMachine::highTom }, { "Crash 16", DrumMachine::crash }, { "ride", DrumMachine::crash },
            { "guitar riff", -1 }, { "bdsm", -1 },
        };
        int wrong = 0;
        juce::String misses;
        for (auto [n, want] : names)
            if (DrumMachine::instrumentForFileName (n) != want)
                ++wrong, misses << n << " ";
        failures += wrong == 0 ? 0 : 1;
        std::printf ("%s  %-24s %d/%d names mapped right %s\n", wrong == 0 ? "PASS" : "FAIL", "sample file names",
                     (int) std::size (names) - wrong, (int) std::size (names), misses.toRawUTF8());

        // A temporary kit folder: a quiet and a loud kick (44.1 kHz, to exercise resampling), one snare, a non-drum file.
        const auto folder = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("poman_kit_test");
        folder.deleteRecursively();
        folder.createDirectory();
        const auto writeWav = [&] (const juce::String& name, double rate, float level, double hz)
        {
            const int n = (int) (rate * 0.3);
            juce::AudioBuffer<float> b (1, n);
            for (int i = 0; i < n; ++i)
                b.setSample (0, i, level * (float) (std::sin (juce::MathConstants<double>::twoPi * hz * i / rate) * std::exp (-i / (rate * 0.08))));
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (folder.getChildFile (name));
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (1).withBitsPerSample (24));
            writer->writeFromAudioSampleBuffer (b, 0, n);
        };
        writeWav ("Kick soft.wav", 44100.0, 0.2f, 60.0);
        writeWav ("Kick hard.wav", 44100.0, 0.8f, 60.0);
        writeWav ("Snare.wav", 48000.0, 0.5f, 200.0);
        writeWav ("Riff idea.wav", 48000.0, 0.5f, 440.0);

        DrumMachine s;
        s.prepare (sampleRate);
        const auto result = s.loadSampleKit (folder);
        const bool loaded = result.wasOk() && s.getKit() == (int) DrumMachine::Kit::samples && s.getSampleKitName() == "poman_kit_test";
        const auto normal = hit (s, DrumMachine::kick, false, quarterBar);
        const auto accent = hit (s, DrumMachine::kick, true, quarterBar);
        float normalPeak = 0.0f, accentPeak = 0.0f;
        for (int i = 0; i < quarterBar; ++i)
            normalPeak = std::max (normalPeak, std::abs (normal[(size_t) i])), accentPeak = std::max (accentPeak, std::abs (accent[(size_t) i]));
        // Layers: the accent plays the loud file (about 4x the soft one), not a scaled copy.
        const bool layers = accentPeak > normalPeak * 3.0f;
        // Playback is the file, on time: the soft kick's 60 Hz at the engine rate (first zero crossing at 1/120 s).
        int crossing = 0;
        for (int i = (int) (sampleRate * 0.001); i < quarterBar; ++i) // past the resampler's start-up ripple
            if (normal[(size_t) i - 1] > 0.0f && normal[(size_t) i] <= 0.0f) { crossing = i; break; }
        const bool pitchOk = std::abs (crossing - sampleRate / 120.0) < 3.0;
        // Missing instrument (hi hat): the Studio voice plays.
        const auto hat = hit (s, DrumMachine::closedHat, false, quarterBar / 4);
        float hatPeak = 0.0f;
        for (auto v : hat)
            hatPeak = std::max (hatPeak, std::abs (v));
        const bool fallback = hatPeak > 0.01f;

        // Swapping the kit while a long sample plays: no crash, the old voice stops.
        bool swapSafe = true;
        {
            s.setStep (DrumMachine::kick, 0, 2);
            std::vector<float> l (blockSize), r (blockSize);
            for (int b = 0; b < 40; ++b)
            {
                if (b == 3)
                    s.clearSampleKit();
                if (b == 6)
                    swapSafe = swapSafe && s.loadSampleKit (folder).wasOk();
                std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
                s.process (l.data(), r.data(), blockSize, b * blockSize, bpm);
                for (auto v : l)
                    swapSafe = swapSafe && std::isfinite (v);
            }
        }
        // The folder is saved with the session and reloaded.
        DrumMachine restored;
        restored.prepare (sampleRate);
        restored.fromValueTree (s.toValueTree());
        const bool persisted = restored.hasSampleKit() && restored.getKit() == (int) DrumMachine::Kit::samples;
        folder.deleteRecursively();

        const bool ok = loaded && layers && pitchOk && fallback && swapSafe && persisted;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s loaded %d, accent/normal %.1f (two layers), 60 Hz crossing at %d (want %.0f), missing hat -> Studio %d, swap %d, saved %d\n",
                     ok ? "PASS" : "FAIL", "sample kit", loaded, accentPeak / juce::jmax (1.0e-6f, normalPeak), crossing, sampleRate / 120.0,
                     fallback, swapSafe, persisted);
    }

    std::printf ("-- Tube models (Koren) --\n");
    {
        using namespace poman::tubes;
        // 12AX7, 100 k load, 300 V, -1.5 V bias: datasheet-like operating point and gain.
        const double vp = PreampTriode::plateVoltage (-1.5);
        const double gain = (PreampTriode::plateVoltage (-1.55) - PreampTriode::plateVoltage (-1.45)) / 0.1;
        const double cutoffVp = PreampTriode::plateVoltage (-4.0);
        const double clamp = PreampTriode::gridVoltage (3.0);
        const auto& t = PreampTriode::table();
        const double tableError = std::abs (t (-1.5f) - vp);
        const bool triodeOk = vp > 170.0 && vp < 240.0 && gain > 45.0 && gain < 80.0 && cutoffVp > 295.0
                           && clamp < 1.0 && tableError < 0.5;
        failures += triodeOk ? 0 : 1;
        std::printf ("%s  %-24s Vp %.0f V at -1.5 V, gain %.0f, cut off at -4 V (Vp %.0f V), +3 V in -> grid %.2f V\n",
                     triodeOk ? "PASS" : "FAIL", "12AX7 stage", vp, gain, cutoffVp, clamp);

        // Push-pull: odd-symmetric (even harmonics cancel), slope <= 1, class A smoother at low level.
        bool ppOk = true;
        juce::String line;
        double crossover[4] {};
        for (int k = 0; k < (int) PowerTube::count; ++k)
        {
            const auto& pp = PushPull::table ((PowerTube) k);
            double asymmetry = 0.0;
            for (float e = 0.05f; e < 3.0f; e += 0.05f)
                asymmetry = std::max (asymmetry, (double) std::abs (pp (e) + pp (-e)));
            // Low-level gain relative to the full-drive peak gain (crossover makes it smaller).
            crossover[k] = (pp (0.02f) - pp (-0.02f)) / 0.04;
            const double slope = pp.maxSlope();
            ppOk = ppOk && asymmetry < 1.0e-3 && slope <= 1.0001 && pp (3.0f) > pp (1.0f);
            line << juce::StringArray { "EL34", "6L6", "EL84", "6V6" }[k] << " " << juce::String (crossover[k], 2) << "  ";
        }
        ppOk = ppOk && crossover[(int) PowerTube::el84] > crossover[(int) PowerTube::sixL6];
        failures += ppOk ? 0 : 1;
        std::printf ("%s  %-24s symmetric, slope <= 1; small-signal gain: %s\n", ppOk ? "PASS" : "FAIL", "push-pull pairs", line.toRawUTF8());
    }

    std::printf ("-- Whammy and detune --\n");
    {
        // Median YIN pitch of the second half of a signal. 4096-sample windows hold two periods of the
        // lowest result (-2 octaves from 220 Hz = 55 Hz); YIN cannot see longer periods than half a window.
        const auto pitchOf = [&] (const std::vector<float>& y, float minHz, float maxHz)
        {
            poman::YinDetector yin;
            std::vector<float> estimates;
            for (size_t start = y.size() / 2; start + 4096 <= y.size(); start += 480)
                if (const float hz = yin.detect (y.data() + start, 4096, sampleRate, minHz, maxHz); hz > 0.0f)
                    estimates.push_back (hz);
            std::sort (estimates.begin(), estimates.end());
            return estimates.empty() ? 0.0f : estimates[estimates.size() / 2];
        };
        const int total = (int) sampleRate;
        std::vector<float> note ((size_t) total);
        for (int i = 0; i < total; ++i)
            note[(size_t) i] = 0.3f * (float) std::sin (juce::MathConstants<double>::twoPi * 220.0 * i / sampleRate);

        juce::String line;
        bool ok = true;
        for (int mode = 0; mode < poman::Whammy::modeNames().size(); ++mode)
        {
            poman::Whammy w;
            w.prepare (sampleRate);
            w.setParams (mode, 1.0f, 1.0f);
            w.reset();
            auto y = note;
            for (int start = 0; start < total; start += blockSize)
                w.process (y.data() + start, juce::jmin (blockSize, total - start));
            const float expected = 220.0f * std::exp2 (poman::Whammy::modeSemitones (mode) / 12.0f);
            const float got = pitchOf (y, 40.0f, 1900.0f);
            const float cents = got > 0.0f ? 1200.0f * std::log2 (got / expected) : 9999.0f;
            ok = ok && std::abs (cents) < 5.0f;
            line << poman::Whammy::modeNames()[mode] << " " << juce::String (cents, 1) << "c  ";
        }
        // At the heel the pedal passes the signal untouched (no comb filtering from the shifter).
        {
            poman::Whammy w;
            w.prepare (sampleRate);
            w.setParams (0, 0.0f, 1.0f);
            w.reset();
            auto y = note;
            for (int start = 0; start < total; start += blockSize)
                w.process (y.data() + start, juce::jmin (blockSize, total - start));
            float diff = 0.0f;
            for (int i = 0; i < total; ++i)
                diff = std::max (diff, std::abs (y[(size_t) i] - note[(size_t) i]));
            ok = ok && diff < 1.0e-6f;
            line << "heel diff " << juce::String (diff, 7);
        }
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s toe pitch error: %s\n", ok ? "PASS" : "FAIL", "whammy", line.toRawUTF8());

        // Detune: left flat and right sharp by the depth (15 cents at full).
        poman::Modulation mod;
        mod.prepare (sampleRate, blockSize);
        mod.setParams (poman::Modulation::Type::detune, 1.0f, 1.0f, 1.0f, 0.0f);
        std::vector<float> l = note, r = note;
        for (int start = 0; start < total; start += blockSize)
            mod.process (l.data() + start, r.data() + start, juce::jmin (blockSize, total - start));
        // The wet part alone (out - 0.5 dry) carries the shifted pitch.
        for (int i = 0; i < total; ++i)
            l[(size_t) i] -= 0.5f * note[(size_t) i], r[(size_t) i] -= 0.5f * note[(size_t) i];
        const float centsL = 1200.0f * std::log2 (pitchOf (l, 100.0f, 400.0f) / 220.0f);
        const float centsR = 1200.0f * std::log2 (pitchOf (r, 100.0f, 400.0f) / 220.0f);
        const bool detuneOk = std::abs (centsL + 15.0f) < 1.0f && std::abs (centsR - 15.0f) < 1.0f;
        failures += detuneOk ? 0 : 1;
        std::printf ("%s  %-24s left %+.1f cents, right %+.1f cents (want -15 / +15)\n", detuneOk ? "PASS" : "FAIL", "detune", centsL, centsR);
    }

    std::printf ("-- Backing track (speed / pitch / loop) --\n");
    {
        // A 4 s, 440 Hz stereo song at 44.1 kHz (the engine runs at 48 kHz, so the rate converts too).
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("poman_song_test.wav");
        {
            const double rate = 44100.0;
            const int n = (int) (rate * 4.0);
            juce::AudioBuffer<float> b (2, n);
            for (int i = 0; i < n; ++i)
            {
                const float s = 0.4f * (float) std::sin (juce::MathConstants<double>::twoPi * 440.0 * i / rate);
                b.setSample (0, i, s), b.setSample (1, i, s);
            }
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (rate).withNumChannels (2).withBitsPerSample (24));
            writer->writeFromAudioSampleBuffer (b, 0, n);
        }

        struct Run { std::vector<float> out; double seconds; };
        const auto play = [&] (double speed, int semis)
        {
            poman::BackingTrack t;
            t.prepare (sampleRate);
            t.load (file);
            t.setSpeed (speed);
            t.setSemitones (semis);
            t.setVolume (1.0f);
            t.setPlaying (true);
            Run run;
            std::vector<float> l (blockSize), r (blockSize);
            while (t.isPlaying() && run.out.size() < (size_t) (sampleRate * 20))
            {
                std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
                t.process (l.data(), r.data(), blockSize);
                run.out.insert (run.out.end(), l.begin(), l.end());
            }
            // Duration: up to the last non-silent sample.
            size_t last = 0;
            for (size_t i = 0; i < run.out.size(); ++i)
                if (std::abs (run.out[i]) > 1.0e-3f)
                    last = i;
            run.seconds = (double) last / sampleRate;
            return run;
        };
        const auto pitchCents = [&] (const std::vector<float>& y, double expectedHz)
        {
            poman::YinDetector yin;
            std::vector<float> estimates;
            for (size_t start = (size_t) (0.3 * sampleRate); start + 2048 <= y.size() - (size_t) (0.3 * sampleRate); start += 960)
                if (const float hz = yin.detect (y.data() + start, 2048, sampleRate, 100.0f, 1500.0f); hz > 0.0f)
                    estimates.push_back (hz);
            std::sort (estimates.begin(), estimates.end());
            if (estimates.empty())
                return 9999.0;
            // Worst of the 10th / 90th percentiles: a bad splice would show up as a stray estimate.
            const double lo = estimates[estimates.size() / 10], hi = estimates[estimates.size() * 9 / 10];
            const double c1 = 1200.0 * std::log2 (lo / expectedHz), c2 = 1200.0 * std::log2 (hi / expectedHz);
            return std::abs (c1) > std::abs (c2) ? c1 : c2;
        };
        const auto levelSpread = [&] (const std::vector<float>& y, double seconds) // max-min of 50 ms RMS, dB
        {
            float lo = 1e9f, hi = -1e9f;
            const int w = (int) (0.05 * sampleRate);
            for (int s = (int) (0.2 * sampleRate); s + w < (int) (seconds * sampleRate) - (int) (0.2 * sampleRate); s += w)
            {
                double e = 0.0;
                for (int i = s; i < s + w; ++i)
                    e += (double) y[(size_t) i] * y[(size_t) i];
                const float db = (float) (10.0 * std::log10 (e / w + 1e-20));
                lo = std::min (lo, db), hi = std::max (hi, db);
            }
            return hi - lo;
        };

        const auto normal = play (1.0, 0), slow = play (0.75, 0), up = play (1.0, 3);
        const double cN = pitchCents (normal.out, 440.0), cS = pitchCents (slow.out, 440.0), cU = pitchCents (up.out, 440.0 * std::exp2 (3.0 / 12.0));
        const bool durations = std::abs (normal.seconds - 4.0) < 0.08 && std::abs (slow.seconds - 4.0 / 0.75) < 0.1 && std::abs (up.seconds - 4.0) < 0.08;
        const bool pitches = std::abs (cN) < 5.0 && std::abs (cS) < 5.0 && std::abs (cU) < 5.0;
        const float spread = std::max (levelSpread (slow.out, slow.seconds), levelSpread (up.out, up.seconds));
        const bool ok = durations && pitches && spread < 1.0f;
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s 1x %.2f s %+.1fc | 0.75x %.2f s (want %.2f) %+.1fc | +3 st %.2f s %+.1fc | level spread %.2f dB\n",
                     ok ? "PASS" : "FAIL", "speed and pitch", normal.seconds, cN, slow.seconds, 4.0 / 0.75, cS, up.seconds, cU, spread);

        // A-B loop: the position stays inside 1.0-2.0 s for 6 s of playing, and playback keeps going.
        {
            poman::BackingTrack t;
            t.prepare (sampleRate);
            t.load (file);
            t.setLoop (1.0, 2.0, true);
            t.seek (1.0);
            t.setPlaying (true);
            std::vector<float> l (blockSize), r (blockSize);
            double lo = 1e9, hi = -1e9;
            for (int b = 0; b < (int) (6.0 * sampleRate) / blockSize; ++b)
            {
                std::fill (l.begin(), l.end(), 0.0f), std::fill (r.begin(), r.end(), 0.0f);
                t.process (l.data(), r.data(), blockSize);
                if (b > 10)
                    lo = std::min (lo, t.getPositionSeconds()), hi = std::max (hi, t.getPositionSeconds());
            }
            const bool loopOk = t.isPlaying() && lo >= 0.99 && hi <= 2.06;
            failures += loopOk ? 0 : 1;
            std::printf ("%s  %-24s position stayed in %.2f..%.2f s over 6 s\n", loopOk ? "PASS" : "FAIL", "A-B loop", lo, hi);
        }
        file.deleteFile();
    }

    std::printf ("-- Guitar input selection --\n");
    {
        // Four interface inputs, each a different constant, so the output shows exactly what was taken.
        constexpr int n = 8;
        std::array<std::array<float, n>, 4> data {};
        for (int ch = 0; ch < 4; ++ch)
            data[(size_t) ch].fill ((float) (ch + 1) * 0.1f);
        const float* inputs[] { data[0].data(), data[1].data(), data[2].data(), data[3].data() };
        std::array<float, n> out {};
        const auto take = [&] (int numInputs, int choice) { poman::InputSelect::read (inputs, numInputs, choice, 2, out.data(), n - 2); return out[0]; };
        const bool ok = std::abs (take (4, 2) - 0.3f) < 1.0e-6f                                     // IN 3 only (no mic bleed from the others)
                     && std::abs (take (4, 0) - 0.1f) < 1.0e-6f                                     // IN 1
                     && std::abs (take (4, poman::InputSelect::sumOfFirstTwo) - 0.15f) < 1.0e-6f    // IN 1+2 averaged
                     && std::abs (take (2, 3) - 0.1f) < 1.0e-6f                                     // IN 4 on a 2-input device -> IN 1
                     && std::abs (take (1, poman::InputSelect::sumOfFirstTwo) - 0.1f) < 1.0e-6f;    // 1+2 on a mono device -> IN 1
        failures += ok ? 0 : 1;
        std::printf ("%s  %-24s single input taken alone, 1+2 averaged, missing input falls back to IN 1\n", ok ? "PASS" : "FAIL", "input selector");
    }
    return failures;
}
