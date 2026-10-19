// NAM engine checks with the example captures shipped in NeuralAmpModelerCore.
// Usage: POMANNamTests <NeuralAmpModelerCore/example_models folder>
#include "dsp/NamAmp.h"
#include <chrono>
#include <cstdio>

namespace
{
int failures = 0;

void expect (bool ok, const juce::String& label, const juce::String& detail = {})
{
    failures += ok ? 0 : 1;
    std::printf ("%s  %-34s %s\n", ok ? "PASS" : "FAIL", label.toRawUTF8(), detail.toRawUTF8());
}

struct Run { bool finite = true; float rmsDb = -200.0f; double cpuPercent = 0.0; };

Run render (poman::NamAmp& amp, double rate, int block, double seconds)
{
    amp.prepare (rate, block);
    poman::AmpParams p;
    p.gain = 5.0f; // 0 dB drive, flat tone
    p.master = 5.0f;
    amp.setParams (p);

    const int total = (int) (rate * seconds);
    std::vector<float> buf ((size_t) block);
    double sum = 0.0;
    Run r;
    const auto start = std::chrono::high_resolution_clock::now();
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = juce::jmin (block, total - pos);
        for (int i = 0; i < n; ++i) // a decaying E power chord at DI level
        {
            const double t = (pos + i) / rate;
            buf[(size_t) i] = (float) (0.15 * std::exp (-1.5 * t) * (std::sin (juce::MathConstants<double>::twoPi * 82.41 * t)
                                                                     + std::sin (juce::MathConstants<double>::twoPi * 123.47 * t)));
        }
        float* ch[] { buf.data() };
        juce::dsp::AudioBlock<float> blk (ch, 1, (size_t) n);
        amp.process (blk);
        for (int i = 0; i < n; ++i)
        {
            r.finite = r.finite && std::isfinite (buf[(size_t) i]);
            sum += (double) buf[(size_t) i] * buf[(size_t) i];
        }
    }
    const double elapsed = std::chrono::duration<double> (std::chrono::high_resolution_clock::now() - start).count();
    r.cpuPercent = 100.0 * elapsed / seconds;
    r.rmsDb = juce::Decibels::gainToDecibels ((float) std::sqrt (sum / total), -200.0f);
    return r;
}
} // namespace

int main (int argc, char** argv)
{
    const juce::File folder (argc > 1 ? juce::String (argv[1]) : juce::String());
    if (! folder.isDirectory())
    {
        std::printf ("usage: POMANNamTests <example_models folder>\n");
        return 1;
    }

    for (auto name : { "wavenet.nam", "lstm.nam", "wavenet_a1_standard.nam", "A2.nam" })
    {
        const auto file = folder.getChildFile (name);
        if (! file.existsAsFile())
            continue;

        poman::NamAmp amp;
        amp.prepare (48000.0, 256);
        const auto loadResult = amp.load (file);
        expect (loadResult.wasOk() && amp.isLoaded(), juce::String (name) + " loads", loadResult.getErrorMessage());
        if (loadResult.failed())
            continue;

        const auto at48 = render (amp, 48000.0, 256, 3.0);
        const int latency48 = amp.getLatencySamples();
        const auto at44 = render (amp, 44100.0, 256, 3.0);
        const int latency44 = amp.getLatencySamples();
        expect (at48.finite && at48.rmsDb > -60.0f && latency48 == 0,
                juce::String (name) + " @48k", juce::String::formatted ("rms %.1f dB, latency %d, CPU %.2f %% of a core", at48.rmsDb, latency48, at48.cpuPercent));
        expect (at44.finite && std::abs (at44.rmsDb - at48.rmsDb) < 1.5f && latency44 > 0 && latency44 < 260,
                juce::String (name) + " @44.1k (resampled)",
                juce::String::formatted ("rms %.1f dB, latency %d, CPU %.2f %%", at44.rmsDb, latency44, at44.cpuPercent));
    }

    // Unusable files fail with a message and leave no model loaded.
    const auto bad = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("poman_bad.nam");
    bad.replaceWithText ("{ this is not a capture");
    poman::NamAmp amp;
    amp.prepare (48000.0, 256);
    const auto r = amp.load (bad);
    expect (r.failed() && ! amp.isLoaded(), "corrupt file rejected", r.getErrorMessage().substring (0, 60));
    bad.deleteFile();
    expect (amp.load (folder.getChildFile ("missing.nam")).failed(), "missing file rejected");

    return failures;
}
