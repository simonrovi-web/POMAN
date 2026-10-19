#pragma once

#include "DspCommon.h"
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>

namespace poman
{
// Synthesised 8-voice drum machine with a 16-step pattern (one bar of 16ths).
//
// Timing is derived only from the timeline position and tempo, never from block boundaries, so a
// bar renders identically live, after a seek, or offline for the mix export. Pattern cells and
// mixer values are atomics: the UI edits them while the audio thread reads them.
class DrumMachine
{
public:
    static constexpr int numInstruments = 8;
    static constexpr int numSteps = 16;
    enum Instrument { kick, snare, clap, closedHat, openHat, lowTom, highTom, crash };

    static juce::StringArray instrumentNames() { return { "KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "LO TOM", "HI TOM", "CRASH" }; }

    struct PatternPreset
    {
        const char* name;
        float swing;
        std::array<const char*, numInstruments> rows; // 16 chars: '.' off, 'x' hit, 'X' accent
    };

    static const std::vector<PatternPreset>& patternPresets()
    {
        static const std::vector<PatternPreset> presets {
            { "Rock", 0.0f, { "X.......X.x.....", "....X.......X...", "................", "x.x.x.x.x.x.x.x.",
                              "................", "................", "................", "X..............." } },
            { "Metal", 0.0f, { "xxxxxxxxxxxxxxxx", "....X.......X...", "................", "x...x...x...x...",
                               "................", "................", "................", "X..............." } },
            { "Djent", 0.0f, { "X..x..X...x.X..x", "........X.......", "................", "x.x.x.x.x.x.x.x.",
                               "................", "................", "................", "X..............." } },
            { "Funk", 0.15f, { "X..x..x...x..x..", "....X..x.x..X..x", "................", "xxxxxxxxxxxxxx.x",
                               "..............x.", "................", "................", "................" } },
            { "Shuffle", 0.65f, { "X.......X.......", "....X.......X...", "................", "x.xxx.xxx.xxx.xx",
                                  "................", "................", "................", "................" } },
            { "Pop", 0.0f, { "X...X...X...X...", "................", "....X.......X...", "x...x...x...x...",
                             "..x...x...x...x.", "................", "................", "................" } },
            { "Punk", 0.0f, { "X.x.....X.x.....", "....X.......X...", "................", "X.x.X.x.X.x.X.x.",
                              "................", "................", "................", "X..............." } },
            { "Reggae", 0.3f, { "........X.......", "........X.......", "................", "..x...x...x...x.",
                                "................", "................", "................", "................" } },
            { "Tom Groove", 0.0f, { "X.....x.X.......", "....X.......X...", "................", "x.x.x.x.x.x.x.x.",
                                    "................", "..........x..x..", "..............xx", "X..............." } },
            { "Empty", 0.0f, { "................", "................", "................", "................",
                               "................", "................", "................", "................" } },
        };
        return presets;
    }

    DrumMachine()
    {
        constexpr std::array<float, numInstruments> defaultLevels { 0.9f, 0.8f, 0.6f, 0.45f, 0.45f, 0.7f, 0.7f, 0.5f };
        for (int i = 0; i < numInstruments; ++i)
            levels[(size_t) i].store (defaultLevels[(size_t) i]);
        loadPreset (0);
    }

    // Copies pattern and mixer (not voice state), e.g. for an offline render.
    void copySettingsFrom (const DrumMachine& other)
    {
        for (size_t c = 0; c < cells.size(); ++c)
            cells[c].store (other.cells[c].load());
        for (size_t i = 0; i < levels.size(); ++i)
            levels[i].store (other.levels[i].load());
        swing.store (other.swing.load());
        master.store (other.master.load());
        enabled.store (other.enabled.load());
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        chokeRate = (float) std::exp (-1.0 / (0.006 * sampleRate));
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v.active = false;
    }

    // Adds the drums for the block starting at timeline sample `position` (negative during a count-in).
    void process (float* left, float* right, int numSamples, juce::int64 position, double bpm) noexcept
    {
        if (! enabled.load())
            return;

        const double samplesPerStep = sampleRate * 60.0 / juce::jmax (20.0, bpm) / 4.0;
        const juce::int64 end = position + numSamples;
        auto step = (juce::int64) std::floor ((double) position / samplesPerStep) - 1;
        int cursor = 0;

        for (;; ++step)
        {
            const auto time = triggerTime (step, samplesPerStep);
            if (time >= end)
                break;
            if (time < position)
                continue;

            const int offset = (int) (time - position);
            render (left, right, cursor, offset);
            cursor = offset;

            const int column = (int) (((step % numSteps) + numSteps) % numSteps);
            for (int inst = 0; inst < numInstruments; ++inst)
                if (const int cell = getStep (inst, column); cell > 0)
                    trigger (inst, cell == 2 ? 1.0f : 0.72f);
        }

        render (left, right, cursor, numSamples);
    }

    // Lets ringing voices decay without new hits (transport stopped).
    void processTails (float* left, float* right, int numSamples) noexcept { render (left, right, 0, numSamples); }

    // The step whose trigger is the most recent one at `position`.
    int stepAt (juce::int64 position, double bpm) const
    {
        const double samplesPerStep = sampleRate * 60.0 / juce::jmax (20.0, bpm) / 4.0;
        auto step = (juce::int64) std::floor ((double) position / samplesPerStep);
        if (triggerTime (step, samplesPerStep) > position)
            --step;
        return (int) (((step % numSteps) + numSteps) % numSteps);
    }

    int getStep (int instrument, int step) const { return cells[(size_t) (instrument * numSteps + step)].load(); }
    void setStep (int instrument, int step, int value) { cells[(size_t) (instrument * numSteps + step)].store ((std::uint8_t) juce::jlimit (0, 2, value)); }

    void loadPreset (int index)
    {
        const auto& presets = patternPresets();
        const auto& p = presets[(size_t) juce::jlimit (0, (int) presets.size() - 1, index)];
        for (int inst = 0; inst < numInstruments; ++inst)
            for (int s = 0; s < numSteps; ++s)
            {
                const char c = p.rows[(size_t) inst][s];
                setStep (inst, s, c == 'X' ? 2 : (c == 'x' ? 1 : 0));
            }
        swing.store (p.swing);
    }

    // Index of the factory pattern equal to the current grid, or -1 for a custom pattern.
    int matchingPreset() const
    {
        const auto& presets = patternPresets();
        for (int p = 0; p < (int) presets.size(); ++p)
        {
            bool same = true;
            for (int inst = 0; inst < numInstruments && same; ++inst)
                for (int s = 0; s < numSteps && same; ++s)
                {
                    const char c = presets[(size_t) p].rows[(size_t) inst][s];
                    same = getStep (inst, s) == (c == 'X' ? 2 : (c == 'x' ? 1 : 0));
                }
            if (same)
                return p;
        }
        return -1;
    }

    void setSwing (float s) { swing.store (juce::jlimit (0.0f, 1.0f, s)); }
    float getSwing() const { return swing.load(); }
    void setLevel (int instrument, float level) { levels[(size_t) instrument].store (level); }
    float getLevel (int instrument) const { return levels[(size_t) instrument].load(); }
    void setMasterLevel (float level) { master.store (level); }
    float getMasterLevel() const { return master.load(); }
    void setEnabled (bool on) { enabled.store (on); }
    bool isEnabled() const { return enabled.load(); }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("Drums");
        tree.setProperty ("enabled", enabled.load(), nullptr);
        tree.setProperty ("swing", swing.load(), nullptr);
        tree.setProperty ("master", master.load(), nullptr);
        juce::String pattern, levelList;
        for (const auto& c : cells)
            pattern << (int) c.load();
        for (const auto& l : levels)
            levelList << juce::String (l.load(), 4) << " ";
        tree.setProperty ("pattern", pattern, nullptr);
        tree.setProperty ("levels", levelList.trim(), nullptr);
        return tree;
    }

    void fromValueTree (const juce::ValueTree& tree)
    {
        if (! tree.hasType ("Drums"))
            return;
        enabled.store ((bool) tree.getProperty ("enabled", false));
        swing.store ((float) tree.getProperty ("swing", 0.0f));
        master.store ((float) tree.getProperty ("master", 0.5f));

        const auto pattern = tree.getProperty ("pattern").toString();
        if (pattern.length() == (int) cells.size())
            for (size_t c = 0; c < cells.size(); ++c)
                cells[c].store ((std::uint8_t) juce::jlimit (0, 2, (int) pattern[(int) c] - (int) '0'));

        const auto levelList = juce::StringArray::fromTokens (tree.getProperty ("levels").toString(), " ", {});
        for (int i = 0; i < juce::jmin (numInstruments, levelList.size()); ++i)
            levels[(size_t) i].store (levelList[i].getFloatValue());
    }

private:
    struct Voice
    {
        bool active = false;
        double t = 0.0; // samples since the hit
        float velocity = 1.0f, choke = 1.0f;
        bool choking = false;
        double phase = 0.0;
        std::array<double, 6> metalPhases {};
        std::uint32_t noiseState = 1;
        Biquad filter;
    };

    juce::int64 triggerTime (juce::int64 step, double samplesPerStep) const
    {
        // Swing delays every off-beat 16th by up to half a step.
        const double swingDelay = (step & 1) != 0 ? 0.5 * swing.load() * samplesPerStep : 0.0;
        return (juce::int64) std::llround ((double) step * samplesPerStep + swingDelay);
    }

    void trigger (int instrument, float velocity) noexcept
    {
        auto& v = voices[(size_t) instrument];
        v.active = true;
        v.t = 0.0;
        v.velocity = velocity;
        v.choke = 1.0f;
        v.choking = false;
        v.phase = 0.0;
        v.metalPhases = {};
        v.noiseState = 0x9e3779b9u + (std::uint32_t) instrument * 7919u; // same hit, same noise
        v.filter.reset();

        switch (instrument)
        {
            case snare:     v.filter.set (ArrayCoeffs::makeHighPass (sampleRate, 1800.0f, 0.7f)); break;
            case clap:      v.filter.set (ArrayCoeffs::makeBandPass (sampleRate, 1100.0f, 1.2f)); break;
            case closedHat:
            case openHat:   v.filter.set (ArrayCoeffs::makeHighPass (sampleRate, 7000.0f, 0.7f)); break;
            case crash:     v.filter.set (ArrayCoeffs::makeHighPass (sampleRate, 4500.0f, 0.7f)); break;
            default: break;
        }

        if (instrument == closedHat)
            voices[openHat].choking = true; // a closed hat cuts the open one, like a real pedal
    }

    static float noise (Voice& v) noexcept
    {
        v.noiseState = v.noiseState * 1664525u + 1013904223u;
        return (float) (v.noiseState >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    float metal (Voice& v, double scale) const noexcept
    {
        // Six detuned square waves at inharmonic ratios: the classic analogue cymbal source.
        constexpr std::array<double, 6> freqs { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
        float sum = 0.0f;
        for (size_t i = 0; i < freqs.size(); ++i)
        {
            v.metalPhases[i] += freqs[i] * scale / sampleRate;
            v.metalPhases[i] -= std::floor (v.metalPhases[i]);
            sum += v.metalPhases[i] < 0.5 ? 1.0f : -1.0f;
        }
        return sum / 6.0f;
    }

    float renderVoice (int instrument, Voice& v) noexcept
    {
        const double sec = v.t / sampleRate;
        const auto env = [sec] (double tau) { return (float) std::exp (-sec / tau); };
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        float out = 0.0f, amp = 1.0f;

        switch (instrument)
        {
            case kick:
            {
                v.phase += (45.0 + 110.0 * std::exp (-sec / 0.03)) / sampleRate;
                amp = env (0.28);
                out = std::tanh (1.5f * ((float) std::sin (twoPi * v.phase) * amp + 0.3f * noise (v) * env (0.002)));
                break;
            }
            case snare:
            {
                const float tone = 0.5f * (float) (std::sin (twoPi * 180.0 * sec) + 0.6 * std::sin (twoPi * 330.0 * sec)) * env (0.07);
                amp = env (0.16);
                out = 0.6f * tone + 0.9f * v.filter (noise (v)) * amp;
                break;
            }
            case clap:
            {
                // Three quick bursts then a tail.
                const double burst = std::fmod (sec, 0.011);
                const float bursts = sec < 0.033 ? (float) std::exp (-burst / 0.004) : 0.0f;
                amp = sec < 0.033 ? 1.0f : (float) std::exp (-(sec - 0.033) / 0.18);
                out = 1.4f * v.filter (noise (v)) * (sec < 0.033 ? bursts : amp);
                break;
            }
            case closedHat:
                amp = env (0.035);
                out = v.filter (0.7f * metal (v, 1.0) + 0.3f * noise (v)) * amp;
                break;
            case openHat:
                amp = env (0.35);
                out = v.filter (0.7f * metal (v, 1.0) + 0.3f * noise (v)) * amp;
                break;
            case lowTom:
            case highTom:
            {
                const double base = instrument == lowTom ? 95.0 : 150.0;
                v.phase += (base + 60.0 * std::exp (-sec / 0.05)) / sampleRate;
                amp = env (0.35);
                out = (float) std::sin (twoPi * v.phase) * amp + 0.05f * noise (v) * env (0.01);
                break;
            }
            case crash:
                amp = env (1.4) * (1.0f - env (0.002));
                out = v.filter (0.5f * metal (v, 1.3) + 0.5f * noise (v)) * amp;
                break;
            default:
                break;
        }

        if (v.choking)
            v.choke *= chokeRate;

        v.t += 1.0;
        if (amp * v.choke < 1.0e-4f && v.t > sampleRate * 0.01)
            v.active = false;

        return out * v.velocity * v.choke;
    }

    void render (float* left, float* right, int from, int to) noexcept
    {
        constexpr std::array<float, numInstruments> pan { 0.0f, 0.0f, 0.1f, 0.3f, 0.3f, -0.4f, 0.4f, -0.25f };
        const float m = master.load();

        for (int inst = 0; inst < numInstruments; ++inst)
        {
            auto& v = voices[(size_t) inst];
            if (! v.active)
                continue;

            const float gain = m * levels[(size_t) inst].load();
            const float angle = (pan[(size_t) inst] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            const float gl = gain * std::cos (angle) * juce::MathConstants<float>::sqrt2;
            const float gr = gain * std::sin (angle) * juce::MathConstants<float>::sqrt2;

            for (int i = from; i < to && v.active; ++i)
            {
                const float s = renderVoice (inst, v);
                left[i] += gl * s;
                right[i] += gr * s;
            }
        }
    }

    double sampleRate = 48000.0;
    float chokeRate = 0.99f;
    std::array<Voice, numInstruments> voices;
    std::array<std::atomic<std::uint8_t>, numInstruments * numSteps> cells {};
    std::array<std::atomic<float>, numInstruments> levels {};
    std::atomic<float> swing { 0.0f }, master { 0.5f }; // 0.5 keeps the busiest preset below full scale
    std::atomic<bool> enabled { false };
};
} // namespace poman
