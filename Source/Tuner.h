#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "PitchDetect.h"
#include <atomic>
#include <vector>

namespace poman
{
struct TunerReading
{
    float frequency = 0.0f; // 0 when no stable pitch
    int midiNote = 0;
    float cents = 0.0f;

    juce::String noteName() const { return midiNoteName (midiNote); }
};

// YIN pitch detector. The audio thread only copies samples into a lock-free FIFO;
// analysis runs on the UI thread.
class Tuner
{
public:
    void prepare (double newSampleRate)
    {
        sampleRate.store (newSampleRate);
        fifo.reset();
    }

    // Audio thread.
    void push (const float* samples, int numSamples) noexcept
    {
        int start1, size1, start2, size2;
        fifo.prepareToWrite (numSamples, start1, size1, start2, size2);
        std::copy (samples, samples + size1, fifoBuffer.begin() + start1);
        std::copy (samples + size1, samples + size1 + size2, fifoBuffer.begin() + start2);
        fifo.finishedWrite (size1 + size2);
    }

    // UI thread.
    TunerReading analyse()
    {
        drainFifo();

        const double sr = sampleRate.load();
        const int decimation = sr > 60000.0 ? 4 : 2;
        const double rate = sr / decimation;
        const int length = windowSize / decimation;

        decimated.resize ((size_t) length);
        for (int i = 0; i < length; ++i)
        {
            float sum = 0.0f;
            for (int k = 0; k < decimation; ++k)
                sum += history[(size_t) (i * decimation + k)];
            decimated[(size_t) i] = sum / (float) decimation;
        }

        // Low B on a 7-string (~31 Hz) up to ~1.3 kHz.
        TunerReading reading;
        reading.frequency = detector.detect (decimated.data(), length, rate, 28.0f, 1300.0f);
        if (reading.frequency <= 0.0f)
            return {};

        const float midi = frequencyToMidi (reading.frequency);
        reading.midiNote = juce::roundToInt (midi);
        reading.cents = (midi - (float) reading.midiNote) * 100.0f;
        return reading;
    }

private:
    void drainFifo()
    {
        const int ready = fifo.getNumReady();
        if (ready <= 0)
            return;

        const int keep = juce::jmax (0, windowSize - ready);
        std::move (history.end() - keep, history.end(), history.begin());

        const int toRead = juce::jmin (ready, windowSize);
        fifo.finishedRead (ready - toRead); // drop anything older than one window

        int start1, size1, start2, size2;
        fifo.prepareToRead (toRead, start1, size1, start2, size2);
        auto out = history.begin() + keep;
        out = std::copy (fifoBuffer.begin() + start1, fifoBuffer.begin() + start1 + size1, out);
        std::copy (fifoBuffer.begin() + start2, fifoBuffer.begin() + start2 + size2, out);
        fifo.finishedRead (size1 + size2);
    }

    static constexpr int fifoSize = 32768;
    static constexpr int windowSize = 4096;

    std::atomic<double> sampleRate { 48000.0 };
    juce::AbstractFifo fifo { fifoSize };
    std::vector<float> fifoBuffer = std::vector<float> (fifoSize);
    std::vector<float> history = std::vector<float> (windowSize);
    std::vector<float> decimated;
    YinDetector detector;
};
} // namespace poman
