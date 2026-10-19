#pragma once

#include "DspCommon.h"

namespace abyssal
{
// Hysteresis gate with hold, tuned for tight palm-muted riffing.
class NoiseGate
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        envAttack = onePoleCoeff (sampleRate, 0.3f);
        envRelease = onePoleCoeff (sampleRate, 25.0f);
        gainAttack = onePoleCoeff (sampleRate, 0.8f);
        gainRelease = onePoleCoeff (sampleRate, 45.0f);
        holdSamples = (int) (0.035 * sampleRate);
        reset();
    }

    void reset()
    {
        envelope = 0.0f;
        gain = 0.0f;
        holdCounter = 0;
        open = false;
    }

    void setThreshold (float thresholdDb)
    {
        openThreshold = dbToGain (thresholdDb);
        closeThreshold = dbToGain (thresholdDb - 6.0f);
    }

    void process (float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float level = std::abs (samples[i]);
            const float envCoeff = level > envelope ? envAttack : envRelease;
            envelope = level + envCoeff * (envelope - level);

            if (envelope > openThreshold)
            {
                open = true;
                holdCounter = holdSamples;
            }
            else if (envelope < closeThreshold)
            {
                if (holdCounter > 0)
                    --holdCounter;
                else
                    open = false;
            }

            const float target = open ? 1.0f : 0.0f;
            const float gainCoeff = target > gain ? gainAttack : gainRelease;
            gain = target + gainCoeff * (gain - target);
            samples[i] *= gain;
        }
    }

private:
    double sampleRate = 44100.0;
    float envAttack = 0.0f, envRelease = 0.0f, gainAttack = 0.0f, gainRelease = 0.0f;
    float openThreshold = 0.001f, closeThreshold = 0.0005f;
    float envelope = 0.0f, gain = 0.0f;
    int holdSamples = 0, holdCounter = 0;
    bool open = false;
};
} // namespace abyssal
