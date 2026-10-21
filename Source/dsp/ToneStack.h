#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <complex>

namespace poman
{
// Passive treble/mid/bass tone stack of guitar amps, modelled from its circuit: the third-order
// transfer function of D. T. Yeh and J. O. Smith, "Discretization of the '59 Fender Bassman Tone
// Stack" (DAFx 2006), discretised with the bilinear transform. Unlike three separate shelving
// filters, the controls interact and the stack scoops the mids exactly as the real circuit does.
class ToneStack
{
public:
    // R1 treble pot, R2 bass pot, R3 mid pot, R4 slope resistor; C1 treble cap, C2/C3 bass/mid caps.
    struct Components { double r1, r2, r3, r4, c1, c2, c3; };

    static constexpr Components fender   { 250e3, 1e6, 25e3, 56e3, 250e-12, 20e-9, 20e-9 }; // '59 Bassman
    static constexpr Components marshall { 220e3, 1e6, 22e3, 33e3, 470e-12, 22e-9, 22e-9 }; // JMP/JCM-style

    // Analog H(s) = (b1 s + b2 s^2 + b3 s^3) / (1 + a1 s + a2 s^2 + a3 s^3) for pot positions 0..1.
    struct Analog { double b1, b2, b3, a1, a2, a3; };

    // Knobs 0..10 to pot positions. The bass pot is audio taper, as in the amps.
    static double treblePosition (float knob) { return juce::jlimit (0.0, 1.0, knob / 10.0); }
    static double midPosition (float knob)    { return juce::jlimit (0.0, 1.0, knob / 10.0); }
    static double bassPosition (float knob)   { return std::exp ((juce::jlimit (0.0, 1.0, knob / 10.0) - 1.0) * 3.4); }

