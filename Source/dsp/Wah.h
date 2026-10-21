#pragma once

#include "DspCommon.h"

namespace poman
{
// Wah: a resonant band-pass (topology-preserving state-variable filter, stable under fast sweeps)
// swept logarithmically from 400 Hz (heel) to 2.2 kHz (toe), with a little low-pass blended back in
// as on the classic inductor circuits. In Auto mode an envelope follower moves it from the playing,
// and the position control sets the sensitivity. The position is smoothed, so a MIDI expression
// pedal (updates at ~100 Hz) sweeps without zipper noise.
class Wah
{
public:
    enum class Mode { pedal, autoWah, count };
    static juce::StringArray modeNames() { return { "Pedal", "Auto" }; }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        smoothing = onePoleCoeff (sampleRate, 12.0f);
        envAttack = onePoleCoeff (sampleRate, 4.0f);
        envRelease = onePoleCoeff (sampleRate, 140.0f);
        reset();
    }

    void reset()
    {
        s1 = s2 = 0.0;
        envelope = 0.0f;
        smoothedPosition = position;
    }

    void setParams (Mode newMode, float newPosition, float newResonance)
    {
        mode = newMode;
        position = juce::jlimit (0.0f, 1.0f, newPosition);
        // Q from a gentle 2.5 to a vocal 9.
        q = 2.5f + 6.5f * juce::jlimit (0.0f, 1.0f, newResonance);
    }

    void process (float* samples, int numSamples) noexcept
    {
        const double k = 1.0 / q;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = samples[i];
            float target = position;
            if (mode == Mode::autoWah)
            {
                const float level = std::abs (x);
                envelope = level + (level > envelope ? envAttack : envRelease) * (envelope - level);
                // POS = sensitivity: at 1 a -30 dBFS pick already reaches the toe.
                target = juce::jlimit (0.0f, 1.0f, envelope * 8.0f * std::pow (10.0f, 1.5f * position));
            }
            smoothedPosition = target + smoothing * (smoothedPosition - target);

            // Coefficient once per 8 samples: the sweep is far slower than that.
            if ((i & 7) == 0)
            {
                const double hz = 400.0 * std::pow (5.5, (double) smoothedPosition);
                g = std::tan (juce::MathConstants<double>::pi * hz / sampleRate);
                a1 = 1.0 / (1.0 + g * (g + k));
            }

            // Zavalishin TPT SVF.
            const double v3 = x - s2;
            const double v1 = a1 * (s1 + g * v3);
            const double v2 = s2 + g * v1;
            s1 = 2.0 * v1 - s1;
            s2 = 2.0 * v2 - s2;
            // v1 (band-pass) peaks at q; scaling by 1/sqrt(q) keeps the loudness similar across RESO.
            samples[i] = (float) (1.2 * v1 / std::sqrt ((double) q) + 0.25 * v2);
        }
    }

private:
    double sampleRate = 48000.0;
    Mode mode = Mode::pedal;
    float position = 0.5f, smoothedPosition = 0.5f, q = 5.0f;
    float smoothing = 0.0f, envAttack = 0.0f, envRelease = 0.0f, envelope = 0.0f;
    double g = 0.1, a1 = 1.0, s1 = 0.0, s2 = 0.0;
};

// Toe-switch behaviour for an expression pedal mapped to the wah position (message thread): moving
// the pedal off the heel switches the wah on; resting at the heel for half a second switches it off,
// but only when the pedal was what switched it on (a wah switched on by hand stays on).
class PedalSwitch
{
public:
    static constexpr float heel = 0.03f;
    static constexpr double offDelayMs = 500.0;
    enum class Change { none, switchOn, switchOff };

    Change pedalMoved (float position, bool wahOn, bool autoEngage, double nowMs)
    {
        if (! autoEngage || wahOn || position <= heel)
            return Change::none;
        engaged = true;
        heelSince = nowMs;
        return Change::switchOn;
    }

    // Called regularly with the current position and on/off state.
    Change tick (float position, bool wahOn, double nowMs)
    {
        if (! engaged || ! wahOn)
        {
            engaged = false;
            return Change::none;
        }
        if (position > heel)
        {
            heelSince = nowMs;
            return Change::none;
        }
        if (nowMs - heelSince < offDelayMs)
            return Change::none;
        engaged = false;
        return Change::switchOff;
    }

private:
    bool engaged = false;
    double heelSince = 0.0;
};
} // namespace poman
