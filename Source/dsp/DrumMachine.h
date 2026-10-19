#pragma once

#include "DspCommon.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_data_structures/juce_data_structures.h>
#include <atomic>

namespace poman
{
// Synthesised 8-voice drum machine with a 16-step pattern (one bar of 16ths) and two kits.
//
// Timing is derived only from the timeline position and tempo, never from block boundaries, so a
// bar renders identically live, after a seek, or offline for the mix export. All per-sample state
// (voices, room, bus compressor) is processed sample by sample for the same reason. Pattern cells
// and mixer values are atomics: the UI edits them while the audio thread reads them.
class DrumMachine
{
public:
    static constexpr int numInstruments = 8;
    static constexpr int numSteps = 16;
    enum Instrument { kick, snare, clap, closedHat, openHat, lowTom, highTom, crash };
    // Studio: modelled acoustic kit with room and glue compression. Electronic: the dry analogue-style kit.
    enum class Kit { studio, electronic, count };

    static juce::StringArray instrumentNames() { return { "KICK", "SNARE", "CLAP", "CL HAT", "OP HAT", "LO TOM", "HI TOM", "CRASH" }; }
    static juce::StringArray kitNames() { return { "Studio", "Electronic" }; }

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

    // Copies pattern, kit and mixer (not voice state), e.g. for an offline render.
    void copySettingsFrom (const DrumMachine& other)
    {
        for (size_t c = 0; c < cells.size(); ++c)
            cells[c].store (other.cells[c].load());
        for (size_t i = 0; i < levels.size(); ++i)
            levels[i].store (other.levels[i].load());
        swing.store (other.swing.load());
        master.store (other.master.load());
        room.store (other.room.load());
        kit.store (other.kit.load());
        enabled.store (other.enabled.load());
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        chokeRate = (float) std::exp (-1.0 / (0.006 * sampleRate));
        compAttack = onePoleCoeff (sampleRate, 1.5f);
        compRelease = onePoleCoeff (sampleRate, 150.0f);

        juce::Reverb::Parameters p;
        p.roomSize = 0.32f;
        p.damping = 0.55f;
        p.width = 0.9f;
        p.wetLevel = 1.0f;
        p.dryLevel = 0.0f;
        roomReverb.setParameters (p);
        roomReverb.setSampleRate (sampleRate);
        reset();
    }

    void reset()
    {
        for (auto& v : voices)
            v.active = false;
        roomReverb.reset();
        compEnvelope = 0.0f;
    }

    // Adds the drums for the block starting at timeline sample `position` (negative during a count-in).
    void process (float* left, float* right, int numSamples, juce::int64 position, double bpm) noexcept
    {
        if (enabled.load())
            run (left, right, numSamples, position, bpm, true);
    }

    // Lets ringing voices and the room decay without new hits (transport stopped).
    void processTails (float* left, float* right, int numSamples) noexcept
    {
        if (enabled.load())
            run (left, right, numSamples, 0, 120.0, false);
    }

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
    void setRoom (float amount) { room.store (juce::jlimit (0.0f, 1.0f, amount)); }
    float getRoom() const { return room.load(); }
    void setKit (int k) { kit.store (juce::jlimit (0, (int) Kit::count - 1, k)); }
    int getKit() const { return kit.load(); }
    void setEnabled (bool on) { enabled.store (on); }
    bool isEnabled() const { return enabled.load(); }

    juce::ValueTree toValueTree() const
    {
        juce::ValueTree tree ("Drums");
        tree.setProperty ("enabled", enabled.load(), nullptr);
        tree.setProperty ("swing", swing.load(), nullptr);
        tree.setProperty ("master", master.load(), nullptr);
        tree.setProperty ("room", room.load(), nullptr);
        tree.setProperty ("kit", kit.load(), nullptr);
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
        room.store ((float) tree.getProperty ("room", 0.35f));
        // States saved before kits existed get the improved Studio kit (the old sound stays selectable).
        setKit ((int) tree.getProperty ("kit", (int) Kit::studio));

        const auto pattern = tree.getProperty ("pattern").toString();
        if (pattern.length() == (int) cells.size())
            for (size_t c = 0; c < cells.size(); ++c)
                cells[c].store ((std::uint8_t) juce::jlimit (0, 2, (int) pattern[(int) c] - (int) '0'));

        const auto levelList = juce::StringArray::fromTokens (tree.getProperty ("levels").toString(), " ", {});
        for (int i = 0; i < juce::jmin (numInstruments, levelList.size()); ++i)
            levels[(size_t) i].store (levelList[i].getFloatValue());
    }

private:
    struct Stereo { float l, r; };

