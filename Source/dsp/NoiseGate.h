#pragma once

#include "DspCommon.h"
#include <atomic>
#include <vector>

namespace poman
{
// Noise gate in the "detect on the guitar, cut after the amp" layout of loop noise suppressors:
// detect() reads the clean DI (so the threshold means the same as before and amp hiss cannot hold
// it open), apply() attenuates the amp's output, so the amp's own noise is removed as well. The
// gain is delayed to line up with the drive/amp latency, minus a little pre-open, so the gate is
// already open when a pick attack reaches it. Release adapts: an abrupt stop (palm mute, staccato)
// closes quickly, a note left to ring is followed slowly.
class NoiseGate
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        envAttack = onePoleCoeff (sampleRate, 0.3f);
        envRelease = onePoleCoeff (sampleRate, 25.0f);
        fastRelease = onePoleCoeff (sampleRate, 8.0f);
        slowRelease = onePoleCoeff (sampleRate, 60.0f);
        gainAttack = onePoleCoeff (sampleRate, 0.3f);
        gainReleaseFast = onePoleCoeff (sampleRate, 10.0f);
        gainReleaseSlow = onePoleCoeff (sampleRate, 70.0f);
        holdSamples = (int) (0.035 * sampleRate);
        preOpenSamples = (int) (0.001 * sampleRate);
        detectorHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 70.0, 0.707)); // mains hum must not hold it open
        gains.assign ((size_t) gainBufferSize, 0.0f);
        reset();
    }

    void reset()
    {
        envelope = fastEnvelope = slowEnvelope = 0.0f;
        gain = 0.0f;
        holdCounter = 0;
        open = false;
        abrupt = false;
        detectorHighPass.reset();
        std::fill (gains.begin(), gains.end(), 0.0f);
        writePos = 0;
    }

    void setThreshold (float thresholdDb)
    {
        openThreshold = dbToGain (thresholdDb);
        closeThreshold = dbToGain (thresholdDb - 6.0f);
    }

    // Samples between the detection point and where apply() runs (the drive/amp latency).
    void setPathLatency (int samples) { pathLatency.store (juce::jlimit (0, gainBufferSize / 2, samples)); } // any thread

    void detect (const float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float level = std::abs (detectorHighPass (samples[i]));
            envelope = level + (level > envelope ? envAttack : envRelease) * (envelope - level);
            fastEnvelope = level + (level > fastEnvelope ? envAttack : fastRelease) * (fastEnvelope - level);
            slowEnvelope = level + (level > slowEnvelope ? envAttack : slowRelease) * (slowEnvelope - level);

            // A stopped string collapses within a few ms while a fading one decays smoothly: when the
            // fast envelope drops far below the slow one, close at once (no hold) and quickly.
            const bool stopped = fastEnvelope < 0.12f * slowEnvelope;
            // Opening needs a fresh attack, so the tail of a stopped note cannot reopen the gate.
            if (fastEnvelope > openThreshold && fastEnvelope > 0.5f * slowEnvelope)
            {
                open = true;
                abrupt = false;
                holdCounter = holdSamples;
            }
            else if (open && stopped && slowEnvelope > openThreshold)
            {
                open = false;
                abrupt = true;
            }
            else if (envelope < closeThreshold)
            {
                if (holdCounter > 0)
                    --holdCounter;
                else
                    open = false; // a fade: released slowly
            }

            const float target = open ? 1.0f : 0.0f;
            const float coeff = target > gain ? gainAttack : (abrupt ? gainReleaseFast : gainReleaseSlow);
            gain = target + coeff * (gain - target);
            gains[(size_t) ((writePos + i) & gainMask)] = gain;
        }
        writePos = (writePos + numSamples) & gainMask;
    }

    // Applies the gains of the block last passed to detect(), aligned with the path latency.
    void apply (float* samples, int numSamples) const noexcept
    {
        const int delay = juce::jmax (0, pathLatency.load() - preOpenSamples);
        const int first = writePos - numSamples - delay;
        for (int i = 0; i < numSamples; ++i)
            samples[i] *= gains[(size_t) ((first + i) & gainMask)];
    }

private:
    static constexpr int gainBufferSize = 65536, gainMask = gainBufferSize - 1; // > latency + largest block

    double sampleRate = 44100.0;
    float envAttack = 0.0f, envRelease = 0.0f, fastRelease = 0.0f, slowRelease = 0.0f;
    float gainAttack = 0.0f, gainReleaseFast = 0.0f, gainReleaseSlow = 0.0f;
    float openThreshold = 0.001f, closeThreshold = 0.0005f;
    float envelope = 0.0f, fastEnvelope = 0.0f, slowEnvelope = 0.0f, gain = 0.0f;
    int holdSamples = 0, holdCounter = 0, preOpenSamples = 0;
    std::atomic<int> pathLatency { 0 };
    bool open = false, abrupt = false;
    Biquad detectorHighPass;
    std::vector<float> gains;
    int writePos = 0;
};
} // namespace poman
