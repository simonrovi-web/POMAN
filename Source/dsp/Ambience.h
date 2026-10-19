#pragma once

#include "DspCommon.h"

namespace poman
{
// Ping-pong delay with darkened, thinned repeats.
class PingPongDelay
{
public:
    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        line.setMaximumDelayInSamples ((int) (sampleRate * 2.1));
        line.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
        delayMs.reset (sampleRate, 0.12);

        for (auto& f : repeatLowPass)
            f.set (ArrayCoeffs::makeLowPass (sampleRate, 4200.0f, 0.707f));
        for (auto& f : repeatHighPass)
            f.set (ArrayCoeffs::makeHighPass (sampleRate, 140.0f, 0.707f));

        reset();
    }

    void reset()
    {
        line.reset();
        for (auto& f : repeatLowPass)
            f.reset();
        for (auto& f : repeatHighPass)
            f.reset();
    }

    void setParams (float timeMs, float newFeedback, float newMix)
    {
        delayMs.setTargetValue (timeMs);
        feedback = newFeedback;
        mix = newMix;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float delaySamples = delayMs.getNextValue() * 0.001f * (float) sampleRate;
            const float wetLeft = line.popSample (0, delaySamples);
            const float wetRight = line.popSample (1, delaySamples);

            const float input = 0.5f * (left[i] + right[i]);
            line.pushSample (0, input + feedback * repeatLowPass[0] (repeatHighPass[0] (wetRight)));
            line.pushSample (1, feedback * repeatLowPass[1] (repeatHighPass[1] (wetLeft)));

            left[i] += mix * wetLeft;
            right[i] += mix * wetRight;
        }
    }

private:
    double sampleRate = 44100.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line;
    juce::SmoothedValue<float> delayMs { 380.0f };
    std::array<Biquad, 2> repeatLowPass, repeatHighPass;
    float feedback = 0.3f, mix = 0.25f;
};

class Ambience
{
public:
    void prepare (double sampleRate, int maxBlockSize)
    {
        reverb.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
        reverb.reset();
    }

    void reset() { reverb.reset(); }

    void setParams (float size, float mix)
    {
        juce::dsp::Reverb::Parameters p;
        p.roomSize = 0.35f + 0.6f * size;
        p.damping = 0.55f;
        p.width = 1.0f;
        p.wetLevel = mix * 0.6f;
        p.dryLevel = 1.0f - 0.35f * mix;
        reverb.setParameters (p);
    }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        reverb.process (juce::dsp::ProcessContextReplacing<float> (block));
    }

private:
    juce::dsp::Reverb reverb;
};
} // namespace poman
