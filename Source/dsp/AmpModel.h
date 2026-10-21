#pragma once

#include "DspCommon.h"
#include "ToneStack.h"
#include "Tubes.h"
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

// Amp with several voicings: cascaded 12AX7 triode stages (Koren model, solved into tables), a passive
// tone stack, and a push-pull power pair (EL34 / 6L6 / EL84 / 6V6) in a feedback loop with sag and an
// output transformer; all nonlinear parts run 4x oversampled.
class AmpModel
{
public:
    static constexpr int maxStages = 5;

    // Indices 0-2 are the original Clean/Rhythm/Lead channels; keep the order so saved states stay valid.
    static juce::StringArray modelNames()
    {
        return { "Glass Clean", "Modern Rhythm", "Modern Lead", "US Clean", "Chime Class A",
                 "Brit Crunch", "Hot Rod Lead", "Hi-Gain Rect", "Tight Djent", "Lead 51", "Cali Lead", "Liquid Lead",
                 "Tweed Deluxe", "Plexi 50", "Bassman 4x10", "Smooth Overdrive", "Brit 80s Lead" };
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
        gridAttack = onePoleCoeff (osRate, 1.0f);
        gridRelease = onePoleCoeff (osRate, 35.0f);
        transformerCoeff = (float) (1.0 - std::exp (-juce::MathConstants<double>::twoPi * 80.0 / osRate));
        tubes::PreampTriode::table(); // solved once, here rather than on the audio thread
        tubes::PushPull::table (tubes::PowerTube::el34);

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

        for (auto* f : { &inputHighPass, &preShape, &bright, &voiceFilter, &presenceFilter, &depthFilter,
                         &feedbackPresence, &feedbackDepth, &speakerResonance, &speakerInductance })
            f->reset();
        gridEnvelope = {};
        lastOutput = 0.0f;
        transformerLow = 0.0f;
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
        // PRESENCE / DEPTH mainly take treble / bass out of the negative feedback (as on the amps), so their
        // effect depends on the model's feedback and on how hard the power stage works. A gentle post shelf
        // keeps them useful on models without feedback (Chime Class A).
        if (force || params.presence != cached.presence)
        {
            feedbackPresence.set (ArrayCoeffs::makeHighShelf (osRate, 3500.0, 0.7, dbToGain (-1.8f * params.presence)));
            presenceFilter.set (ArrayCoeffs::makeHighShelf (sampleRate, v.presenceHz, 0.7f, dbToGain (knobToDb (params.presence, 4.0f))));
        }
        if (force || params.depth != cached.depth)
        {
            feedbackDepth.set (ArrayCoeffs::makeLowShelf (osRate, 90.0, 0.7, dbToGain (-1.8f * params.depth)));
            depthFilter.set (ArrayCoeffs::makePeakFilter (sampleRate, 85.0f, 1.0f, dbToGain (knobToDb (params.depth, 4.0f))));
        }

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
        const auto& pw = power[(size_t) voicingChannel];
        const auto& triode = tubes::PreampTriode::table();
        const auto& pair = tubes::PushPull::table (powerTubes[(size_t) voicingChannel]);
        const float asymmetryOffset = pair (pw.asymmetry);
        const float outputScale = 0.4f * (1.0f + pw.feedback) * dbToGain (pw.levelDb);
        constexpr float voltsPerUnit = tubes::PreampTriode::voltsPerUnit;
        const float blockingVolts = pw.gridBlocking * voltsPerUnit;

        for (int i = 0; i < numUp; ++i)
        {
            float s = d[i];
            const float g = stageGain.getNextValue();

            for (int st = 0; st < v.stages; ++st)
            {
                // The stage's grid voltage: its operating point plus the coupled signal.
                const float vin = stageBias + voltsPerUnit * (s * g);

                // Grid conduction: when the grid goes positive it charges the coupling cap, which shifts
                // the stage colder for a few tens of ms, so a hard pick is squashed and then blooms back.
                const float conduction = juce::jmax (0.0f, vin);
                auto& env = gridEnvelope[(size_t) st];
                env = conduction + (conduction > env ? gridAttack : gridRelease) * (env - conduction);
                const float shift = blockingVolts * env / (1.0f + env); // bounded
                const float operating = stageBias - shift;

                // Plate swing from the operating point, scaled so the small-signal gain is one. Each
                // common-cathode stage inverts, so the next one clips the other half of the wave first.
                const float out = (triode (operating) - triode (vin - shift)) * stageNorm;
                s = (st & 1) != 0 ? -out : out;
                s = stageLowPass[(size_t) st] (stageHighPass[(size_t) st] (s));
            }

            s = voiceFilter (toneStack (s * v.outputTrim));

            // Power stage: slightly asymmetric output pair inside a global negative-feedback loop (one
            // oversampled sample of loop delay; stable because loop gain = feedback * tanh' < 1), with
            // supply sag. Less feedback lets the speaker's impedance curve show through, as on real amps.
            const float pd = powerDrive.getNextValue();
            const float fed = pw.feedback * feedbackDepth (feedbackPresence (lastOutput));
            const float e = s * pd / (1.0f + v.sag * sagEnvelope) - fed;
            const float tube = pair (e + pw.asymmetry) - asymmetryOffset; // push-pull pair, slightly unbalanced

            // Output transformer: core flux is the integral of the voltage, so the lows saturate first.
            // The band below ~80 Hz is softly limited and recombined; it sits inside the loop like the real one.
            transformerLow += transformerCoeff * (tube - transformerLow);
            const float y = tube - transformerLow + softLimit (transformerLow * transformerDrive) / transformerDrive;
            lastOutput = y;

            const float level = std::abs (y);
            sagEnvelope = level + (level > sagEnvelope ? sagAttack : sagRelease) * (sagEnvelope - level);

            d[i] = speakerInductance (speakerResonance (y)) * outputScale / pair (pd);
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
    static constexpr std::array<Voicing, 17> voicings { {
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
        // Liquid Lead: virtuoso lead voice. Four stages with a firm low end and a vocal 1.2 kHz push
        // before the clipping, a soft top (no fizz) and a little sag so long notes breathe and sing.
        { 4, 100.0f, 1200.0f, 5.0f, 1.5f, 12.0f, 700.0f, 0.12f, 60.0f, 6800.0f, 0.6f, 1100.0f, 0.8f, 2.0f, 0.35f, 4300.0f },
        // Tweed Deluxe: small '50s combo. Two stages, warm mid hump, early and very touch-sensitive
        // breakup, big sag from the tube rectifier; the 6V6 pair runs without feedback.
        { 2, 60.0f, 700.0f, 3.0f, 2.0f, 3.0f, 40.0f, 0.10f, 20.0f, 9000.0f, 0.9f, 800.0f, 0.7f, 2.0f, 0.7f, 3500.0f },
        // Plexi 50: British non-master-volume head. Bright cap, 1 kHz bark, crunch that comes from
        // turning it up; EL34s with light feedback.
        { 3, 80.0f, 1200.0f, 3.0f, 4.0f, 2.5f, 40.0f, 0.08f, 35.0f, 9000.0f, 0.8f, 1000.0f, 0.8f, 1.5f, 0.4f, 4000.0f },
        // Bassman 4x10: tweed bass amp that became a blues-rock classic. Fat lows, smooth breakup, 6L6s.
        { 2, 50.0f, 600.0f, 2.0f, 2.0f, 2.5f, 30.0f, 0.08f, 20.0f, 10000.0f, 0.9f, 700.0f, 0.7f, 1.0f, 0.5f, 4500.0f },
        // Smooth Overdrive: boutique overdrive-channel style. A clean preamp feeding cascaded drive
        // stages, thick 700 Hz mids, silky top and little sag, so it follows the pick closely.
        { 4, 85.0f, 700.0f, 4.0f, 1.0f, 8.0f, 300.0f, 0.10f, 40.0f, 6000.0f, 0.65f, 700.0f, 0.8f, 2.5f, 0.3f, 4000.0f },
        // Brit 80s Lead: master-volume British lead of the '80s. A cold-biased clipping stage, gritty
        // 1.6 kHz upper mids, bright and tight; EL34s.
        { 3, 100.0f, 1600.0f, 4.0f, 3.0f, 8.0f, 250.0f, 0.22f, 50.0f, 8000.0f, 0.7f, 900.0f, 0.9f, 1.0f, 0.3f, 4300.0f },
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

        // Speaker load: the less feedback, the more the cabinet's 100 Hz resonance and voice-coil rise show.
        const auto& pw = power[(size_t) channel];
        speakerResonance.set (ArrayCoeffs::makePeakFilter (osRate, 100.0, 1.4, dbToGain ((1.0f - pw.feedback) * 6.0f)));
        transformerDrive = 0.6f + 1.6f * (1.0f - pw.feedback); // feedback also linearises the transformer
        speakerInductance.set (ArrayCoeffs::makeHighShelf (osRate, 2500.0, 0.7, dbToGain ((1.0f - pw.feedback) * 3.0f)));

        // Preamp operating point (volts) and the gain normalisation at that point.
        stageBias = tubes::PreampTriode::operatingPoint (v.bias);
        const auto& triode = tubes::PreampTriode::table();
        stageNorm = 1.0f / ((triode (stageBias - 0.05f) - triode (stageBias + 0.05f)) / 0.1f * tubes::PreampTriode::voltsPerUnit);

        // Circuit family of the tone stack, then the model's own fixed mid voicing on top.
        toneStack.setComponents (fenderStack[(size_t) channel] ? ToneStack::fender : ToneStack::marshall);
        voiceFilter.set (ArrayCoeffs::makePeakFilter (osRate, v.midHz, v.midQ, dbToGain (v.midOffsetDb)));
    }

    // Which models use the Fender-family stack (the Mark-style stack derives from it); the rest are Marshall-style.
    static constexpr std::array<bool, 17> fenderStack { true,  false, false, true,  true, false,
                                                        false, false, false, false, true,  false,
                                                        true,  false, true,  true,  false };

    // Output tubes per model (EL84 / 6V6 pairs are cathode-biased class A, EL34 / 6L6 class AB).
    static constexpr std::array<tubes::PowerTube, 17> powerTubes {
        tubes::PowerTube::sixL6, tubes::PowerTube::sixL6, tubes::PowerTube::el34, tubes::PowerTube::sixL6, // Glass, Modern Rhythm, Modern Lead, US
        tubes::PowerTube::el84,  tubes::PowerTube::el34,  tubes::PowerTube::el34, tubes::PowerTube::sixL6, // Chime, Brit, Hot Rod, Rect
        tubes::PowerTube::sixL6, tubes::PowerTube::sixL6, tubes::PowerTube::sixL6, tubes::PowerTube::el34, // Djent, Lead 51, Cali, Liquid
        tubes::PowerTube::sixV6, tubes::PowerTube::el34,  tubes::PowerTube::sixL6, tubes::PowerTube::sixL6, // Tweed, Plexi, Bassman, Smooth OD
        tubes::PowerTube::el34,                                                                              // Brit 80s
    };

    static constexpr size_t oversamplingOrder = 2;

    double sampleRate = 44100.0, osRate = 176400.0;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampling;

    AmpParams params, cached;
    int voicingChannel = -1;

    Biquad inputHighPass, preShape, bright;
    std::array<Biquad, maxStages> stageHighPass, stageLowPass;
    // Power section and dynamics per model: global feedback (0 = none, like a class-A AC-style amp),
    // output-pair asymmetry (even harmonics), how strongly hard picks bias the preamp colder, and the
    // output trim that keeps each model at its previous level (measured at gain 6, master 5).
    struct Power { float feedback, asymmetry, gridBlocking, levelDb; };
    static constexpr std::array<Power, 17> power { {
        { 0.50f, 0.05f, 0.15f, -4.4f }, // Glass Clean
        { 0.50f, 0.04f, 0.15f, -5.8f }, // Modern Rhythm
        { 0.45f, 0.04f, 0.20f, -7.2f }, // Modern Lead
        { 0.55f, 0.05f, 0.15f, -4.2f }, // US Clean
        { 0.00f, 0.08f, 0.30f, -6.3f }, // Chime Class A: no feedback, lots of interaction
        { 0.35f, 0.07f, 0.30f, -5.4f }, // Brit Crunch
        { 0.40f, 0.05f, 0.25f, -6.7f }, // Hot Rod Lead
        { 0.45f, 0.05f, 0.25f, -8.2f }, // Hi-Gain Rect
        { 0.60f, 0.03f, 0.05f, -6.5f }, // Tight Djent: tight, fast, little push-back
        { 0.50f, 0.04f, 0.15f, -7.7f }, // Lead 51
        { 0.50f, 0.04f, 0.10f, -7.1f }, // Cali Lead
        { 0.45f, 0.05f, 0.20f, -6.8f }, // Liquid Lead: some push-back on hard picks, then it blooms
        { 0.00f, 0.07f, 0.30f, -6.4f }, // Tweed Deluxe: no feedback, lots of push-back
        { 0.30f, 0.06f, 0.25f, -5.6f }, // Plexi 50
        { 0.35f, 0.06f, 0.25f, -7.1f }, // Bassman 4x10
        { 0.50f, 0.05f, 0.20f, -7.0f }, // Smooth Overdrive
        { 0.40f, 0.05f, 0.25f, -6.9f }, // Brit 80s Lead
    } };

    ToneStack toneStack;
    Biquad voiceFilter, presenceFilter, depthFilter;
    Biquad feedbackPresence, feedbackDepth, speakerResonance, speakerInductance;
    std::array<float, maxStages> gridEnvelope {};
    float gridAttack = 0.0f, gridRelease = 0.0f, lastOutput = 0.0f;
    float transformerCoeff = 0.0f, transformerDrive = 1.0f, transformerLow = 0.0f;
    float stageBias = -1.5f, stageNorm = 1.0f; // preamp grid operating point (V) and 1 / (gain * volts per unit)

    // tanh-like Pade approximation, exact at the clamp points; cheap enough for per-sample use.
    static float softLimit (float x) noexcept
    {
        x = juce::jlimit (-3.0f, 3.0f, x);
        return x * (27.0f + x * x) / (27.0f + 9.0f * x * x);
    }

    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> stageGain { 1.0f }, powerDrive { 1.0f };
    float sagAttack = 0.0f, sagRelease = 0.0f, sagEnvelope = 0.0f;
};
} // namespace poman
