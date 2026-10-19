#pragma once

#include "DspCommon.h"
#include "../PitchDetect.h"
#include <atomic>

namespace poman
{
struct Scale
{
    const char* name;
    std::vector<int> pitchClasses; // semitones above the key, ascending, starting at 0
};

inline const std::vector<Scale>& harmonizerScales()
{
    static const std::vector<Scale> scales {
        { "Major", { 0, 2, 4, 5, 7, 9, 11 } },
        { "Natural Minor", { 0, 2, 3, 5, 7, 8, 10 } },
        { "Dorian", { 0, 2, 3, 5, 7, 9, 10 } },
        { "Phrygian", { 0, 1, 3, 5, 7, 8, 10 } },
        { "Mixolydian", { 0, 2, 4, 5, 7, 9, 10 } },
        { "Harmonic Minor", { 0, 2, 3, 5, 7, 8, 11 } },
        { "Minor Pentatonic", { 0, 3, 5, 7, 10 } },
        { "Major Pentatonic", { 0, 2, 4, 7, 9 } },
    };
    return scales;
}

// Semitones to add to midiNote so the voice lands `steps` scale degrees away inside the key.
// Notes outside the scale are measured from the scale degree just below them.
inline int diatonicShift (int midiNote, int key, const Scale& scale, int steps)
{
    const auto& pcs = scale.pitchClasses;
    const int size = (int) pcs.size();
    const auto floorDiv = [] (int a, int b) { return (a >= 0 ? a : a - b + 1) / b; };

    const int relative = midiNote - key;
    const int octave = floorDiv (relative, 12);
    const int pitchClass = relative - octave * 12;

    int degree = 0;
    while (degree + 1 < size && pcs[(size_t) degree + 1] <= pitchClass)
        ++degree;

    const int absolute = octave * size + degree + steps;
    const int targetOctave = floorDiv (absolute, size);
    const int targetDegree = absolute - targetOctave * size;

    return (targetOctave * 12 + pcs[(size_t) targetDegree]) - (octave * 12 + pcs[(size_t) degree]);
}

// Two-voice harmonizer. track() follows the pitch of the clean DI; process() pitch-shifts the
// finished (post-cab) tone into two panned voices with a dual-tap crossfading delay line.
// The splice window is kept at a whole number of input periods: each tap jumps back by one
// window, so any remainder would slip the phase and pull the harmony out of tune.
class Harmonizer
{
public:
    enum class Mode { chromatic, diatonic };
    static constexpr int numVoices = 2;

    static juce::StringArray modeNames() { return { "Chromatic", "Diatonic" }; }
    static juce::StringArray keyNames() { return { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" }; }
    static juce::StringArray scaleNames()
    {
        juce::StringArray names;
        for (const auto& s : harmonizerScales())
            names.add (s.name);
        return names;
    }

    struct Params
    {
        Mode mode = Mode::diatonic;
        int key = 4, scale = 1;
        std::array<int, numVoices> steps { 2, 4 };
        std::array<float, numVoices> level { 0.8f, 0.0f };
        float dry = 1.0f;
    };

    void prepare (double newSampleRate)
    {
        sampleRate = newSampleRate;
        windowSamples = targetWindow = (float) (defaultWindowSeconds * sampleRate);
        windowGlide = onePoleCoeff (sampleRate, 30.0f);
        delayBuffer.assign ((size_t) bufferSize, 0.0f);
        writeIndex = 0;

        decimation = juce::jmax (1, juce::roundToInt (sampleRate / 12000.0));
        trackRing.assign ((size_t) trackLength, 0.0f);
        trackScratch.assign ((size_t) trackLength, 0.0f);
        detector.reserve (trackLength);
        ringPos = accumCount = sinceDetect = 0;
        accum = 0.0f;
        candidateNote = -1;
        candidateHits = 0;
        currentNote = -1;
        detectedNote.store (-1);

        glide = onePoleCoeff (sampleRate, 15.0f);
        for (auto& v : voices)
            v = {};
        updateTargets();
    }

    void setParams (const Params& p)
    {
        params = p;
        updateTargets();
    }

    // Clean DI, before gate/amp, so distortion does not confuse the pitch tracker.
    void track (const float* in, int numSamples) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            accum += in[i];
            if (++accumCount < decimation)
                continue;

            trackRing[(size_t) ringPos] = accum / (float) decimation;
            ringPos = (ringPos + 1) % trackLength;
            accum = 0.0f;
            accumCount = 0;

            if (++sinceDetect >= trackHop)
            {
                sinceDetect = 0;
                detectNote();
            }
        }
    }

    void process (const float* in, float* left, float* right, int numSamples) noexcept
    {
        constexpr std::array<float, numVoices> pan { -0.45f, 0.45f };
        std::array<float, numVoices> gainL {}, gainR {};
        for (int v = 0; v < numVoices; ++v)
        {
            const float angle = (pan[(size_t) v] + 1.0f) * juce::MathConstants<float>::pi * 0.25f;
            gainL[(size_t) v] = std::cos (angle) * params.level[(size_t) v];
            gainR[(size_t) v] = std::sin (angle) * params.level[(size_t) v];
        }

        for (int i = 0; i < numSamples; ++i)
        {
            const float x = in[i];
            delayBuffer[(size_t) writeIndex] = x;

            float outL = params.dry * x, outR = params.dry * x;
            windowSamples = targetWindow + windowGlide * (windowSamples - targetWindow);

            for (int v = 0; v < numVoices; ++v)
            {
                auto& voice = voices[(size_t) v];
                voice.ratio = voice.targetRatio + glide * (voice.ratio - voice.targetRatio);
                if (params.level[(size_t) v] < 1.0e-4f)
                    continue;

                const float shifted = tick (voice);
                outL += gainL[(size_t) v] * shifted;
                outR += gainR[(size_t) v] * shifted;
            }

            left[i] = outL;
            right[i] = outR;
            writeIndex = (writeIndex + 1) & bufferMask;
        }
    }

