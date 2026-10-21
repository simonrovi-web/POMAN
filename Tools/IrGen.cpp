// Generates POMAN's bundled cabinet impulse responses (synthetic, designed here: no third-party IRs).
// Each IR combines a speaker (low resonance, cone break-up ripple), a cabinet (closed or open back,
// wall reflections), a microphone type and a mic position, and is written as 48 kHz / 24-bit mono WAV.
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

// The driver: where it resonates in the box, its presence region and how jagged the cone break-up is.
struct Speaker
{
    double resonanceHz, resonanceDb;
    std::vector<Peak> voice;      // broad character (mid hump, presence)
    std::vector<Peak> breakUp;    // narrow peaks/notches of the cone above ~1.5 kHz
    double rollOffHz;             // on-axis top end
};

struct Cabinet
{
    const char* name;
    Speaker speaker;
    double highPassHz, highPassQ;
    double boxDipHz, boxDipDb;    // panel/box coloration in the low mids
    bool openBack;                // open backs lose low end and get a back-wall reflection
    std::vector<Reflection> reflections;
};

struct Microphone { const char* name; std::vector<Peak> response; double lowPassHz, lowPassQ; double proximityDb; };

// Where the mic points: 0 = dust cap (brightest), 1 = outer cone. Distance adds floor/room reflections.
struct Position { const char* name; double offAxis; double distanceCm; };

