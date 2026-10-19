#pragma once

#include "DspCommon.h"
#include <memory>

namespace abyssal
{
struct AmpParams
{
    int channel = 1;
    // Knobs are 0..10.
    float gain = 6.0f, bass = 5.0f, mid = 5.0f, treble = 5.0f;
    float presence = 5.0f, depth = 5.0f, master = 5.0f;
};

// Three-channel amp: cascaded asymmetric triode-like stages, a tone stack and a saggy power stage,
// all nonlinear parts running 4x oversampled.
class AmpModel
{
public:
    static constexpr int maxStages = 4;

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1, oversamplingOrder, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, false, false);
        oversampling->initProcessing ((size_t) maxBlockSize);
        osRate = sampleRate * (double) (1 << oversamplingOrder);

        stageGain.reset (osRate, 0.05);
        powerDrive.reset (osRate, 0.05);
        sagAttack = onePoleCoeff (osRate, 4.0f);
        sagRelease = onePoleCoeff (osRate, 120.0f);

        cached = {};
        cached.channel = -1;
        voicingChannel = -1;
        setParams (params, true);
        reset();
    }

    void reset()
    {
        if (oversampling != nullptr)
            oversampling->reset();

        for (auto* f : { &inputHighPass, &preShape, &bright, &bassFilter, &midFilter, &trebleFilter, &presenceFilter, &depthFilter })
            f->reset();

        for (int s = 0; s < maxStages; ++s)
        {
            stageHighPass[(size_t) s].reset();
            stageLowPass[(size_t) s].reset();
        }

        sagEnvelope = 0.0f;
        stageGain.setCurrentAndTargetValue (stageGain.getTargetValue());
        powerDrive.setCurrentAndTargetValue (powerDrive.getTargetValue());
    }

    int getLatencySamples() const
    {
        return oversampling != nullptr ? (int) std::round (oversampling->getLatencyInSamples()) : 0;
    }

    void setParams (const AmpParams& newParams, bool force = false)
    {
        params = newParams;
        const auto channel = juce::jlimit (0, 2, params.channel);

        if (force || channel != voicingChannel)
            applyVoicing (channel);

        const auto& v = voicings[(size_t) channel];
        const float total = v.minGain * std::pow (v.maxGain / v.minGain, params.gain / 10.0f);
        stageGain.setTargetValue (std::pow (total, 1.0f / (float) v.stages));
        powerDrive.setTargetValue (0.35f * std::pow (10.0f, params.master / 10.0f * 1.25f));

        if (force || params.bass != cached.bass)
            bassFilter.set (ArrayCoeffs::makeLowShelf (osRate, 110.0f, 0.7f, dbToGain (knobToDb (params.bass, 12.0f))));
        if (force || params.mid != cached.mid)
            midFilter.set (ArrayCoeffs::makePeakFilter (osRate, 650.0f, 0.9f, dbToGain (knobToDb (params.mid, 12.0f))));
        if (force || params.treble != cached.treble)
            trebleFilter.set (ArrayCoeffs::makeHighShelf (osRate, 3000.0f, 0.7f, dbToGain (knobToDb (params.treble, 12.0f))));
        if (force || params.presence != cached.presence)
            presenceFilter.set (ArrayCoeffs::makeHighShelf (sampleRate, 4500.0f, 0.7f, dbToGain (knobToDb (params.presence, 8.0f))));
        if (force || params.depth != cached.depth)
            depthFilter.set (ArrayCoeffs::makePeakFilter (sampleRate, 85.0f, 1.0f, dbToGain (knobToDb (params.depth, 8.0f))));

        cached = params;
    }

    void process (juce::dsp::AudioBlock<float>& block) noexcept
    {
        auto* x = block.getChannelPointer (0);
        const auto numSamples = (int) block.getNumSamples();

        for (int i = 0; i < numSamples; ++i)
            x[i] = bright (preShape (inputHighPass (x[i])));

        auto upBlock = oversampling->processSamplesUp (block);
        auto* d = upBlock.getChannelPointer (0);
        const auto numUp = (int) upBlock.getNumSamples();

        const auto& v = voicings[(size_t) voicingChannel];
        const float biasOffset = std::tanh (v.bias);

        for (int i = 0; i < numUp; ++i)
        {
            float s = d[i];
            const float g = stageGain.getNextValue();

            for (int st = 0; st < v.stages; ++st)
            {
                s = std::tanh (s * g + v.bias) - biasOffset;
                s = stageLowPass[(size_t) st] (stageHighPass[(size_t) st] (s));
            }

            s = trebleFilter (midFilter (bassFilter (s * v.outputTrim)));

            const float pd = powerDrive.getNextValue();
            const float p = s * pd;
            const float level = std::abs (p);
            const float sagCoeff = level > sagEnvelope ? sagAttack : sagRelease;
            sagEnvelope = level + sagCoeff * (sagEnvelope - level);

            d[i] = std::tanh (p / (1.0f + 0.3f * sagEnvelope)) / std::tanh (pd) * 0.4f;
        }

        oversampling->processSamplesDown (block);

        for (int i = 0; i < numSamples; ++i)
            x[i] = depthFilter (presenceFilter (x[i]));
    }

private:
    struct Voicing
    {
        int stages;
        float inputHighPassHz, preShapeHz, preShapeDb, brightDb;
        float minGain, maxGain, bias;
        float couplingHz, millerHz, outputTrim;
    };

    // Clean: glassy two-stage. Rhythm: tight, scooped-ready crunch. Lead: four stages, mid-forward saturation.
    static constexpr std::array<Voicing, 3> voicings { {
        { 2, 50.0f, 800.0f, 0.0f, 4.0f, 1.5f, 8.0f, 0.06f, 25.0f, 12000.0f, 0.9f },
        { 3, 115.0f, 900.0f, 3.0f, 2.0f, 6.0f, 90.0f, 0.15f, 60.0f, 9000.0f, 0.7f },
        { 4, 95.0f, 750.0f, 5.0f, 1.0f, 20.0f, 600.0f, 0.22f, 45.0f, 7000.0f, 0.6f },
    } };

    void applyVoicing (int channel)
    {
        voicingChannel = channel;
        const auto& v = voicings[(size_t) channel];
        inputHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, v.inputHighPassHz, 0.707f));
        preShape.set (ArrayCoeffs::makePeakFilter (sampleRate, v.preShapeHz, 0.8f, dbToGain (v.preShapeDb)));
        bright.set (ArrayCoeffs::makeHighShelf (sampleRate, 1800.0f, 0.7f, dbToGain (v.brightDb)));

        for (int s = 0; s < maxStages; ++s)
        {
            stageHighPass[(size_t) s].set (ArrayCoeffs::makeHighPass (osRate, v.couplingHz, 0.6f));
            stageLowPass[(size_t) s].set (ArrayCoeffs::makeLowPass (osRate, v.millerHz, 0.6f));
        }
    }

    static constexpr size_t oversamplingOrder = 2;

    double sampleRate = 44100.0, osRate = 176400.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    AmpParams params, cached;
    int voicingChannel = -1;

    Biquad inputHighPass, preShape, bright;
    std::array<Biquad, maxStages> stageHighPass, stageLowPass;
    Biquad bassFilter, midFilter, trebleFilter, presenceFilter, depthFilter;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> stageGain { 1.0f }, powerDrive { 1.0f };
    float sagAttack = 0.0f, sagRelease = 0.0f, sagEnvelope = 0.0f;
};
} // namespace abyssal
