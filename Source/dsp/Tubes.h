#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>
#include <vector>

// Vacuum tubes from Norman Koren's equations ("Improved VT models for SPICE simulations", 1996/2003).
// Solving a stage's load line every sample would be costly, so each transfer curve is solved once
// (bisection on the plate voltage, grid current included) into a table that the audio thread
// interpolates.
namespace poman::tubes
{
struct TubeParams { double mu, ex, kg1, kp, kvb; };

// 12AX7 triode (Koren's published fit).
inline constexpr TubeParams ecc83 { 100.0, 1.4, 1060.0, 600.0, 300.0 };

// log(1 + e^x) without overflow.
inline double softplus (double x) noexcept { return x > 30.0 ? x : std::log1p (std::exp (x)); }

// Triode plate current (A) for grid-cathode and plate-cathode voltages.
inline double triodeCurrent (const TubeParams& t, double vgk, double vpk) noexcept
{
    if (vpk <= 0.0)
        return 0.0;
    const double e1 = vpk / t.kp * softplus (t.kp * (1.0 / t.mu + vgk / std::sqrt (t.kvb + vpk * vpk)));
    return e1 > 0.0 ? 2.0 * std::pow (e1, t.ex) / t.kg1 : 0.0;
}

// Pentode plate current (A) with a fixed screen voltage, above the knee (plate voltage high).
inline double pentodeCurrent (const TubeParams& t, double vg1, double vg2) noexcept
{
    const double e1 = vg2 / t.kp * softplus (t.kp * (1.0 / t.mu + vg1 / vg2));
    return e1 > 0.0 ? 2.0 * std::pow (e1, t.ex) / t.kg1 : 0.0;
}

// Linear-interpolated table of y = f(x) on [xMin, xMax], clamped outside.
class TransferTable
{
public:
    template <typename Fn>
    void build (double newMin, double newMax, int points, Fn&& f)
    {
        xMin = newMin;
        step = (newMax - newMin) / (points - 1);
        invStep = 1.0 / step;
        y.resize ((size_t) points);
        for (int i = 0; i < points; ++i)
            y[(size_t) i] = (float) f (xMin + i * step);
    }

    float operator() (float x) const noexcept
    {
        const double pos = juce::jlimit (0.0, (double) (y.size() - 1) - 1.0e-9, ((double) x - xMin) * invStep);
        const auto i = (size_t) pos;
        const float frac = (float) (pos - (double) i);
        return y[i] + frac * (y[i + 1] - y[i]);
    }

    // Largest |dy/dx| between table points.
    double maxSlope() const
    {
        double m = 0.0;
        for (size_t i = 1; i < y.size(); ++i)
            m = std::max (m, std::abs ((double) y[i] - y[i - 1]) * invStep);
        return m;
    }

    void scale (double k)
    {
        for (auto& v : y)
            v = (float) (v * k);
    }

private:
    double xMin = 0.0, step = 1.0, invStep = 1.0;
    std::vector<float> y { 0.0f, 0.0f };
};

//==============================================================================
// Preamp stage: common-cathode 12AX7, 100 k plate load on a 300 V supply, driven through 68 k of
// source resistance. Above 0 V the grid conducts (Langmuir-Child, I = k Vg^1.5), so the grid voltage
// is solved from Vin = Vg + Rs Ig(Vg) first. The table maps the input grid voltage to plate voltage.
struct PreampTriode
{
    static constexpr double supply = 300.0, plateLoad = 100.0e3, sourceResistance = 68.0e3, gridK = 1.0e-4;
    static constexpr double vinMin = -12.0, vinMax = 8.0;

    static double gridVoltage (double vin)
    {
        if (vin <= 0.0)
            return vin;
        double lo = 0.0, hi = vin;
        for (int it = 0; it < 60; ++it)
        {
            const double vg = 0.5 * (lo + hi);
            (vg + sourceResistance * gridK * std::pow (vg, 1.5) > vin ? hi : lo) = vg;
        }
        return 0.5 * (lo + hi);
    }

