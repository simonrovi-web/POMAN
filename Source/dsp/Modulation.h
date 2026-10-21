#pragma once

#include "DspCommon.h"
#include "PitchShift.h"

namespace poman
{
// Stereo modulation pedal: chorus, flanger, tremolo or detune.
// Controls: rate, depth, mix and feedback. For the tremolo, "mix" sets the wave shape
// (sine to square) and "feedback" the stereo phase offset (0 = mono pulse, 1 = auto-pan). For the detune,
// "depth" sets the detune (up to 15 cents, left down and right up), "mix" the blend and "rate" is unused.
class Modulation
{
public:
    enum class Type { chorus, flanger, tremolo, detune, count };

    static juce::StringArray typeNames() { return { "Chorus", "Flanger", "Tremolo", "Detune" }; }

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        line.setMaximumDelayInSamples ((int) (maxDelaySeconds * sampleRate) + 4);
        line.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
        for (auto& s : detuners)
        {
            s.prepare (sampleRate, 0.04f);
            s.setCrossfade (0.1f);
        }
        depth.reset (sampleRate, 0.05);
        mix.reset (sampleRate, 0.05);
        reset();
    }

    void reset()
    {
        line.reset();
        feedbackState = { 0.0f, 0.0f };
        phase = 0.0;
        for (auto& s : detuners)
            s.reset();
    }

    void setParams (Type newType, float rateHz, float newDepth, float newMix, float newFeedback)
    {
        if (newType != type)
        {
            type = newType;
            line.reset();
            feedbackState = { 0.0f, 0.0f };
        }
        rate = rateHz;
        depth.setTargetValue (newDepth);
        mix.setTargetValue (newMix);
        feedback = newFeedback;
    }

    void process (float* left, float* right, int numSamples) noexcept
    {
        float* channels[] { left, right };
        const double increment = rate / sampleRate;

        if (type == Type::detune)
        {
            // Two micro pitch shifters, one a little flat (left) and one a little sharp (right): the
            // doubled-take width of a studio detune, without a chorus's sweep. Delays differ slightly too.
            for (int i = 0; i < numSamples; ++i)
            {
                const float cents = 15.0f * depth.getNextValue();
                const float m = mix.getNextValue();
                for (int ch = 0; ch < 2; ++ch)
                {
                    const float ratio = std::exp2 ((ch == 0 ? -cents : cents) / 1200.0f);
                    const float x = channels[ch][i];
                    channels[ch][i] = (1.0f - 0.5f * m) * x + m * detuners[(size_t) ch].process (x, ratio);
                }
            }
            return;
        }

        if (type == Type::tremolo)
        {
            for (int i = 0; i < numSamples; ++i)
            {
                const float d = depth.getNextValue();
                const float hardness = 1.0f + 8.0f * mix.getNextValue();
                const float norm = std::tanh (hardness);
                for (int ch = 0; ch < 2; ++ch)
                {
                    const double p = phase + 0.5 * feedback * ch; // up to 180 degrees between sides
                    const float wave = std::tanh (hardness * (float) std::sin (juce::MathConstants<double>::twoPi * p)) / norm;
                    channels[ch][i] *= 1.0f - d * (0.5f + 0.5f * wave);
                }
                advance (increment);
            }
            return;
        }

        const bool chorus = type == Type::chorus;
        // Chorus: 7-21 ms sine sweep, light regen. Flanger: 0.3-4.3 ms triangle sweep, strong regen.
        const double stereoOffset = chorus ? 0.25 : 0.125;
        const float regen = chorus ? feedback * 0.35f : feedback * 0.9f;

        for (int i = 0; i < numSamples; ++i)
        {
            const float d = depth.getNextValue();
            const float m = mix.getNextValue();
            for (int ch = 0; ch < 2; ++ch)
            {
                const double p = phase + stereoOffset * ch;
                double delaySeconds;
                if (chorus)
                {
                    delaySeconds = 0.014 + 0.007 * d * std::sin (juce::MathConstants<double>::twoPi * p);
                }
                else
                {
                    const double frac = p - std::floor (p);
                    const double triangle = 1.0 - 2.0 * std::abs (2.0 * frac - 1.0); // -1..1
                    delaySeconds = 0.0003 + 0.004 * d * (0.5 + 0.5 * triangle);
                }

                const float x = channels[ch][i];
                line.pushSample (ch, x + regen * feedbackState[(size_t) ch]);
                const float wet = line.popSample (ch, (float) (delaySeconds * sampleRate));
                feedbackState[(size_t) ch] = std::tanh (wet); // soft limit keeps high regen stable
                channels[ch][i] = (1.0f - m) * x + m * wet;
            }
            advance (increment);
        }
    }

private:
    void advance (double increment) noexcept
    {
        phase += increment;
        phase -= std::floor (phase);
    }

    static constexpr double maxDelaySeconds = 0.03;

    double sampleRate = 48000.0, phase = 0.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Lagrange3rd> line;
    Type type = Type::chorus;
    float rate = 0.8f, feedback = 0.3f;
    juce::SmoothedValue<float> depth { 0.5f }, mix { 0.5f };
    std::array<float, 2> feedbackState { 0.0f, 0.0f };
    std::array<PitchShifter, 2> detuners;
};
} // namespace poman
