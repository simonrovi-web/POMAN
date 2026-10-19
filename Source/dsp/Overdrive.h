#pragma once

#include "DspCommon.h"

namespace poman
{
// Mid-hump boost/overdrive: only the high-passed band is clipped and summed back with the clean signal,
// which tightens the low end before the amp.
class Overdrive
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        clipHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 720.0f, 0.6f));
        cleanHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 90.0f, 0.707f));
        lastTone = -1.0f;
        setParams (drive, 0.5f, 0.0f);
        reset();
    }

    void reset()
    {
        clipHighPass.reset();
        cleanHighPass.reset();
        toneLowPass.reset();
    }

    void setParams (float newDrive, float tone, float levelDb)
    {
        drive = newDrive;
        driveGain = 1.0f + drive * drive * 120.0f;
        level = dbToGain (levelDb);

        if (tone != lastTone)
        {
            lastTone = tone;
            const float cutoff = 700.0f * std::pow (10.0f, tone * 1.05f);
            toneLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, cutoff, 0.707f));
        }
    }

    void process (float* samples, int numSamples) noexcept
    {
        constexpr float bias = 0.12f;
        const float biasOffset = std::tanh (bias);

        for (int i = 0; i < numSamples; ++i)
        {
            const float input = samples[i];
            const float clipped = std::tanh (clipHighPass (input) * driveGain + bias) - biasOffset;
            const float mixed = cleanHighPass (input) + 0.35f * clipped;
            samples[i] = toneLowPass (mixed) * level;
        }
    }

private:
    double sampleRate = 44100.0;
    Biquad clipHighPass, cleanHighPass, toneLowPass;
    float drive = 0.3f, driveGain = 1.0f, level = 1.0f, lastTone = -1.0f;
};
} // namespace poman
