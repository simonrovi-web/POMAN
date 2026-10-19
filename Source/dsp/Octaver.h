#pragma once

#include "DspCommon.h"

namespace abyssal
{
// Analog-style octaver: flip-flop divider for the sub octave, full-wave rectifier for the octave up.
class Octaver
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        trackingLowPass1.set (ArrayCoeffs::makeLowPass (sampleRate, 260.0f, 0.707f));
        trackingLowPass2.set (ArrayCoeffs::makeLowPass (sampleRate, 260.0f, 0.707f));
        subSmoothing.set (ArrayCoeffs::makeLowPass (sampleRate, 420.0f, 0.707f));
        upHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 110.0f, 0.707f));
        upLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, 5000.0f, 0.707f));
        envAttack = onePoleCoeff (sampleRate, 1.0f);
        envRelease = onePoleCoeff (sampleRate, 60.0f);
        reset();
    }

    void reset()
    {
        trackingLowPass1.reset();
        trackingLowPass2.reset();
        subSmoothing.reset();
        upHighPass.reset();
        upLowPass.reset();
        envelope = 0.0f;
        flipFlop = 1.0f;
        armed = false;
    }

    void setMix (float sub, float up, float dry)
    {
        subLevel = sub;
        upLevel = up;
        dryLevel = dry;
    }

    void process (float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float input = samples[i];
            const float level = std::abs (input);
            const float envCoeff = level > envelope ? envAttack : envRelease;
            envelope = level + envCoeff * (envelope - level);

            // Divide the fundamental by two: toggle on every positive-going crossing.
            const float tracked = trackingLowPass2 (trackingLowPass1 (input));
            const float hysteresis = 0.08f * envelope + 1.0e-5f;

            if (! armed && tracked < -hysteresis)
                armed = true;
            else if (armed && tracked > hysteresis)
            {
                flipFlop = -flipFlop;
                armed = false;
            }

            const float sub = subSmoothing (flipFlop * envelope) * 1.6f;
            const float up = upLowPass (upHighPass (std::abs (input))) * 1.5f;

            samples[i] = dryLevel * input + subLevel * sub + upLevel * up;
        }
    }

private:
    double sampleRate = 44100.0;
    Biquad trackingLowPass1, trackingLowPass2, subSmoothing, upHighPass, upLowPass;
    float envAttack = 0.0f, envRelease = 0.0f, envelope = 0.0f;
    float flipFlop = 1.0f;
    bool armed = false;
    float subLevel = 0.5f, upLevel = 0.0f, dryLevel = 1.0f;
};
} // namespace abyssal
