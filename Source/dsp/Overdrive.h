#pragma once

#include "DspCommon.h"
#include <memory>

namespace poman
{
// Drive pedal with five circuit types. Each type has its own pre-filtering, gain range,
// clipping curve, clean blend and tone control. The whole circuit runs 8x oversampled: hard
// clippers generate harmonics far above Nyquist, which at the base rate fold back as aliasing.
class Overdrive
{
public:
    enum class Type
    {
        midBoost,    // mid-hump overdrive: clips only the upper band, tightens lows before the amp
        bluesDrive,  // transparent: clean signal blended with soft-clipped drive, treble tone
        distortion,  // op-amp hard clipping with gain-dependent slew roll-off
        fuzz,        // two cascaded clipping stages and a mid-scooped tone stack
        trebleBoost, // germanium-style treble booster that pushes the amp's front end
        count
    };

    static juce::StringArray typeNames() { return { "Mid Boost", "Blues Drive", "Distortion", "Fuzz", "Treble Boost" }; }

    static constexpr size_t oversamplingOrder = 4; // 16x

    // maxBlockSize bounds a single process() call.
    void prepare (double hostSampleRate, int maxBlockSize = 1 << 17)
    {
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1, oversamplingOrder, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false);
        oversampling->initProcessing ((size_t) maxBlockSize);
        sampleRate = hostSampleRate * (double) (1 << oversamplingOrder); // all filters run at the oversampled rate
        currentType = Type::count;
        lastTone = lastDrive = -1.0f;
        setParams (type, drive, 0.5f, 0.0f);
        reset();
    }

    int getLatencySamples() const
    {
        return oversampling != nullptr ? (int) std::round (oversampling->getLatencyInSamples()) : 0;
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();
        diodeClip.reset();
        fuzzStage1.reset();
        fuzzStage2.reset();
        for (auto* f : { &clipHighPass, &cleanHighPass, &slewLowPass, &toneLowPass, &toneHighPass, &toneShelf })
            f->reset();
    }

    void setParams (Type newType, float newDrive, float tone, float levelDb)
    {
        type = newType;
        drive = newDrive;
        level = dbToGain (levelDb);

        const bool typeChanged = type != currentType;
        if (typeChanged)
        {
            currentType = type;
            applyTypeFilters();
        }

        switch (type)
        {
            case Type::midBoost:    driveGain = 1.0f + drive * drive * 120.0f; break;
            case Type::bluesDrive:  driveGain = 1.0f + drive * drive * 60.0f; break;
            case Type::distortion:  driveGain = 2.0f + drive * drive * 1500.0f; break;
            case Type::fuzz:        driveGain = 8.0f + drive * drive * 2500.0f; break;
            case Type::trebleBoost: driveGain = 1.5f + drive * 14.0f; break;
            case Type::count:       break;
        }

        if (typeChanged || drive != lastDrive)
        {
            lastDrive = drive;
            // The distortion op-amp loses bandwidth as its gain rises.
            if (type == Type::distortion)
                slewLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, 16000.0f / (1.0f + drive * 3.5f), 0.707f));
        }

        if (typeChanged || tone != lastTone)
        {
            lastTone = tone;
            applyTone (tone);
        }
    }

    void process (float* samples, int numSamples) noexcept
    {
        float* channels[] { samples };
        juce::dsp::AudioBlock<float> block (channels, 1, (size_t) numSamples);
        auto up = oversampling->processSamplesUp (block);
        processOversampled (up.getChannelPointer (0), (int) up.getNumSamples());
        oversampling->processSamplesDown (block);
    }