    static double plateVoltage (double vgk)
    {
        // Vp = B+ - R Ip(Vg, Vp): the right side falls as Vp rises, so bisection converges.
        double lo = 0.0, hi = supply;
        for (int it = 0; it < 60; ++it)
        {
            const double vp = 0.5 * (lo + hi);
            (supply - plateLoad * triodeCurrent (ecc83, vgk, vp) > vp ? lo : hi) = vp;
        }
        return 0.5 * (lo + hi);
    }

    static const TransferTable& table()
    {
        static const TransferTable t = []
        {
            TransferTable tt;
            tt.build (vinMin, vinMax, 4097, [] (double vin) { return plateVoltage (gridVoltage (vin)); });
            return tt;
        }();
        return t;
    }

    // Grid bias for a model's "bias" amount: higher values sit colder, nearer cutoff (cold-clipper
    // high-gain stages); low values sit warm, nearer grid conduction.
    static float operatingPoint (float bias) noexcept { return -0.9f - 4.0f * bias; }
    static constexpr float voltsPerUnit = 1.0f; // stage input scaling: about where clipping starts
};

//==============================================================================
// Power stage: a push-pull pair of pentodes. Each side draws Ip(Vb ± drive); the transformer outputs
// the difference. Class AB idles near cutoff (some crossover at low level), class A at mid-current.
// Past 0 V the grid conducts and the plate runs into its knee, both modelled as a soft current limit.
enum class PowerTube { el34, sixL6, el84, sixV6, count };

struct PentodeSpec { TubeParams params; double screen; bool classA; };

inline const PentodeSpec& pentodeSpec (PowerTube tube)
{
    static const std::array<PentodeSpec, (size_t) PowerTube::count> specs { {
        { { 11.0, 1.35, 650.0, 60.0, 24.0 }, 400.0, false },   // EL34: crunchy, mid-forward, breaks up earlier
        { { 8.7, 1.35, 1460.0, 48.0, 12.0 }, 420.0, false },   // 6L6: big clean headroom, firm lows
        { { 19.3, 1.35, 1180.0, 200.0, 16.0 }, 280.0, true },  // EL84: cathode-biased class A, early, chimey compression
        { { 10.7, 1.31, 1672.0, 41.0, 12.0 }, 320.0, true },   // 6V6: small class-A tweed, soft and sagging
    } };
    return specs[(size_t) tube];
}

class PushPull
{
public:
    static constexpr float driveRange = 4.0f;

    static const TransferTable& table (PowerTube tube)
    {
        static const std::array<TransferTable, (size_t) PowerTube::count> tables = []
        {
            std::array<TransferTable, (size_t) PowerTube::count> all;
            for (int k = 0; k < (int) PowerTube::count; ++k)
                all[(size_t) k] = build ((PowerTube) k);
            return all;
        }();
        return tables[(size_t) tube];
    }

private:
    static TransferTable build (PowerTube tube)
    {
        const auto& spec = pentodeSpec (tube);
        const auto& p = spec.params;
        const double cutoff = -spec.screen / p.mu;              // grid voltage where the current stops
        const double bias = cutoff * (spec.classA ? 0.5 : 0.93); // idle point: AB idles near cutoff (low current)
        const double swing = -bias;                              // drive 1.0 takes one grid to 0 V
        const double iZero = pentodeCurrent (p, 0.0, spec.screen);
        const double slopeZero = (iZero - pentodeCurrent (p, -0.5, spec.screen)) / 0.5;
        const auto side = [&] (double vg)
        {
            // Grid conduction above 0 V: the current barely rises any more.
            return vg <= 0.0 ? pentodeCurrent (p, vg, spec.screen) : iZero + 0.1 * slopeZero * vg;
        };
        const double limit = 1.1 * iZero; // plate knee: the supply cannot pull more than this
        TransferTable t;
        t.build (-driveRange, driveRange, 4097, [&] (double e)
        {
            const double diff = side (bias + swing * e) - side (bias - swing * e);
            return limit * std::tanh (diff / limit);
        });
        t.scale (1.0 / t.maxSlope()); // slope <= 1 everywhere keeps the feedback loop stable
        return t;
    }
};
} // namespace poman::tubes