std::vector<float> render (const Cabinet& cab, const Microphone& mic, const Position& pos)
{
    using poman::ArrayCoeffs;
    std::vector<poman::Biquad> chain;
    const auto add = [&chain] (const poman::BiquadCoeffs& c) { chain.emplace_back().set (c); };
    const auto db = [] (double d) { return juce::Decibels::decibelsToGain (d); };
    const auto& s = cab.speaker;

    // Cabinet and speaker low end.
    add (ArrayCoeffs::makeHighPass (sampleRate, cab.highPassHz * (cab.openBack ? 1.25 : 1.0), cab.highPassQ));
    add (ArrayCoeffs::makePeakFilter (sampleRate, s.resonanceHz, 1.4, db (s.resonanceDb - (cab.openBack ? 2.0 : 0.0))));
    add (ArrayCoeffs::makePeakFilter (sampleRate, cab.boxDipHz, 1.0, db (cab.boxDipDb)));
    for (const auto& p : s.voice)
        add (ArrayCoeffs::makePeakFilter (sampleRate, p.hz, p.q, db (p.db)));

    // Cone break-up is strongest on axis; off axis the cone's edge smooths and darkens it.
    const double breakUpScale = 1.0 - 0.5 * pos.offAxis;
    for (const auto& p : s.breakUp)
        add (ArrayCoeffs::makePeakFilter (sampleRate, p.hz, p.q, db (p.db * breakUpScale)));
    const double topEnd = s.rollOffHz * (1.0 - 0.45 * pos.offAxis);
    add (ArrayCoeffs::makeLowPass (sampleRate, topEnd, 0.8));
    add (ArrayCoeffs::makeLowPass (sampleRate, topEnd * 1.3, 0.6));
    // Dust-cap "fizz" peak on axis.
    add (ArrayCoeffs::makePeakFilter (sampleRate, 3800.0 + 800.0 * (1.0 - pos.offAxis), 1.4, db (3.5 * (1.0 - pos.offAxis))));

    // Microphone: its own response, and proximity bass when close.
    for (const auto& p : mic.response)
        add (ArrayCoeffs::makePeakFilter (sampleRate, p.hz, p.q, db (p.db)));
    add (ArrayCoeffs::makeLowPass (sampleRate, mic.lowPassHz, mic.lowPassQ));
    const double proximity = mic.proximityDb * juce::jlimit (0.0, 1.0, 1.0 - pos.distanceCm / 30.0);
    add (ArrayCoeffs::makeLowShelf (sampleRate, 180.0, 0.7, db (proximity)));

    std::vector<float> direct (length);
    for (int i = 0; i < length; ++i)
    {
        float x = i == 0 ? 1.0f : 0.0f;
        for (auto& f : chain)
            x = f (x);
        direct[(size_t) i] = x;
    }

    // Early reflections: cabinet walls (and the back wall for open backs), then the floor for a
    // distant mic: its path is longer by about twice the mic height minus the direct distance.
    auto reflections = cab.reflections;
    if (cab.openBack)
        reflections.push_back ({ 2.9, -0.35, 2500.0 }); // the back wave arrives inverted
    if (pos.distanceCm > 20.0)
    {
        const double extraMs = (std::sqrt (pos.distanceCm * pos.distanceCm + 4.0 * 60.0 * 60.0) - pos.distanceCm) / 34.3;
        reflections.push_back ({ extraMs, 0.45, 6000.0 });
        reflections.push_back ({ extraMs * 2.3, 0.2, 4000.0 });
    }

    auto ir = direct;
    for (const auto& r : reflections)
    {
        poman::Biquad dark;
        dark.set (ArrayCoeffs::makeLowPass (sampleRate, r.lowPassHz, 0.707));
        const int offset = (int) std::round (r.ms * 0.001 * sampleRate);
        for (int i = 0; i + offset < length; ++i)
            ir[(size_t) (i + offset)] += (float) r.gain * dark (direct[(size_t) i]);
    }

    // Time of flight: a distant mic hears the cab later (CabSim aligns two IRs automatically).
    const int flight = (int) std::round (pos.distanceCm / 34300.0 * sampleRate);
    if (flight > 0)
    {
        ir.insert (ir.begin(), (size_t) flight, 0.0f);
        ir.resize ((size_t) length);
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

bool write (const juce::File& file, const std::vector<float>& ir)
{
    juce::AudioBuffer<float> buffer (1, length);
    buffer.copyFrom (0, 0, ir.data(), length);
    file.deleteFile();
    std::unique_ptr<juce::OutputStream> stream = std::make_unique<juce::FileOutputStream> (file);
    juce::WavAudioFormat wav;
    auto writer = wav.createWriterFor (stream, juce::AudioFormatWriterOptions{}.withSampleRate (sampleRate).withNumChannels (1).withBitsPerSample (24));
    return writer != nullptr && writer->writeFromAudioSampleBuffer (buffer, 0, length);
}
} // namespace

int main (int argc, char** argv)
{
    const juce::File outDir (argc > 1 ? juce::String (argv[1]) : juce::File::getCurrentWorkingDirectory().getFullPathName());
    outDir.createDirectory();

    // Speakers: modern high-power (tight, 2.5 kHz bite), vintage low-power (warm mid hump, soft top),
    // American ceramic (scooped, glassy, extended), British alnico (chimey upper mids).
    const Speaker modern   { 105, 2.5, { { 450, -3, 1.0 }, { 2500, 4, 1.2 } },
                             { { 1800, -2.5, 6 }, { 3300, 3.0, 5 }, { 4300, -4.0, 7 }, { 5200, 3.0, 6 } }, 6500 };
    const Speaker vintage  { 95, 2.5, { { 800, 1.5, 0.8 }, { 2200, 3, 1.0 } },
                             { { 2400, 2.0, 5 }, { 3100, -2.5, 6 }, { 3900, 2.0, 6 } }, 6400 };
    const Speaker american { 115, 2.0, { { 500, -3, 0.8 }, { 3000, 3, 1.0 } },
                             { { 2200, -2.0, 5 }, { 3600, 2.5, 5 }, { 5800, 2.0, 4 } }, 7500 };
    const Speaker alnico   { 120, 2.0, { { 1200, 2, 1.0 }, { 3500, 4, 1.4 } },
                             { { 2700, -2.5, 6 }, { 4200, 3.0, 5 }, { 5000, -3.0, 7 } }, 6800 };

    const std::vector<Cabinet> cabinets {
        { "4x12 Modern",    modern,   80, 0.9, 450, -2, false, { { 0.6, 0.25, 5000 }, { 1.3, 0.12, 3500 } } },
        { "4x12 Vintage",   vintage,  70, 0.9, 300, -1, false, { { 0.8, 0.30, 4000 }, { 1.7, 0.15, 3000 } } },
        { "2x12 Open Back", american, 90, 0.7, 350, -1, true,  { { 1.2, 0.20, 5000 } } },
        { "1x12 Combo",     alnico,   85, 1.0, 400, -2, true,  { { 0.4, 0.25, 5000 } } },
    };

    // Generic microphone types: dynamic (presence peak, proximity), ribbon (dark, smooth, big
    // proximity), small condenser (flat and extended, little proximity at this distance).
    const Microphone dynamic   { "Dinamico",    { { 200, -1.0, 0.8 }, { 5500, 4.0, 1.2 }, { 9000, -3.0, 1.5 } }, 13000, 0.7, 2.5 };
    const Microphone ribbon    { "Cinta",       { { 1500, -1.0, 0.6 }, { 6000, -3.0, 0.7 } },                    9000, 0.6, 4.0 };
    const Microphone condenser { "Condensador", { { 10000, 2.0, 0.8 } },                                          18000, 0.7, 1.0 };

    struct Take { const Microphone* mic; Position pos; };
    const Take takes[] {
        { &dynamic,   { "Centro", 0.0, 2.0 } },
        { &dynamic,   { "Borde",  0.5, 2.0 } },
        { &dynamic,   { "Cono",   1.0, 2.0 } },
        { &ribbon,    { "Borde",  0.5, 5.0 } },
        { &condenser, { "Sala",   0.4, 60.0 } },
    };

    for (const auto& cab : cabinets)
        for (const auto& take : takes)
        {
            const auto name = juce::String ("POMAN ") + cab.name + " - " + take.mic->name + " " + take.pos.name;
            const auto file = outDir.getChildFile (name + ".wav");
            const bool ok = write (file, render (cab, *take.mic, take.pos));
            std::printf ("%s  %s\n", ok ? "wrote" : "FAILED", file.getFileName().toRawUTF8());
            if (! ok)
                return 1;
        }
    return 0;
}
