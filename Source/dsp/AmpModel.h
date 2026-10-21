#pragma once

#include "DspCommon.h"
#include "ToneStack.h"
#include <memory>

namespace poman
{
struct AmpParams
{
    int channel = 1; // amp model index, see AmpModel::modelNames()
    // Knobs are 0..10.
    float gain = 6.0f, bass = 5.0f, mid = 5.0f, treble = 5.0f;
    float presence = 5.0f, depth = 5.0f, master = 5.0f;
};

// Amp with several voicings: cascaded asymmetric triode-like stages, a per-model tone stack and a
// saggy power stage, all nonlinear parts running 4x oversampled.
class AmpModel
{
public:
    static constexpr int maxStages = 5;

    // Indices 0-2 are the original Clean/Rhythm/Lead channels; keep the order so saved states stay valid.
    static juce::StringArray modelNames()
    {
        return { "Glass Clean", "Modern Rhythm", "Modern Lead", "US Clean", "Chime Class A",
                 "Brit Crunch", "Hot Rod Lead", "Hi-Gain Rect", "Tight Djent", "Lead 51", "Cali Lead" };
    }

    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        oversampling = std::make_unique<juce::dsp::Oversampling<float>> (
            1, oversamplingOrder, juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR, true, false); // max-quality filters: steep anti-alias stopband
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

        for (auto* f : { &inputHighPass, &preShape, &bright, &voiceFilter, &presenceFilter, &depthFilter })
            f->reset();
        toneStack.reset();

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
        const auto channel = juce::jlimit (0, (int) voicings.size() - 1, params.channel);

        // A new model brings its own tone stack frequencies, so every filter is recomputed.
        if (force || channel != voicingChannel)
        {
            applyVoicing (channel);
            force = true;
        }

        const auto& v = voicings[(size_t) channel];
        const float total = v.minGain * std::pow (v.maxGain / v.minGain, params.gain / 10.0f);
        stageGain.setTargetValue (std::pow (total, 1.0f / (float) v.stages));
        powerDrive.setTargetValue (0.35f * std::pow (10.0f, params.master / 10.0f * 1.25f));

        // BASS / MID / TREBLE drive the passive tone stack between preamp and power amp, as in the amps.
        toneStack.setKnobs (osRate, params.treble, params.mid, params.bass);
        if (force || params.presence != cached.presence)
            presenceFilter.set (ArrayCoeffs::makeHighShelf (sampleRate, v.presenceHz, 0.7f, dbToGain (knobToDb (params.presence, 8.0f))));
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

            s = voiceFilter (toneStack (s * v.outputTrim));

            const float pd = powerDrive.getNextValue();
            const float p = s * pd;
            const float level = std::abs (p);
            const float sagCoeff = level > sagEnvelope ? sagAttack : sagRelease;
            sagEnvelope = level + sagCoeff * (sagEnvelope - level);

            d[i] = std::tanh (p / (1.0f + v.sag * sagEnvelope)) / std::tanh (pd) * 0.4f;
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
        // Tone stack: shelf/peak frequencies, inherent mid voicing, then power-amp sag and presence corner.
        float midHz, midQ, midOffsetDb; // fixed mid voicing on top of the tone stack
        float sag, presenceHz;
    };

