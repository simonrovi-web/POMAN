#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include <cmath>

namespace poman
{
// Coefficients are designed in double precision: at oversampled rates, low cutoffs put the poles
// within ~1e-4 of the unit circle, where float coefficients and state add audible noise.
using ArrayCoeffs = juce::dsp::IIR::ArrayCoefficients<double>;
using BiquadCoeffs = std::array<double, 6>; // b0, b1, b2, a0, a1, a2

// Single-channel biquad (transposed direct form II, double-precision coefficients and state).
// set() never allocates, so coefficients can change on the audio thread.
struct Biquad
{
    void set (const BiquadCoeffs& c) noexcept
    {
        const double inv = 1.0 / c[3];
        b0 = c[0] * inv; b1 = c[1] * inv; b2 = c[2] * inv;
        a1 = c[4] * inv; a2 = c[5] * inv;
    }

    void reset() noexcept { z1 = z2 = 0.0; }

    float operator() (float input) noexcept
    {
        const double x = input;
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return (float) y;
    }

private:
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0, z1 = 0.0, z2 = 0.0;
};

// tanh with first-order antiderivative anti-aliasing (ADAA): outputs the average of tanh between
// consecutive inputs, (F(x) - F(x_prev)) / (x - x_prev) with F = log(cosh). Harmonics then fall as
// 1/n^2 instead of 1/n, which keeps very high gain clipping (near square waves) from aliasing.
struct AdaaTanh
{
    float operator() (float x) noexcept
    {
        const double dx = (double) x - prev;
        const double y = std::abs (dx) > 1.0e-5 ? (logCosh (x) - logCosh (prev)) / dx
                                                : std::tanh (0.5 * ((double) x + prev));
        prev = x;
        return (float) y;
    }

    void reset() noexcept { prev = 0.0; }

private:
    static double logCosh (double x) noexcept
    {
        const double a = std::abs (x);
        return a + std::log1p (std::exp (-2.0 * a)) - 0.69314718055994530942; // stable for any |x|
    }

    double prev = 0.0;
};

inline float onePoleCoeff (double sampleRate, float timeMs)
{
    return (float) std::exp (-1.0 / (0.001 * (double) timeMs * sampleRate));
}

inline float dbToGain (float db) { return juce::Decibels::decibelsToGain (db); }

// Knob 0..10 with 5 as flat, mapped to +/- rangeDb.
inline float knobToDb (float knob, float rangeDb) { return (knob - 5.0f) / 5.0f * rangeDb; }
} // namespace poman
