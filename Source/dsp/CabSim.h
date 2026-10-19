#pragma once

#include "DspCommon.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>

namespace poman
{
// Cabinet: two impulse-response slots (e.g. two microphones on one speaker) blended with an
// equal-power A/B mix, or a built-in 4x12 filter model when no IR is loaded. A second IR is
// time-aligned to the first by cross-correlation when loaded, so blending never comb-filters.
// Low and high cuts shape the result in either mode.
class CabSim
{
public:
    static constexpr int numSlots = 2;
    static constexpr int maxAlignment = 256; // samples at the processing rate

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        for (auto& c : convolutions)
            c.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
        for (auto& s : scratch)
            s.assign ((size_t) maxBlockSize, 0.0f);

        lowCut.set (ArrayCoeffs::makeHighPass (sampleRate, 70.0, 0.8));
        resonance.set (ArrayCoeffs::makePeakFilter (sampleRate, 115.0, 1.3, dbToGain (4.0f)));
        boxDip.set (ArrayCoeffs::makePeakFilter (sampleRate, 420.0, 1.0, dbToGain (-4.0f)));
        lastMic = -1.0f;
        setMic (mic);
        lastLowCut = lastHighCut = -1.0f;
        setCuts (lowCutHz, highCutHz);
        updateAlignment();
        reset();
    }

    void reset()
    {
        for (auto& c : convolutions)
            c.reset();
        for (auto* f : { &lowCut, &resonance, &boxDip, &speakerPeak, &rollOff1, &rollOff2, &cutLow, &cutHigh })
            f->reset();
        for (auto& d : delays)
            d.fill (0.0f);
        delayWrite = 0;
    }

    // 0 = mic at the cone edge (dark), 1 = dead centre (bright, aggressive). Built-in model only.
    void setMic (float newMic)
    {
        mic = newMic;
        if (mic == lastMic)
            return;

        lastMic = mic;
        const double cutoff = 3600.0 + mic * 4400.0;
        speakerPeak.set (ArrayCoeffs::makePeakFilter (sampleRate, 2400.0 + mic * 900.0, 1.1, dbToGain (1.0f + mic * 4.0f)));
        rollOff1.set (ArrayCoeffs::makeLowPass (sampleRate, cutoff, 0.85));
        rollOff2.set (ArrayCoeffs::makeLowPass (sampleRate, cutoff * 1.25, 0.6));
    }

    void setMix (float newMix) { mix = juce::jlimit (0.0f, 1.0f, newMix); }

    void setCuts (float newLowHz, float newHighHz)
    {
        lowCutHz = newLowHz;
        highCutHz = newHighHz;
        if (lowCutHz != lastLowCut)
        {
            lastLowCut = lowCutHz;
            cutLow.set (ArrayCoeffs::makeHighPass (sampleRate, lowCutHz, 0.707));
        }
        if (highCutHz != lastHighCut)
        {
            lastHighCut = highCutHz;
            cutHigh.set (ArrayCoeffs::makeLowPass (sampleRate, juce::jmin ((double) highCutHz, sampleRate * 0.45), 0.707));
        }
    }

    // Message thread. Reads the file, keeps its head for alignment and hands it to the convolver.
    juce::Result loadImpulseResponse (int slot, const juce::File& file)
    {
        juce::AudioFormatManager formats;
        formats.registerBasicFormats();
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (file));
        if (reader == nullptr)
            return juce::Result::fail ("Not an audio file: " + file.getFileName());
        if (reader->lengthInSamples < 16 || reader->lengthInSamples > (juce::int64) (reader->sampleRate * 2.0))
            return juce::Result::fail ("An impulse response should be between 16 samples and 2 seconds long.");

        juce::AudioBuffer<float> ir (1, (int) reader->lengthInSamples);
        reader->read (&ir, 0, ir.getNumSamples(), 0, true, false); // left channel of stereo files

        heads[(size_t) slot] = std::vector<float> (ir.getReadPointer (0), ir.getReadPointer (0) + juce::jmin (ir.getNumSamples(), 2048));
        headRates[(size_t) slot] = reader->sampleRate;