    struct Voice
    {
        bool active = false;
        double t = 0.0; // samples since the hit
        float velocity = 1.0f, choke = 1.0f;
        bool choking = false;
        double phase = 0.0;
        std::array<double, 6> metalPhases {};
        std::uint32_t noiseL = 1, noiseR = 2;
        std::array<Biquad, 2> filtersL, filtersR;
    };

    static constexpr int chunkSize = 256;

    juce::int64 triggerTime (juce::int64 step, double samplesPerStep) const
    {
        // Swing delays every off-beat 16th by up to half a step.
        const double swingDelay = (step & 1) != 0 ? 0.5 * swing.load() * samplesPerStep : 0.0;
        return (juce::int64) std::llround ((double) step * samplesPerStep + swingDelay);
    }

    // Renders into the private drum bus in fixed chunks, then mixes the bus into the output.
    void run (float* left, float* right, int numSamples, juce::int64 position, double bpm, bool withTriggers) noexcept
    {
        for (int done = 0; done < numSamples;)
        {
            const int n = juce::jmin (chunkSize, numSamples - done);
            std::fill (busL.begin(), busL.begin() + n, 0.0f);
            std::fill (busR.begin(), busR.begin() + n, 0.0f);
            std::fill (sendL.begin(), sendL.begin() + n, 0.0f);
            std::fill (sendR.begin(), sendR.begin() + n, 0.0f);

            if (withTriggers)
                sequence (n, position + done, bpm);
            else
                render (0, n);

            finishBus (left + done, right + done, n);
            done += n;
        }
    }

    void sequence (int numSamples, juce::int64 position, double bpm) noexcept
    {
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
            render (cursor, offset);
            cursor = offset;

            const int column = (int) (((step % numSteps) + numSteps) % numSteps);
            for (int inst = 0; inst < numInstruments; ++inst)
                if (const int cell = getStep (inst, column); cell > 0)
                    trigger (inst, cell == 2 ? 1.0f : 0.72f);
        }

        render (cursor, numSamples);
    }

    void finishBus (float* left, float* right, int n) noexcept
    {
        if (kit.load() == (int) Kit::studio)
        {
            // Room: a short, damped space fed by per-voice sends (snare and toms more than kick).
            roomReverb.processStereo (sendL.data(), sendR.data(), n);
            const float roomGain = 0.9f * room.load();

            // Glue: stereo-linked peak compressor, 3:1 above -14 dBFS with a fast 1.5 ms attack, then make-up.
            constexpr float threshold = 0.2f, inverseRatio = 1.0f / 3.0f, makeUp = 1.1f;
            for (int i = 0; i < n; ++i)
            {
                const float l = busL[(size_t) i] + roomGain * sendL[(size_t) i];
                const float r = busR[(size_t) i] + roomGain * sendR[(size_t) i];
                const float level = juce::jmax (std::abs (l), std::abs (r));
                const float coeff = level > compEnvelope ? compAttack : compRelease;
                compEnvelope = level + coeff * (compEnvelope - level);
                const float gain = compEnvelope > threshold
                                       ? threshold * std::pow (compEnvelope / threshold, inverseRatio) / compEnvelope
                                       : 1.0f;
                left[i] += softLimit (l * gain * makeUp);
                right[i] += softLimit (r * gain * makeUp);
            }
        }
        else
        {
            for (int i = 0; i < n; ++i)
            {
                left[i] += softLimit (busL[(size_t) i]);
                right[i] += softLimit (busR[(size_t) i]);
            }
        }
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
        v.noiseL = 0x9e3779b9u + (std::uint32_t) instrument * 7919u; // same hit, same noise
        v.noiseR = 0x85ebca6bu + (std::uint32_t) instrument * 104729u;
        for (auto* bank : { &v.filtersL, &v.filtersR })
            for (auto& f : *bank)
                f.reset();

        const auto setBoth = [&v] (int index, const BiquadCoeffs& c)
        {
            v.filtersL[(size_t) index].set (c);
            v.filtersR[(size_t) index].set (c);
        };
        const double sr = sampleRate;

        if (kit.load() == (int) Kit::studio)
        {
            switch (instrument)
            {
                case kick:   setBoth (0, ArrayCoeffs::makeBandPass (sr, 3200.0f, 0.9f)); break;
                case snare:  setBoth (0, ArrayCoeffs::makeBandPass (sr, 4500.0f, 0.6f));
                             setBoth (1, ArrayCoeffs::makeHighPass (sr, 1200.0f, 0.7f)); break;
                case clap:   setBoth (0, ArrayCoeffs::makeBandPass (sr, 1300.0f, 1.1f));
                             setBoth (1, ArrayCoeffs::makeHighPass (sr, 500.0f, 0.7f)); break;
                case closedHat:
                case openHat:
                             setBoth (0, ArrayCoeffs::makeHighPass (sr, 5500.0f, 0.7f));
                             setBoth (1, ArrayCoeffs::makeLowPass (sr, (float) juce::jmin (13000.0, sr * 0.45), 0.7f)); break;
                case lowTom:
                case highTom: setBoth (0, ArrayCoeffs::makeBandPass (sr, 2000.0f, 1.0f)); break;
                case crash:  setBoth (0, ArrayCoeffs::makeHighPass (sr, 2500.0f, 0.7f));
                             setBoth (1, ArrayCoeffs::makeLowPass (sr, (float) juce::jmin (11000.0, sr * 0.45), 0.7f)); break;
                default: break;
            }
        }
        else
        {
            switch (instrument)
            {
                case snare:     setBoth (0, ArrayCoeffs::makeHighPass (sr, 1800.0f, 0.7f)); break;
                case clap:      setBoth (0, ArrayCoeffs::makeBandPass (sr, 1100.0f, 1.2f)); break;
                case closedHat:
                case openHat:   setBoth (0, ArrayCoeffs::makeHighPass (sr, 7000.0f, 0.7f)); break;
                case crash:     setBoth (0, ArrayCoeffs::makeHighPass (sr, 4500.0f, 0.7f)); break;
                default: break;
            }
        }

        if (instrument == closedHat)
            voices[openHat].choking = true; // a closed hat cuts the open one, like a real pedal
    }

