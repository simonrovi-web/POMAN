#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <atomic>
#include <vector>

namespace poman
{
struct TunerReading
{
    float frequency = 0.0f; // 0 when no stable pitch
    int midiNote = 0;
    float cents = 0.0f;

    juce::String noteName() const
    {
        static const char* names[] { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        return juce::String (names[((midiNote % 12) + 12) % 12]) + juce::String (midiNote / 12 - 1);
    }
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
        double energy = 0.0;
        for (int i = 0; i < length; ++i)
        {
            float sum = 0.0f;
            for (int k = 0; k < decimation; ++k)
                sum += history[(size_t) (i * decimation + k)];
            decimated[(size_t) i] = sum / (float) decimation;
            energy += (double) decimated[(size_t) i] * decimated[(size_t) i];
        }

        if (std::sqrt (energy / length) < 0.002)
            return {};

        // Low B on a 7-string (~31 Hz) up to ~1.3 kHz.
        const int tauMin = juce::jmax (2, (int) (rate / 1300.0));
        const int tauMax = juce::jmin (length / 2, (int) (rate / 28.0));
        const int integration = length - tauMax;

        difference.assign ((size_t) tauMax + 2, 0.0f);
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            float sum = 0.0f;
            for (int j = 0; j < integration; ++j)
            {
                const float d = decimated[(size_t) j] - decimated[(size_t) (j + tau)];
                sum += d * d;
            }
            difference[(size_t) tau] = sum;
        }

        // Cumulative mean normalised difference.
        float running = 0.0f;
        difference[0] = 1.0f;
        for (int tau = 1; tau <= tauMax + 1; ++tau)
        {
            running += difference[(size_t) tau];
            difference[(size_t) tau] = running > 0.0f ? difference[(size_t) tau] * (float) tau / running : 1.0f;
        }

        int best = -1;
        for (int tau = tauMin; tau <= tauMax; ++tau)
        {
            if (difference[(size_t) tau] < 0.15f)
            {
                while (tau + 1 <= tauMax && difference[(size_t) tau + 1] < difference[(size_t) tau])
                    ++tau;
                best = tau;
                break;
            }
        }

        if (best < 0)
            return {};

        const float a = difference[(size_t) best - 1], b = difference[(size_t) best], c = difference[(size_t) best + 1];
        const float denom = a - 2.0f * b + c;
        const float refined = (float) best + (std::abs (denom) > 1.0e-9f ? 0.5f * (a - c) / denom : 0.0f);

        TunerReading reading;
        reading.frequency = (float) (rate / refined);
        const float midi = 69.0f + 12.0f * std::log2 (reading.frequency / 440.0f);
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
    std::vector<float> decimated, difference;
};
} // namespace poman
