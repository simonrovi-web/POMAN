#pragma once

#include "DspCommon.h"
#include "../PitchDetect.h"
#include <vector>

namespace poman
{
// Delay-line pitch shifter: two read taps sweep through a window at a speed set by the pitch ratio,
// with Hann gains offset by half a window that sum to one and hide each tap's wrap-around. Cheap and
// latency-light; best on single notes, like the classic pitch pedals. A window that spans a whole
// number of the input's periods (setWindow) makes each splice land in phase, so it does not warble.
class PitchShifter
{
public:
    void prepare (double sampleRate, float windowSeconds, float maxWindowSeconds = 0.0f)
    {
        window = targetWindow = (float) (windowSeconds * sampleRate);
        windowGlide = onePoleCoeff (sampleRate, 30.0f);
        const float longest = (float) (juce::jmax (windowSeconds, maxWindowSeconds) * sampleRate);
        int size = 1;
        while (size < (int) (longest * 2.0f) + 8)
            size <<= 1;
        buffer.assign ((size_t) size, 0.0f);
        mask = size - 1;
        reset();
    }

    void reset()
    {
        std::fill (buffer.begin(), buffer.end(), 0.0f);
        writeIndex = 0;
        phase = 0.0f;
    }

    void setWindow (float samples) noexcept { targetWindow = samples; }

    // 0 (default): Hann gains, the two taps always overlap (smooth for big shifts). 0 < f <= 1: a
    // trapezoid where each tap plays alone and they only cross for a fraction f of the cycle, so tiny
    // shifts (detune) stay exactly in pitch between the rare splices.
    void setCrossfade (float fraction) noexcept { crossfade = juce::jlimit (0.0f, 1.0f, fraction); }

    float process (float x, float ratio) noexcept
    {
        buffer[(size_t) writeIndex] = x;
        window = targetWindow + windowGlide * (window - targetWindow);
        phase += (1.0f - ratio) / window;
        phase -= std::floor (phase);
        float phaseB = phase + 0.5f;
        phaseB -= std::floor (phaseB);

        const float out = gain (phase) * read (minDelay + phase * window) + gain (phaseB) * read (minDelay + phaseB * window);
        writeIndex = (writeIndex + 1) & mask;
        return out;
    }

private:
    // Gains of the taps at phase p and p + 0.5 always sum to one, and are zero where a tap wraps.
    float gain (float p) const noexcept
    {
        if (crossfade <= 0.0f)
            return 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * p);
        return juce::jlimit (0.0f, 1.0f, (0.25f + 0.5f * crossfade - std::abs (p - 0.5f)) / crossfade);
    }

    float read (float delay) const noexcept
    {
        float pos = (float) writeIndex - delay;
        if (pos < 0.0f)
            pos += (float) buffer.size();
        const int i0 = (int) pos;
        const float frac = pos - (float) i0;
        return buffer[(size_t) i0] + frac * (buffer[(size_t) ((i0 + 1) & mask)] - buffer[(size_t) i0]);
    }

    static constexpr float minDelay = 2.0f;
    std::vector<float> buffer { 0.0f, 0.0f };
    int writeIndex = 0, mask = 1;
    float window = 1000.0f, targetWindow = 1000.0f, windowGlide = 0.0f, phase = 0.0f, crossfade = 0.0f;
};

// Whammy-style pitch pedal: the position (an expression pedal) bends from no shift at the heel to the
// mode's interval at the toe. MIX below 1 adds the dry note for harmonies. At the heel the dry signal
// is passed through, so a resting pedal colours nothing.
class Whammy
{
public:
    static juce::StringArray modeNames() { return { "+1 Oct", "+2 Oct", "-1 Oct", "-2 Oct", "+5th", "-4th" }; }
    static float modeSemitones (int mode)
    {
        constexpr float semis[] { 12.0f, 24.0f, -12.0f, -24.0f, 7.0f, -5.0f };
        return semis[juce::jlimit (0, 5, mode)];
    }

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        shifter.prepare (sampleRate, defaultWindowSeconds, maxWindowSeconds);
        smoothing = onePoleCoeff (sampleRate, 15.0f);
        decimation = juce::jmax (1, juce::roundToInt (sampleRate / 12000.0));
        trackRing.assign ((size_t) trackLength, 0.0f);
        trackScratch.assign ((size_t) trackLength, 0.0f);
        detector.reserve (trackLength);
        reset();
    }

    void reset()
    {
        shifter.reset();
        smoothedSemis = targetSemis;
        std::fill (trackRing.begin(), trackRing.end(), 0.0f);
        ringPos = accumCount = sinceDetect = 0;
        accum = 0.0f;
    }

    void setParams (int mode, float position, float newMix)
    {
        targetSemis = modeSemitones (mode) * juce::jlimit (0.0f, 1.0f, position);
        mix = juce::jlimit (0.0f, 1.0f, newMix);
    }

    void process (float* samples, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            smoothedSemis = targetSemis + smoothing * (smoothedSemis - targetSemis);
            const float x = samples[i];
            track (x);
            const float shifted = shifter.process (x, std::exp2 (smoothedSemis / 12.0f));
            // Within half a semitone of the heel, fade to the plain signal (a shifter at ratio 1 combs).
            const float engaged = juce::jlimit (0.0f, 1.0f, std::abs (smoothedSemis) * 2.0f);
            const float wet = engaged * shifted + (1.0f - engaged) * x;
            samples[i] = mix * wet + (1.0f - mix) * x;
        }
    }

private:
    // Follows the played note (decimated YIN, as in the harmonizer) and sizes the window to whole periods.
    void track (float x) noexcept
    {
        accum += x;
        if (++accumCount < decimation)
            return;
        trackRing[(size_t) ringPos] = accum / (float) decimation;
        ringPos = (ringPos + 1) % trackLength;
        accum = 0.0f;
        accumCount = 0;
        if (++sinceDetect < trackHop)
            return;
        sinceDetect = 0;
        for (int i = 0; i < trackLength; ++i)
            trackScratch[(size_t) i] = trackRing[(size_t) ((ringPos + i) % trackLength)];
        const float hz = detector.detect (trackScratch.data(), trackLength, sampleRate / decimation, 60.0f, 1400.0f, 0.2f, 0.003f);
        if (hz <= 0.0f)
            return; // keep the last window through decays and rests
        const float period = (float) (sampleRate / hz);
        const float periods = std::max (1.0f, std::round (defaultWindowSeconds * (float) sampleRate / period));
        shifter.setWindow (juce::jlimit (minWindowSeconds * (float) sampleRate, maxWindowSeconds * (float) sampleRate, periods * period));
    }

    static constexpr float defaultWindowSeconds = 0.035f, minWindowSeconds = 0.02f, maxWindowSeconds = 0.06f;
    static constexpr int trackLength = 512, trackHop = 128; // ~42 ms at ~12 kHz, re-checked every ~11 ms

    double sampleRate = 48000.0;
    PitchShifter shifter;
    int decimation = 4, ringPos = 0, accumCount = 0, sinceDetect = 0;
    float accum = 0.0f;
    std::vector<float> trackRing, trackScratch;
    YinDetector detector;
    float targetSemis = 0.0f, smoothedSemis = 0.0f, smoothing = 0.0f, mix = 1.0f;
};
} // namespace poman
