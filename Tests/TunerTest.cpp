// Offline check of the YIN tuner against synthetic guitar-like tones.
#include "Tuner.h"
#include <cstdio>

int main()
{
    struct Case { double sampleRate; float frequency; };
    const Case cases[] { { 44100.0, 41.20f }, { 44100.0, 82.41f }, { 48000.0, 110.0f }, { 48000.0, 196.0f },
                         { 48000.0, 329.63f }, { 96000.0, 82.41f }, { 96000.0, 659.25f }, { 48000.0, 84.0f } };
    int failures = 0;

    for (const auto& c : cases)
    {
        poman::Tuner tuner;
        tuner.prepare (c.sampleRate);

        std::vector<float> block (256);
        double phase = 0.0;
        poman::TunerReading reading;
        for (int b = 0; b < 40; ++b)
        {
            for (auto& s : block)
            {
                // Fundamental plus strong 2nd/3rd harmonics, like a plucked string.
                s = (float) (0.3 * std::sin (phase) + 0.25 * std::sin (2.0 * phase) + 0.12 * std::sin (3.0 * phase));
                phase += juce::MathConstants<double>::twoPi * c.frequency / c.sampleRate;
            }
            tuner.push (block.data(), (int) block.size());
            reading = tuner.analyse();
        }

        const float expectedMidi = 69.0f + 12.0f * std::log2 (c.frequency / 440.0f);
        const float errorCents = reading.frequency > 0.0f
                                     ? 1200.0f * std::log2 (reading.frequency / c.frequency)
                                     : 9999.0f;
        const bool ok = std::abs (errorCents) < 1.0f && reading.midiNote == juce::roundToInt (expectedMidi);
        failures += ok ? 0 : 1;
        std::printf ("%s  sr=%6.0f  in=%8.2f Hz  out=%8.2f Hz  %-4s %+6.2f cents  err=%+.3f cents\n",
                     ok ? "PASS" : "FAIL", c.sampleRate, c.frequency, reading.frequency,
                     reading.noteName().toRawUTF8(), reading.cents, errorCents);
    }

    poman::Tuner silent;
    silent.prepare (48000.0);
    std::vector<float> zeros (4096, 0.0f);
    silent.push (zeros.data(), (int) zeros.size());
    const bool silenceOk = silent.analyse().frequency == 0.0f;
    failures += silenceOk ? 0 : 1;
    std::printf ("%s  silence -> no reading\n", silenceOk ? "PASS" : "FAIL");

    return failures;
}