    static Analog analog (const Components& c, double t, double m, double l)
    {
        const double R1 = c.r1, R2 = c.r2, R3 = c.r3, R4 = c.r4, C1 = c.c1, C2 = c.c2, C3 = c.c3;
        Analog a;
        a.b1 = t * C1 * R1 + m * C3 * R3 + l * (C1 * R2 + C2 * R2) + (C1 * R3 + C2 * R3);
        a.b2 = t * (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + m * (C1 * C3 * R1 * R3 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + l * (C1 * C2 * R1 * R2 + C1 * C2 * R2 * R4 + C1 * C3 * R2 * R4)
             + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3)
             + (C1 * C2 * R1 * R3 + C1 * C2 * R3 * R4 + C1 * C3 * R3 * R4);
        a.b3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
             - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
             + m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
             + t * C1 * C2 * C3 * R1 * R3 * R4 - t * m * C1 * C2 * C3 * R1 * R3 * R4
             + t * l * C1 * C2 * C3 * R1 * R2 * R4;
        a.a1 = (C1 * R1 + C1 * R3 + C2 * R3 + C2 * R4 + C3 * R4) + m * C3 * R3 + l * (C1 * R2 + C2 * R2);
        a.a2 = m * (C1 * C3 * R1 * R3 - C2 * C3 * R3 * R4 + C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + l * m * (C1 * C3 * R2 * R3 + C2 * C3 * R2 * R3) - m * m * (C1 * C3 * R3 * R3 + C2 * C3 * R3 * R3)
             + l * (C1 * C2 * R2 * R4 + C1 * C2 * R1 * R2 + C1 * C3 * R2 * R4 + C2 * C3 * R2 * R4)
             + (C1 * C2 * R1 * R4 + C1 * C3 * R1 * R4 + C1 * C2 * R3 * R4 + C1 * C2 * R1 * R3 + C1 * C3 * R3 * R4 + C2 * C3 * R3 * R4);
        a.a3 = l * m * (C1 * C2 * C3 * R1 * R2 * R3 + C1 * C2 * C3 * R2 * R3 * R4)
             - m * m * (C1 * C2 * C3 * R1 * R3 * R3 + C1 * C2 * C3 * R3 * R3 * R4)
             + m * (C1 * C2 * C3 * R3 * R3 * R4 + C1 * C2 * C3 * R1 * R3 * R3 - C1 * C2 * C3 * R1 * R3 * R4)
             + l * C1 * C2 * C3 * R1 * R2 * R4 + C1 * C2 * C3 * R1 * R3 * R4;
        return a;
    }

    static double analogMagnitude (const Analog& a, double hz)
    {
        const std::complex<double> s (0.0, juce::MathConstants<double>::twoPi * hz);
        return std::abs ((a.b1 * s + a.b2 * s * s + a.b3 * s * s * s) / (1.0 + a.a1 * s + a.a2 * s * s + a.a3 * s * s * s));
    }

    // Digital coefficients (b0..b3, a0..a3) via the bilinear transform, c = 2 fs.
    using Digital = std::array<double, 8>;
    static Digital discretise (const Analog& a, double sampleRate)
    {
        const double c = 2.0 * sampleRate, c2 = c * c, c3 = c2 * c;
        return { -a.b1 * c - a.b2 * c2 - a.b3 * c3,
                 -a.b1 * c + a.b2 * c2 + 3.0 * a.b3 * c3,
                  a.b1 * c + a.b2 * c2 - 3.0 * a.b3 * c3,
                  a.b1 * c - a.b2 * c2 + a.b3 * c3,
                 -1.0 - a.a1 * c - a.a2 * c2 - a.a3 * c3,
                 -3.0 - a.a1 * c + a.a2 * c2 + 3.0 * a.a3 * c3,
                 -3.0 + a.a1 * c + a.a2 * c2 - 3.0 * a.a3 * c3,
                 -1.0 + a.a1 * c - a.a2 * c2 + a.a3 * c3 };
    }

    static double digitalMagnitude (const Digital& d, double hz, double sampleRate)
    {
        const auto z1 = std::polar (1.0, -juce::MathConstants<double>::twoPi * hz / sampleRate);
        const auto z2 = z1 * z1, z3 = z2 * z1;
        return std::abs ((d[0] + d[1] * z1 + d[2] * z2 + d[3] * z3) / (d[4] + d[5] * z1 + d[6] * z2 + d[7] * z3));
    }

    // Loss of the stack with every knob at noon, measured at its loudest point: dividing by it keeps
    // the amp's level where it was voiced while the knobs still change level as on a real amp.
    static double noonPeakGain (const Components& c)
    {
        const auto a = analog (c, treblePosition (5.0f), midPosition (5.0f), bassPosition (5.0f));
        double peak = 0.0;
        for (double hz = 40.0; hz < 12000.0; hz *= 1.05)
            peak = std::max (peak, analogMagnitude (a, hz));
        return peak;
    }

    void setComponents (const Components& c)
    {
        components = c;
        makeUp = std::pow (10.0, 0.9 / 20.0) / noonPeakGain (c); // +0.9 dB keeps the amps' previous level
        designed = false;
    }

    void setKnobs (double sampleRate, float treble, float mid, float bass)
    {
        if (designed && treble == lastTreble && mid == lastMid && bass == lastBass)
            return;
        designed = true;
        lastTreble = treble;
        lastMid = mid;
        lastBass = bass;
        const auto d = discretise (analog (components, treblePosition (treble), midPosition (mid), bassPosition (bass)), sampleRate);
        const double inv = 1.0 / d[4];
        for (int i = 0; i < 4; ++i)
            b[(size_t) i] = d[(size_t) i] * inv * makeUp;
        for (int i = 1; i < 4; ++i)
            a[(size_t) i] = d[(size_t) (4 + i)] * inv;
    }

    void reset() { z = {}; }

    // Transposed direct form II, double precision (poles sit close to the unit circle when oversampled).
    float operator() (float input) noexcept
    {
        const double x = input;
        const double y = b[0] * x + z[0];
        z[0] = b[1] * x - a[1] * y + z[1];
        z[1] = b[2] * x - a[2] * y + z[2];
        z[2] = b[3] * x - a[3] * y;
        return (float) y;
    }

private:
    Components components = fender;
    double makeUp = 1.0;
    std::array<double, 4> b { 1.0, 0.0, 0.0, 0.0 }, a { 1.0, 0.0, 0.0, 0.0 };
    std::array<double, 3> z {};
    float lastTreble = -1.0f, lastMid = -1.0f, lastBass = -1.0f;
    bool designed = false;
};
} // namespace poman
