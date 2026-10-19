// Renders every drum voice of each kit to WAV (for listening) and prints objective measurements:
// spectral centroid of the first 100 ms, dominant low frequency, -40 dB decay time and peak.
// Usage: POMANDrumAnalysis <output folder>
#include "dsp/DrumMachine.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

namespace
{
constexpr double sampleRate = 48000.0;

struct Metrics { float centroidHz, lowPeakHz, decaySeconds, peak; };

Metrics analyse (const juce::AudioBuffer<float>& b)
{
    Metrics m {};
    const int n = b.getNumSamples();
    m.peak = juce::jmax (b.getMagnitude (0, 0, n), b.getMagnitude (1, 0, n));

    // -40 dB decay: last sample above peak/100 (mid channel).
    for (int i = n - 1; i >= 0; --i)
        if (std::abs (0.5f * (b.getSample (0, i) + b.getSample (1, i))) > m.peak * 0.01f)
        {
            m.decaySeconds = (float) (i / sampleRate);
            break;
        }

    constexpr int order = 13, size = 1 << order; // 8192 points, 5.9 Hz bins
    juce::dsp::FFT fft (order);
    std::vector<float> data (size * 2, 0.0f);
    const int len = juce::jmin (size, (int) (0.1 * sampleRate));
    for (int i = 0; i < len; ++i)
    {
        const float w = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (len - 1));
        data[(size_t) i] = 0.5f * (b.getSample (0, i) + b.getSample (1, i)) * w;
    }
    fft.performFrequencyOnlyForwardTransform (data.data());

    double num = 0.0, den = 0.0, best = 0.0;
    for (int k = 1; k < size / 2; ++k)
    {
        const double hz = k * sampleRate / size, mag = data[(size_t) k];
        num += hz * mag * mag;
        den += mag * mag;
        if (hz < 400.0 && mag > best)
            best = mag, m.lowPeakHz = (float) hz;
    }
    m.centroidHz = den > 0.0 ? (float) (num / den) : 0.0f;
    return m;
}
} // namespace

int main (int argc, char** argv)
{
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();
    const auto names = poman::DrumMachine::instrumentNames();
    const auto kits = poman::DrumMachine::kitNames();

    for (int kit = 0; kit < kits.size(); ++kit)
    {
        std::printf ("== %s kit ==\n%-8s %10s %10s %9s %6s\n", kits[kit].toRawUTF8(), "voice", "centroid", "low peak", "decay", "peak");
        for (int inst = 0; inst < poman::DrumMachine::numInstruments; ++inst)
        {
            poman::DrumMachine d;
            d.setEnabled (true);
            d.setKit (kit);
            d.loadPreset ((int) poman::DrumMachine::patternPresets().size() - 1); // Empty
            d.setStep (inst, 0, 1);
            d.prepare (sampleRate);

            const int length = (int) (sampleRate * 2.5); // shorter than a bar at 60 BPM: one hit only
            juce::AudioBuffer<float> out (2, length);
            out.clear();
            for (int pos = 0; pos < length; pos += 256)
                d.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), juce::jmin (256, length - pos), pos, 60.0);

            const auto m = analyse (out);
            std::printf ("%-8s %8.0f Hz %8.0f Hz %7.2f s %6.2f\n", names[inst].toRawUTF8(), m.centroidHz, m.lowPeakHz, m.decaySeconds, m.peak);

            auto file = outDir.getChildFile (kits[kit] + " - " + juce::String (inst + 1) + " " + names[inst].replace (" ", "") + ".wav");
            file.deleteFile();
            std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
            juce::WavAudioFormat wav;
            if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24)))
                writer->writeFromAudioSampleBuffer (out, 0, length);
        }
    }

    // One bar of each groove with the default kit, as a quick listening reference.
    for (int p = 0; p < 3; ++p)
    {
        poman::DrumMachine d;
        d.setEnabled (true);
        d.loadPreset (p);
        d.prepare (sampleRate);
        const int length = (int) (sampleRate * 60.0 / 110.0 * 4.0 * 2.0); // two bars at 110 BPM
        juce::AudioBuffer<float> out (2, length);
        out.clear();
        for (int pos = 0; pos < length; pos += 256)
            d.process (out.getWritePointer (0, pos), out.getWritePointer (1, pos), juce::jmin (256, length - pos), pos, 110.0);
        auto file = outDir.getChildFile ("Groove - " + juce::String (poman::DrumMachine::patternPresets()[(size_t) p].name) + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        if (auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (2).withBitsPerSample (24)))
            writer->writeFromAudioSampleBuffer (out, 0, length);
        std::printf ("groove %-8s peak %.2f\n", poman::DrumMachine::patternPresets()[(size_t) p].name, out.getMagnitude (0, length));
    }
    return 0;
}
