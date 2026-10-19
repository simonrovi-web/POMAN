// Generates POMAN's bundled cabinet impulse responses (synthetic, designed here: no third-party IRs).
// Each IR is a unit impulse through a speaker/cabinet filter model plus a few early cabinet
// reflections, normalised and written as 48 kHz / 24-bit mono WAV.
// Usage: POMANIrGen <output folder>
#include "dsp/DspCommon.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cstdio>

namespace
{
constexpr double sampleRate = 48000.0;
constexpr int length = 4096; // 85 ms

struct Peak { double hz, db, q; };
struct Reflection { double ms, gain, lowPassHz; };

struct CabDesign
{
    const char* name;
    double highPassHz, highPassQ;
    std::vector<Peak> peaks;
    double lowPassHz, lowPassQ, lowPass2Hz;
    std::vector<Reflection> reflections;
};

std::vector<float> render (const CabDesign& d)
{
    using poman::ArrayCoeffs;
    std::vector<poman::Biquad> chain;
    const auto add = [&chain] (const poman::BiquadCoeffs& c) { chain.emplace_back().set (c); };
    add (ArrayCoeffs::makeHighPass (sampleRate, d.highPassHz, d.highPassQ));
    for (const auto& p : d.peaks)
        add (ArrayCoeffs::makePeakFilter (sampleRate, p.hz, p.q, juce::Decibels::decibelsToGain (p.db)));
    add (ArrayCoeffs::makeLowPass (sampleRate, d.lowPassHz, d.lowPassQ));
    add (ArrayCoeffs::makeLowPass (sampleRate, d.lowPass2Hz, 0.6));

    std::vector<float> direct (length);
    for (int i = 0; i < length; ++i)
    {
        float x = i == 0 ? 1.0f : 0.0f;
        for (auto& f : chain)
            x = f (x);
        direct[(size_t) i] = x;
    }

    // Early reflections from the cabinet walls: delayed, darker copies of the direct sound.
    auto ir = direct;
    for (const auto& r : d.reflections)
    {
        poman::Biquad dark;
        dark.set (ArrayCoeffs::makeLowPass (sampleRate, r.lowPassHz, 0.707));
        const int offset = (int) std::round (r.ms * 0.001 * sampleRate);
        for (int i = 0; i + offset < length; ++i)
            ir[(size_t) (i + offset)] += (float) r.gain * dark (direct[(size_t) i]);
    }

    // Fade the tail and normalise the peak.
    float peak = 0.0f;
    for (int i = 0; i < length; ++i)
    {
        const float fade = i > length - 512 ? (float) (length - i) / 512.0f : 1.0f;
        ir[(size_t) i] *= fade;
        peak = std::max (peak, std::abs (ir[(size_t) i]));
    }
    for (auto& v : ir)
        v *= 0.9f / peak;
    return ir;
}
} // namespace

int main (int argc, char** argv)
{
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    const std::vector<CabDesign> designs {
        { "POMAN 4x12 Modern",   78.0, 1.1, { { 100, 5, 1.3 }, { 450, -4, 1.0 }, { 2500, 4, 1.2 }, { 4200, 2, 2.0 } },
          6500.0, 0.9, 8000.0, { { 0.6, 0.25, 5000 }, { 1.3, 0.12, 3500 } } },
        { "POMAN 4x12 Vintage",  70.0, 0.9, { { 110, 4, 1.2 }, { 800, 2, 0.8 }, { 2000, 3, 1.0 } },
          4800.0, 0.8, 6000.0, { { 0.8, 0.3, 4000 }, { 1.7, 0.15, 3000 } } },
        { "POMAN 2x12 Open Back", 95.0, 0.7, { { 140, 2, 1.0 }, { 1500, 2, 1.0 }, { 3200, 3, 1.2 } },
          7000.0, 0.8, 9000.0, { { 2.5, 0.35, 6000 }, { 5.0, 0.18, 4500 }, { 7.5, 0.08, 3500 } } },
        { "POMAN 1x12 Combo",    85.0, 1.0, { { 130, 3, 1.5 }, { 600, 3, 1.2 }, { 2200, 2, 1.0 } },
          5500.0, 0.8, 7000.0, { { 0.4, 0.3, 5000 }, { 1.1, 0.15, 3500 } } },
    };

    for (const auto& d : designs)
    {
        const auto ir = render (d);
        juce::AudioBuffer<float> buffer (1, length);
        buffer.copyFrom (0, 0, ir.data(), length);

        const auto file = outDir.getChildFile (juce::String (d.name) + ".wav");
        file.deleteFile();
        std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
        juce::WavAudioFormat wav;
        auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (1).withBitsPerSample (24));
        const bool ok = writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, length);
        std::printf ("%s  %s\n", ok ? "wrote" : "FAILED", file.getFullPathName().toRawUTF8());
        if (! ok)
            return 1;
    }
    return 0;
}
