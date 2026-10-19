#pragma once

#include "DspCommon.h"
#include <atomic>

namespace abyssal
{
// 4x12-style cabinet: a built-in filter model, or a user impulse response when one is loaded.
class CabSim
{
public:
    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        convolution.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 1 });
        lowCut.set (ArrayCoeffs::makeHighPass (sampleRate, 70.0f, 0.8f));
        resonance.set (ArrayCoeffs::makePeakFilter (sampleRate, 115.0f, 1.3f, dbToGain (4.0f)));
        boxDip.set (ArrayCoeffs::makePeakFilter (sampleRate, 420.0f, 1.0f, dbToGain (-4.0f)));
        lastMic = -1.0f;
        setMic (mic);
        reset();
    }

    void reset()
    {
        convolution.reset();
        for (auto* f : { &lowCut, &resonance, &boxDip, &speakerPeak, &rollOff1, &rollOff2 })
            f->reset();
    }

    // 0 = mic at the cone edge (dark), 1 = dead centre (bright, aggressive).
    void setMic (float newMic)
    {
        mic = newMic;
        if (mic == lastMic)
            return;

        lastMic = mic;
        const float cutoff = 3600.0f + mic * 4400.0f;
        speakerPeak.set (ArrayCoeffs::makePeakFilter (sampleRate, 2400.0f + mic * 900.0f, 1.1f, dbToGain (1.0f + mic * 4.0f)));
        rollOff1.set (ArrayCoeffs::makeLowPass (sampleRate, cutoff, 0.85f));
        rollOff2.set (ArrayCoeffs::makeLowPass (sampleRate, cutoff * 1.25f, 0.6f));
    }

    void loadImpulseResponse (const juce::File& file)
    {
        convolution.loadImpulseResponse (file, juce::dsp::Convolution::Stereo::no,
                                         juce::dsp::Convolution::Trim::yes, 0,
                                         juce::dsp::Convolution::Normalise::yes);
        useImpulseResponse = true;
    }

    void clearImpulseResponse() { useImpulseResponse = false; }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        if (useImpulseResponse.load())
        {
            convolution.process (juce::dsp::ProcessContextReplacing<float> (block));
            return;
        }

        auto* x = block.getChannelPointer (0);
        for (size_t i = 0; i < block.getNumSamples(); ++i)
            x[i] = rollOff2 (rollOff1 (speakerPeak (boxDip (resonance (lowCut (x[i]))))));
    }

private:
    double sampleRate = 44100.0;
    juce::dsp::Convolution convolution;
    std::atomic<bool> useImpulseResponse { false };
    Biquad lowCut, resonance, boxDip, speakerPeak, rollOff1, rollOff2;
    float mic = 0.5f, lastMic = -1.0f;
};
} // namespace abyssal
