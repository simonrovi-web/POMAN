#pragma once

#include "DspCommon.h"
#include <atomic>

namespace poman
{
// Guitar compressor pedal: feed-forward peak detector, 6 dB soft knee and automatic make-up.
// SUSTAIN sets threshold and ratio together (like a one-knob pedal), ATTACK lets more pick through,
// BLEND mixes the dry signal back in (parallel compression).
class Compressor
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        output.reset (sampleRate, 0.05);
        envelope = 0.0f;
        setParams (sustain, attack, 0.0f, blend);
    }

    // sustain, attack and blend are 0..1; levelDb is the output trim.
    void setParams (float newSustain, float newAttack, float levelDb, float newBlend)
    {
        sustain = newSustain;
        attack = newAttack;
        blend = newBlend;
        thresholdDb = -10.0f - 30.0f * sustain;   // -10 .. -40 dBFS
        ratio = 2.0f + 8.0f * sustain;            //   2 .. 10
        makeUpDb = -thresholdDb * (1.0f - 1.0f / ratio) * 0.5f;
        attackCoeff = onePoleCoeff (sampleRate, 1.0f + 49.0f * attack * attack); // 1 .. 50 ms
        releaseCoeff = onePoleCoeff (sampleRate, 120.0f + 200.0f * sustain);
        output.setTargetValue (dbToGain (levelDb));
    }

    // Static curve: gain change in dB for a detector level in dB.
    float gainChangeDb (float levelDb) const noexcept
    {
        constexpr float knee = 6.0f;
        const float over = levelDb - thresholdDb;
        const float slope = 1.0f / ratio - 1.0f;
        if (over <= -0.5f * knee)
            return 0.0f;
        if (over < 0.5f * knee)
            return slope * (over + 0.5f * knee) * (over + 0.5f * knee) / (2.0f * knee);
        return slope * over;
    }

    float getMakeUpDb() const { return makeUpDb; }

    void process (float* samples, int numSamples) noexcept
    {
        float deepest = 0.0f;
        for (int i = 0; i < numSamples; ++i)
        {
            const float x = samples[i];
            const float level = std::abs (x);
            envelope = level + (level > envelope ? attackCoeff : releaseCoeff) * (envelope - level);

            const float change = gainChangeDb (juce::Decibels::gainToDecibels (envelope, -120.0f));
            deepest = juce::jmin (deepest, change);
            const float wet = x * dbToGain (change + makeUpDb);
            samples[i] = (blend * wet + (1.0f - blend) * x) * output.getNextValue();
        }
        gainReduction.store (deepest);
    }

    // Deepest gain reduction (dB, <= 0) in the last block, for the UI meter.
    float getGainReductionDb() const { return gainReduction.load(); }

private:
    double sampleRate = 48000.0;
    float sustain = 0.5f, attack = 0.3f, blend = 1.0f;
    float thresholdDb = -25.0f, ratio = 6.0f, makeUpDb = 0.0f;
    float attackCoeff = 0.0f, releaseCoeff = 0.0f, envelope = 0.0f;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> output { 1.0f };
    std::atomic<float> gainReduction { 0.0f };
};
} // namespace poman