    // Transparent below 0.8, then rounds peaks off towards 1.0.
    static float softLimit (float x) noexcept
    {
        const float a = std::abs (x);
        return a <= 0.8f ? x : std::copysign (0.8f + 0.2f * std::tanh ((a - 0.8f) / 0.2f), x);
    }

    static float nextNoise (std::uint32_t& state) noexcept
    {
        state = state * 1664525u + 1013904223u;
        return (float) (state >> 8) * (2.0f / 16777216.0f) - 1.0f;
    }

    //==============================================================================
    // Studio kit: band-limited models of an acoustic kit.
    Stereo renderStudio (int instrument, Voice& v, float& amp) noexcept
    {
        const double sec = v.t / sampleRate;
        const auto env = [sec] (double tau) { return (float) std::exp (-sec / tau); };
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        const float vel = v.velocity;
        const float attack = 1.0f - (float) std::exp (-sec / 0.0008); // softens the very first sample

        switch (instrument)
        {
            case kick:
            {
                // Drum head: 125 Hz falling to 50 Hz, a second mode for punch, and the felt beater's click.
                v.phase += (50.0 + 75.0 * std::exp (-sec / 0.045)) / sampleRate;
                amp = env (0.17); // a damped kick: about 0.8 s to -40 dB
                const float body = (float) std::sin (twoPi * v.phase) * amp * attack;
                const float punch = 0.25f * (float) std::sin (2.0 * twoPi * v.phase) * env (0.06);
                const float beater = 0.6f * vel * v.filtersL[0] (nextNoise (v.noiseL)) * env (0.004);
                const float x = std::tanh (1.6f * (body + punch + beater)) / std::tanh (1.6f);
                return { x, x };
            }
            case snare:
            {
                // Shell modes (slight pitch drop) + snare wires (band noise) + stick crack.
                v.phase += 190.0 * (1.0 + 0.08 * std::exp (-sec / 0.02)) / sampleRate;
                const float shell = (float) std::sin (twoPi * v.phase) * env (0.11)
                                  + 0.45f * (float) std::sin (twoPi * 330.0 * sec) * env (0.07)
                                  + 0.25f * (float) std::sin (twoPi * 470.0 * sec) * env (0.04);
                const float n = nextNoise (v.noiseL);
                amp = env (0.13);
                const float wires = 0.95f * v.filtersL[0] (n) * amp;
                const float mid = 0.4f * v.filtersL[1] (n) * env (0.06);
                const float crack = 0.5f * vel * n * env (0.006);
                const float x = std::tanh (1.2f * (0.6f * shell * attack + wires + mid + crack));
                return { x, x };
            }
            case clap:
            {
                // Four slightly uneven hand slaps, then the room tail.
                constexpr std::array<double, 4> slaps { 0.0, 0.008, 0.017, 0.028 };
                float burst = 0.0f;
                for (auto s : slaps)
                    if (sec >= s)
                        burst = (float) std::exp (-(sec - s) / 0.0045);
                const float tail = sec > 0.028 ? (float) std::exp (-(sec - 0.028) / 0.22) : 0.0f;
                amp = sec < 0.03 ? 1.0f : tail;
                const float x = 1.6f * v.filtersL[1] (v.filtersL[0] (nextNoise (v.noiseL))) * juce::jmax (burst, 0.8f * tail);
                return { x, x };
            }
            case closedHat:
            case openHat:
            {
                // Seven inharmonic sine partials (no aliasing) plus noise, band-limited to 5.5-13 kHz.
                constexpr std::array<double, 7> partials { 3270.0, 4183.0, 5260.0, 6727.0, 8110.0, 9937.0, 11870.0 };
                float metal = 0.0f;
                for (size_t i = 0; i < partials.size(); ++i)
                    if (partials[i] < sampleRate * 0.45)
                        metal += (float) std::sin (twoPi * partials[i] * sec + (double) i);
                metal /= (float) partials.size();

                amp = env (instrument == closedHat ? 0.045 : 0.42) * (1.0f - (float) std::exp (-sec / 0.0003));
                const float l = v.filtersL[1] (v.filtersL[0] (0.45f * metal + 0.75f * nextNoise (v.noiseL))) * amp;
                if (instrument == closedHat)
                    return { l, l };
                const float r = v.filtersR[1] (v.filtersR[0] (0.45f * metal + 0.75f * nextNoise (v.noiseR))) * amp;
                return { l, r }; // decorrelated noise: the open hat breathes in stereo
            }
            case lowTom:
            case highTom:
            {
                const double base = instrument == lowTom ? 98.0 : 150.0;
                v.phase += base * (1.0 + 0.35 * std::exp (-sec / 0.06)) / sampleRate;
                amp = env (instrument == lowTom ? 0.26 : 0.2);
                const float body = (float) std::sin (twoPi * v.phase) * amp * attack;
                const float mode2 = 0.3f * (float) std::sin (twoPi * v.phase * 1.52) * env (0.09);
                const float stick = 0.5f * vel * v.filtersL[0] (nextNoise (v.noiseL)) * env (0.006);
                const float x = std::tanh (1.3f * (body + mode2 + stick));
                return { x, x };
            }
            case crash:
            {
                constexpr std::array<double, 12> partials { 2840.0, 3510.0, 4220.0, 4970.0, 5730.0, 6480.0,
                                                            7330.0, 8150.0, 9070.0, 10180.0, 11360.0, 12610.0 };
                float metal = 0.0f;
                for (size_t i = 0; i < partials.size(); ++i)
                    if (partials[i] < sampleRate * 0.45)
                        metal += (float) std::sin (twoPi * partials[i] * sec + 0.7 * (double) i);
                metal /= (float) partials.size();

                // Bright splash on the hit, then the long wash.
                amp = (0.5f * env (0.08) + 0.5f * env (1.9)) * (1.0f - (float) std::exp (-sec / 0.0015));
                const float l = v.filtersL[1] (v.filtersL[0] (0.35f * metal + 0.9f * nextNoise (v.noiseL))) * amp;
                const float r = v.filtersR[1] (v.filtersR[0] (0.35f * metal + 0.9f * nextNoise (v.noiseR))) * amp;
                return { l, r };
            }
            default:
                break;
        }
        amp = 0.0f;
        return { 0.0f, 0.0f };
    }

