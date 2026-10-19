#pragma once

#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace poman
{
// Note values for the tempo-synced delay. beats = length in quarter notes; 0 = free time.
struct DelayDivision
{
    const char* name;
    double beats;
};

inline const std::array<DelayDivision, 8>& delayDivisions()
{
    static const std::array<DelayDivision, 8> divisions { {
        { "Free", 0.0 }, { "1/2", 2.0 }, { "1/4.", 1.5 }, { "1/4", 1.0 },
        { "1/8.", 0.75 }, { "1/8", 0.5 }, { "1/8T", 1.0 / 3.0 }, { "1/16", 0.25 },
    } };
    return divisions;
}

inline juce::StringArray delayDivisionNames()
{
    juce::StringArray names;
    for (const auto& d : delayDivisions())
        names.add (d.name);
    return names;
}

constexpr float maxDelayMs = 2000.0f; // the delay line holds 2.1 s

// Delay time for a division at `bpm`; division 0 returns the free time.
inline float syncedDelayMs (double bpm, int division, float freeMs)
{
    const auto& divisions = delayDivisions();
    const int d = juce::jlimit (0, (int) divisions.size() - 1, division);
    if (divisions[(size_t) d].beats <= 0.0 || bpm <= 0.0)
        return freeMs;
    return (float) juce::jmin ((double) maxDelayMs, 60000.0 / bpm * divisions[(size_t) d].beats);
}

// Averages the last few tap intervals. Taps more than 2 s apart start a new measurement.
class TapTempo
{
public:
    // Returns the tempo once two taps are in, otherwise 0.
    double tap (double timeSeconds)
    {
        if (lastTap >= 0.0)
        {
            const double interval = timeSeconds - lastTap;
            if (interval > 2.0 || interval < 0.15)
                count = 0;
            else
            {
                intervals[(size_t) (next++ % intervals.size())] = interval;
                count = juce::jmin (count + 1, (int) intervals.size());
            }
        }
        lastTap = timeSeconds;

        if (count == 0)
            return 0.0;
        double sum = 0.0;
        for (int i = 0; i < count; ++i)
            sum += intervals[(size_t) ((next - 1 - i + (int) intervals.size() * 4) % (int) intervals.size())];
        return juce::jlimit (40.0, 240.0, 60.0 / (sum / count));
    }

private:
    std::array<double, 4> intervals {};
    int next = 0, count = 0;
    double lastTap = -1.0;
};

// Input gain (dB) that brings a guitar's raw DI peak to the level the amp models are voiced for.
inline float calibrationGainDb (float rawPeakDb, float targetPeakDb = -9.0f)
{
    return juce::jlimit (-24.0f, 24.0f, std::round ((targetPeakDb - rawPeakDb) * 2.0f) / 2.0f);
}
} // namespace poman