    // stages | in HP, pre-shape Hz/dB, bright dB | gain min/max, bias | coupling, miller, trim
    //        | mid Hz, mid Q, mid offset dB | sag, presence Hz
    static constexpr std::array<Voicing, 11> voicings { {
        // Glass Clean: two glassy stages, firm power amp.
        { 2, 50.0f, 800.0f, 0.0f, 4.0f, 1.5f, 8.0f, 0.06f, 25.0f, 12000.0f, 0.9f, 650.0f, 0.9f, 0.0f, 0.3f, 4500.0f },
        // Modern Rhythm: tight, scooped-ready crunch.
        { 3, 115.0f, 900.0f, 3.0f, 2.0f, 6.0f, 90.0f, 0.15f, 60.0f, 9000.0f, 0.7f, 650.0f, 0.9f, 0.0f, 0.3f, 4500.0f },
        // Modern Lead: four stages, mid-forward saturation.
        { 4, 95.0f, 750.0f, 5.0f, 1.0f, 20.0f, 600.0f, 0.22f, 45.0f, 7000.0f, 0.6f, 650.0f, 0.9f, 0.0f, 0.3f, 4500.0f },
        // US Clean: blackface-style scooped mids, sparkly top, spongy sag.
        { 2, 60.0f, 400.0f, -3.0f, 5.0f, 1.2f, 6.0f, 0.04f, 20.0f, 13000.0f, 1.25f, 450.0f, 0.7f, -4.0f, 0.5f, 5000.0f },
        // Chime Class A: upper-mid jangle, early breakup, lots of compression.
        { 2, 80.0f, 2500.0f, 4.0f, 3.0f, 2.0f, 25.0f, 0.12f, 35.0f, 10000.0f, 0.85f, 900.0f, 0.8f, 0.0f, 0.6f, 3800.0f },
        // Brit Crunch: non-master-volume style, 1 kHz bark.
        { 3, 90.0f, 1000.0f, 4.0f, 2.0f, 3.0f, 45.0f, 0.10f, 40.0f, 8000.0f, 0.75f, 800.0f, 0.9f, 2.0f, 0.45f, 4200.0f },
        // Hot Rod Lead: modded British lead, pushed mids and singing sustain.
        { 4, 110.0f, 850.0f, 6.0f, 1.5f, 15.0f, 400.0f, 0.20f, 55.0f, 6500.0f, 0.6f, 750.0f, 0.9f, 3.0f, 0.3f, 4000.0f },
        // Hi-Gain Rect: loose huge lows, scooped mids, saggy rectifier feel.
        { 4, 70.0f, 500.0f, -2.0f, 2.0f, 25.0f, 700.0f, 0.25f, 30.0f, 7500.0f, 0.55f, 600.0f, 0.7f, -4.0f, 0.5f, 4800.0f },
        // Tight Djent: very tight low end, fast attack, almost no sag.
        { 4, 160.0f, 1200.0f, 3.0f, 3.0f, 30.0f, 800.0f, 0.18f, 90.0f, 9000.0f, 0.55f, 1000.0f, 1.0f, 0.0f, 0.15f, 5500.0f },
        // Lead 51: 5150-style lead channel. Five cascaded cold-clipping stages, tight input, a hard
        // 1.5 kHz push with the fizz rolled off, slightly scooped stack and a firm power section.
        { 5, 105.0f, 1500.0f, 4.0f, 2.0f, 40.0f, 1200.0f, 0.30f, 70.0f, 6000.0f, 0.5f, 700.0f, 0.8f, -2.0f, 0.25f, 4000.0f },
        // Cali Lead: California boutique lead style (Mark-type). Five cascaded stages fed by a very tight,
        // mid-focused front end; smooth singing top, little sag. Made to pair with a V-shaped graphic EQ.
        { 5, 120.0f, 900.0f, 5.0f, 3.0f, 30.0f, 900.0f, 0.22f, 80.0f, 6500.0f, 0.55f, 650.0f, 0.8f, -1.0f, 0.2f, 4200.0f },
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

        // Circuit family of the tone stack, then the model's own fixed mid voicing on top.
        toneStack.setComponents (fenderStack[(size_t) channel] ? ToneStack::fender : ToneStack::marshall);
        voiceFilter.set (ArrayCoeffs::makePeakFilter (osRate, v.midHz, v.midQ, dbToGain (v.midOffsetDb)));
    }

    // Which models use the Fender-family stack (the Mark-style stack derives from it); the rest are Marshall-style.
    static constexpr std::array<bool, 11> fenderStack { true,  false, false, true,  true, false,
                                                        false, false, false, false, true };

    static constexpr size_t oversamplingOrder = 2;

    double sampleRate = 44100.0, osRate = 176400.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    AmpParams params, cached;
    int voicingChannel = -1;

    Biquad inputHighPass, preShape, bright;
    std::array<Biquad, maxStages> stageHighPass, stageLowPass;
    ToneStack toneStack;
    Biquad voiceFilter, presenceFilter, depthFilter;

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> stageGain { 1.0f }, powerDrive { 1.0f };
    float sagAttack = 0.0f, sagRelease = 0.0f, sagEnvelope = 0.0f;
};
} // namespace poman