private:
    void processOversampled (float* samples, int numSamples) noexcept
    {
        switch (type)
        {
            case Type::midBoost:
            {
                constexpr float bias = 0.12f;
                const float offset = std::tanh (bias);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = samples[i];
                    const float clipped = std::tanh (clipHighPass (x) * driveGain + bias) - offset;
                    samples[i] = toneLowPass (cleanHighPass (x) + 0.35f * clipped) * level;
                }
                break;
            }

            case Type::bluesDrive:
            {
                const float cleanMix = 1.0f - 0.65f * drive;
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = samples[i];
                    const float driven = clipHighPass (x) * driveGain;
                    const float clipped = driven / (1.0f + std::abs (driven)); // symmetric, very soft knee
                    samples[i] = toneShelf (cleanMix * x + 0.6f * clipped) * level;
                }
                break;
            }

            case Type::distortion:
            {
                for (int i = 0; i < numSamples; ++i)
                {
                    const float driven = slewLowPass (clipHighPass (samples[i]) * driveGain);
                    // Silicon diode pair to ground: an exponential knee, modelled with tanh. A hard knee
                    // would create harmonics that alias even at 8x.
                    const float clipped = diodeClip (driven);
                    samples[i] = toneLowPass (clipped) * 0.08f * level;
                }
                break;
            }

            case Type::fuzz:
            {
                constexpr float bias = 0.3f;
                const float offset = std::tanh (bias);
                for (int i = 0; i < numSamples; ++i)
                {
                    float s = clipHighPass (samples[i]) * driveGain;
                    s = fuzzStage1 (s + bias) - offset;             // starved first transistor, asymmetric
                    s = 0.6f * fuzzStage2 (s * 4.0f / 0.6f);        // second stage slams the rails
                    s = slewLowPass (s);
                    const float t = lastTone;
                    samples[i] = ((1.0f - t) * toneLowPass (s) + t * toneHighPass (s)) * 0.26f * level;
                }
                break;
            }

            case Type::trebleBoost:
            {
                constexpr float bias = 0.2f;
                const float offset = std::tanh (bias);
                for (int i = 0; i < numSamples; ++i)
                {
                    const float x = samples[i];
                    const float boosted = std::tanh (clipHighPass (x) * driveGain + bias) - offset;
                    samples[i] = toneLowPass (0.3f * x + 0.7f * boosted) * 3.5f * level;
                }
                break;
            }

            case Type::count:
                break;
        }
    }

    void applyTypeFilters()
    {
        switch (type)
        {
            case Type::midBoost:
                clipHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 720.0f, 0.6f));
                cleanHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 90.0f, 0.707f));
                break;
            case Type::bluesDrive:
                clipHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 280.0f, 0.6f));
                break;
            case Type::distortion:
                clipHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 60.0f, 0.707f));
                break;
            case Type::fuzz:
                clipHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 110.0f, 0.707f));
                slewLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, 6500.0f, 0.707f));
                break;
            case Type::trebleBoost:
                clipHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 1100.0f, 0.5f));
                break;
            case Type::count:
                break;
        }
        reset();
    }

    void applyTone (float tone)
    {
        switch (type)
        {
            case Type::midBoost:
                toneLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, 700.0f * std::pow (10.0f, tone * 1.05f), 0.707f));
                break;
            case Type::bluesDrive:
                toneShelf.set (ArrayCoeffs::makeHighShelf (sampleRate, 1400.0f, 0.7f, dbToGain ((tone - 0.5f) * 16.0f)));
                break;
            case Type::distortion:
                toneLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, 550.0f * std::pow (10.0f, tone * 1.3f), 0.707f));
                break;
            case Type::fuzz:
                // Passive LP/HP crossfade: the knob sweeps through a mid scoop.
                toneLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, 480.0f, 0.6f));
                toneHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 1100.0f, 0.6f));
                break;
            case Type::trebleBoost:
                toneLowPass.set (ArrayCoeffs::makeLowPass (sampleRate, 2200.0f * std::pow (10.0f, tone * 0.75f), 0.707f));
                break;
            case Type::count:
                break;
        }
    }

    double sampleRate = 44100.0 * 8; // the oversampled rate
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;
    AdaaTanh diodeClip, fuzzStage1, fuzzStage2; // the high-gain clippers
    Biquad clipHighPass, cleanHighPass, slewLowPass, toneLowPass, toneHighPass, toneShelf;
    Type type = Type::midBoost, currentType = Type::count;
    float drive = 0.3f, driveGain = 1.0f, level = 1.0f, lastTone = -1.0f, lastDrive = -1.0f;
};
} // namespace poman