    // UI thread: last committed input note (-1 when none) and each voice's interval in semitones.
    int getDetectedNote() const { return detectedNote.load(); }
    int getVoiceSemitones (int voice) const { return voiceSemitones[(size_t) voice].load(); }

private:
    struct Voice
    {
        float phase = 0.0f, ratio = 1.0f, targetRatio = 1.0f;
    };

    void detectNote() noexcept
    {
        for (int i = 0; i < trackLength; ++i)
            trackScratch[(size_t) i] = trackRing[(size_t) ((ringPos + i) % trackLength)];

        const float hz = detector.detect (trackScratch.data(), trackLength, sampleRate / decimation, 70.0f, 1400.0f, 0.2f, 0.003f);
        if (hz <= 0.0f)
            return; // keep the last interval and window through note decays and rests

        const float period = (float) (sampleRate / hz);
        const float periods = std::max (1.0f, std::round ((float) (defaultWindowSeconds * sampleRate) / period));
        targetWindow = juce::jlimit (minWindowSeconds * (float) sampleRate, maxWindowSeconds * (float) sampleRate, periods * period);

        if (params.mode != Mode::diatonic)
            return;

        // Require two agreeing detections before switching, so pick noise cannot flip the harmony.
        const int note = juce::roundToInt (frequencyToMidi (hz));
        candidateHits = note == candidateNote ? candidateHits + 1 : 1;
        candidateNote = note;
        if (candidateHits >= 2 && note != currentNote)
        {
            currentNote = note;
            detectedNote.store (note);
            updateTargets();
        }
    }

    void updateTargets() noexcept
    {
        const auto& scales = harmonizerScales();
        const auto& scale = scales[(size_t) juce::jlimit (0, (int) scales.size() - 1, params.scale)];

        for (int v = 0; v < numVoices; ++v)
        {
            int semitones = params.steps[(size_t) v];
            if (params.mode == Mode::diatonic)
                semitones = currentNote >= 0 ? diatonicShift (currentNote, params.key, scale, params.steps[(size_t) v])
                                             : diatonicShift (60 + params.key, params.key, scale, params.steps[(size_t) v]);

            voiceSemitones[(size_t) v].store (semitones);
            voices[(size_t) v].targetRatio = std::pow (2.0f, (float) semitones / 12.0f);
        }
    }

    // Two read taps sweep through a ~35 ms window at a speed set by the pitch ratio; Hann gains
    // offset by half a window sum to one, hiding each tap's wrap-around.
    float tick (Voice& voice) noexcept
    {
        voice.phase += (1.0f - voice.ratio) / windowSamples;
        voice.phase -= std::floor (voice.phase);

        float phaseB = voice.phase + 0.5f;
        phaseB -= std::floor (phaseB);

        const auto read = [this] (float delay)
        {
            float pos = (float) writeIndex - delay;
            if (pos < 0.0f)
                pos += (float) bufferSize;
            const int i0 = (int) pos;
            const float frac = pos - (float) i0;
            return delayBuffer[(size_t) i0] + frac * (delayBuffer[(size_t) ((i0 + 1) & bufferMask)] - delayBuffer[(size_t) i0]);
        };

        constexpr float twoPi = juce::MathConstants<float>::twoPi;
        const float gainA = 0.5f - 0.5f * std::cos (twoPi * voice.phase);
        const float gainB = 0.5f - 0.5f * std::cos (twoPi * phaseB);
        return gainA * read (minDelay + voice.phase * windowSamples) + gainB * read (minDelay + phaseB * windowSamples);
    }

    static constexpr float defaultWindowSeconds = 0.035f, minWindowSeconds = 0.02f, maxWindowSeconds = 0.06f;
    static constexpr int bufferSize = 16384; // > 60 ms at 192 kHz
    static constexpr int bufferMask = bufferSize - 1;
    static constexpr float minDelay = 2.0f;
    static constexpr int trackLength = 512; // ~42 ms at the ~12 kHz tracking rate
    static constexpr int trackHop = 128;

    double sampleRate = 48000.0;
    float windowSamples = 1680.0f, targetWindow = 1680.0f, windowGlide = 0.0f, glide = 0.0f;
    std::vector<float> delayBuffer;
    int writeIndex = 0;
    std::array<Voice, numVoices> voices;
    Params params;

    int decimation = 4, ringPos = 0, accumCount = 0, sinceDetect = 0;
    float accum = 0.0f;
    std::vector<float> trackRing, trackScratch;
    YinDetector detector;
    int candidateNote = -1, candidateHits = 0, currentNote = -1;

    std::atomic<int> detectedNote { -1 };
    std::array<std::atomic<int>, numVoices> voiceSemitones {};
};
} // namespace poman
