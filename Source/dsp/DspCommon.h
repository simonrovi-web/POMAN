#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace abyssal
{
using ArrayCoeffs = juce::dsp::IIR::ArrayCoefficients<float>;

// Single-channel biquad whose coefficients can be replaced without allocating on the audio thread.
struct Biquad
{
    Biquad() { filter.coefficients = new juce::dsp::IIR::Coefficients<float> (1.0f, 0.0f, 0.0f, 1.0f, 0.0f, 0.0f); }

    void set (const std::array<float, 6>& c) { *filter.coefficients = c; }
    void reset() { filter.reset(); }
    float operator() (float x) noexcept { return filter.processSample (x); }

    juce::dsp::IIR::Filter<float> filter;
};

inline float onePoleCoeff (double sampleRate, float timeMs)
{
    return (float) std::exp (-1.0 / (0.001 * (double) timeMs * sampleRate));
}

inline float dbToGain (float db) { return juce::Decibels::decibelsToGain (db); }

// Knob 0..10 with 5 as flat, mapped to +/- rangeDb.
inline float knobToDb (float knob, float rangeDb) { return (knob - 5.0f) / 5.0f * rangeDb; }
} // namespace abyssal