        // Trim::no keeps each file's own onset, which the alignment below relies on.
        convolutions[(size_t) slot].loadImpulseResponse (std::move (ir), reader->sampleRate, juce::dsp::Convolution::Stereo::no,
                                                         juce::dsp::Convolution::Trim::no, juce::dsp::Convolution::Normalise::yes);
        loadedSlots[(size_t) slot].store (true);
        updateAlignment();
        return juce::Result::ok();
    }

    void clearImpulseResponse (int slot)
    {
        loadedSlots[(size_t) slot].store (false);
        heads[(size_t) slot].clear();
        updateAlignment();
    }

    bool hasImpulseResponse (int slot) const { return loadedSlots[(size_t) slot].load(); }

    // Samples by which slot B lags slot A (positive: A is delayed to match B).
    int getAlignmentSamples() const { return alignment.load(); }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        auto* x = block.getChannelPointer (0);
        const int n = (int) block.getNumSamples();
        const bool hasA = loadedSlots[0].load(), hasB = loadedSlots[1].load();

        if (! hasA && ! hasB)
        {
            for (int i = 0; i < n; ++i)
                x[i] = rollOff2 (rollOff1 (speakerPeak (boxDip (resonance (lowCut (x[i]))))));
        }
        else
        {
            // One IR plays at unity; two are blended with equal power.
            const float angle = mix * juce::MathConstants<float>::halfPi;
            const float gainA = hasB ? std::cos (angle) : 1.0f;
            const float gainB = hasA ? std::sin (angle) : 1.0f;
            const int lag = alignment.load();
            const std::array<int, numSlots> delay { juce::jmax (0, lag), juce::jmax (0, -lag) };
            const std::array<bool, numSlots> active { hasA, hasB };

            for (int s = 0; s < numSlots; ++s)
            {
                if (! active[(size_t) s])
                    continue;
                auto& buf = scratch[(size_t) s];
                std::copy (x, x + n, buf.begin());
                float* channels[] { buf.data() };
                juce::dsp::AudioBlock<float> slotBlock (channels, 1, (size_t) n);
                convolutions[(size_t) s].process (juce::dsp::ProcessContextReplacing<float> (slotBlock));
            }

            auto& lineA = delays[0];
            auto& lineB = delays[1];
            for (int i = 0; i < n; ++i)
            {
                const int w = (delayWrite + i) & delayMask;
                lineA[(size_t) w] = hasA ? scratch[0][(size_t) i] : 0.0f;
                lineB[(size_t) w] = hasB ? scratch[1][(size_t) i] : 0.0f;
                x[i] = gainA * lineA[(size_t) ((w - delay[0]) & delayMask)] + gainB * lineB[(size_t) ((w - delay[1]) & delayMask)];
            }
            delayWrite = (delayWrite + n) & delayMask;
        }

        if (lowCutHz > 20.5f || highCutHz < 19900.0f)
            for (int i = 0; i < n; ++i)
                x[i] = cutHigh (cutLow (x[i]));
    }

    // Lag (in samples) that best aligns b to a, searched within +/- maxLag. Positive: b arrives later.
    static int findLag (const std::vector<float>& a, const std::vector<float>& b, int maxLag)
    {
        const int len = (int) juce::jmin (a.size(), b.size());
        int best = 0;
        double bestScore = -1.0e30;
        for (int lag = -maxLag; lag <= maxLag; ++lag)
        {
            double sum = 0.0;
            for (int i = 0; i < len; ++i)
            {
                const int j = i + lag;
                if (j >= 0 && j < len)
                    sum += (double) a[(size_t) i] * b[(size_t) j];
            }
            if (sum > bestScore)
                bestScore = sum, best = lag;
        }
        return best;
    }

private:
    void updateAlignment()
    {
        int lag = 0;
        const auto& a = heads[0];
        const auto& b = heads[1];
        if (! a.empty() && ! b.empty() && headRates[0] == headRates[1] && headRates[0] > 0.0)
        {
            const int maxLagFile = (int) (headRates[0] * 0.002); // a mic moved up to ~70 cm
            lag = juce::roundToInt (findLag (a, b, maxLagFile) * sampleRate / headRates[0]);
        }
        alignment.store (juce::jlimit (-maxAlignment, maxAlignment, lag));
    }

    static constexpr int delaySize = 1024, delayMask = delaySize - 1; // > maxAlignment + block

    double sampleRate = 48000.0;
    std::array<juce::dsp::Convolution, numSlots> convolutions;
    std::array<std::atomic<bool>, numSlots> loadedSlots {};
    std::array<std::vector<float>, numSlots> heads, scratch;
    std::array<double, numSlots> headRates {};
    std::atomic<int> alignment { 0 };
    std::array<std::array<float, delaySize>, numSlots> delays {};
    int delayWrite = 0;

    Biquad lowCut, resonance, boxDip, speakerPeak, rollOff1, rollOff2, cutLow, cutHigh;
    float mic = 0.5f, lastMic = -1.0f, mix = 0.5f;
    float lowCutHz = 20.0f, highCutHz = 20000.0f, lastLowCut = -1.0f, lastHighCut = -1.0f;
};
} // namespace poman