    //==============================================================================
    // Electronic kit: the original analogue-style voices, unchanged.
    float metalSquares (Voice& v, double scale) const noexcept
    {
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

    float renderElectronic (int instrument, Voice& v, float& amp) noexcept
    {
        const double sec = v.t / sampleRate;
        const auto env = [sec] (double tau) { return (float) std::exp (-sec / tau); };
        constexpr double twoPi = juce::MathConstants<double>::twoPi;
        auto& filter = v.filtersL[0];
        float out = 0.0f;
        amp = 1.0f;

        switch (instrument)
        {
            case kick:
                v.phase += (45.0 + 110.0 * std::exp (-sec / 0.03)) / sampleRate;
                amp = env (0.28);
                out = std::tanh (1.5f * ((float) std::sin (twoPi * v.phase) * amp + 0.3f * nextNoise (v.noiseL) * env (0.002)));
                break;
            case snare:
            {
                const float tone = 0.5f * (float) (std::sin (twoPi * 180.0 * sec) + 0.6 * std::sin (twoPi * 330.0 * sec)) * env (0.07);
                amp = env (0.16);
                out = 0.6f * tone + 0.9f * filter (nextNoise (v.noiseL)) * amp;
                break;
            }
            case clap:
            {
                const double burst = std::fmod (sec, 0.011);
                const float bursts = sec < 0.033 ? (float) std::exp (-burst / 0.004) : 0.0f;
                amp = sec < 0.033 ? 1.0f : (float) std::exp (-(sec - 0.033) / 0.18);
                out = 1.4f * filter (nextNoise (v.noiseL)) * (sec < 0.033 ? bursts : amp);
                break;
            }
            case closedHat:
                amp = env (0.035);
                out = filter (0.7f * metalSquares (v, 1.0) + 0.3f * nextNoise (v.noiseL)) * amp;
                break;
            case openHat:
                amp = env (0.35);
                out = filter (0.7f * metalSquares (v, 1.0) + 0.3f * nextNoise (v.noiseL)) * amp;
                break;
            case lowTom:
            case highTom:
            {
                const double base = instrument == lowTom ? 95.0 : 150.0;
                v.phase += (base + 60.0 * std::exp (-sec / 0.05)) / sampleRate;
                amp = env (0.35);
                out = (float) std::sin (twoPi * v.phase) * amp + 0.05f * nextNoise (v.noiseL) * env (0.01);
                break;
            }
            case crash:
                amp = env (1.4) * (1.0f - env (0.002));
                out = filter (0.5f * metalSquares (v, 1.3) + 0.5f * nextNoise (v.noiseL)) * amp;
                break;
            default:
                break;
        }
        return out;
    }

    void render (int from, int to) noexcept
    {
        constexpr std::array<float, numInstruments> pan { 0.0f, 0.0f, 0.1f, 0.3f, 0.3f, -0.4f, 0.4f, -0.25f };
        // Room sends (Studio kit): snare, clap and toms live in the room; kick and hats stay close.
        constexpr std::array<float, numInstruments> sends { 0.06f, 0.3f, 0.35f, 0.08f, 0.12f, 0.22f, 0.22f, 0.15f };
        const bool studio = kit.load() == (int) Kit::studio;
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
            const float send = sends[(size_t) inst];

            for (int i = from; i < to && v.active; ++i)
            {
                float amp = 0.0f;
                Stereo s;
                if (studio)
                {
                    s = renderStudio (inst, v, amp);
                }
                else
                {
                    const float x = renderElectronic (inst, v, amp);
                    s = { x, x };
                }

                if (v.choking)
                    v.choke *= chokeRate;
                v.t += 1.0;
                if (amp * v.choke < 1.0e-4f && v.t > sampleRate * 0.01)
                    v.active = false;

                const float l = gl * s.l * v.velocity * v.choke;
                const float r = gr * s.r * v.velocity * v.choke;
                busL[(size_t) i] += l;
                busR[(size_t) i] += r;
                sendL[(size_t) i] += send * l;
                sendR[(size_t) i] += send * r;
            }
        }
    }

    double sampleRate = 48000.0;
    float chokeRate = 0.99f, compAttack = 0.0f, compRelease = 0.0f, compEnvelope = 0.0f;
    std::array<Voice, numInstruments> voices;
    std::array<float, chunkSize> busL {}, busR {}, sendL {}, sendR {};
    juce::Reverb roomReverb;

    std::array<std::atomic<std::uint8_t>, numInstruments * numSteps> cells {};
    std::array<std::atomic<float>, numInstruments> levels {};
    std::atomic<float> swing { 0.0f }, master { 0.5f }, room { 0.35f }; // master 0.5 keeps the busiest preset below full scale
    std::atomic<int> kit { (int) Kit::studio };
    std::atomic<bool> enabled { false };
};
} // namespace poman
