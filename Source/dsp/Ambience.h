#pragma once

#include "DspCommon.h"

namespace poman
{
// Ping-pong delay with darkened, thinned repeats.
class PingPongDelay
{
public:
    void prepare (double newSampleRate, int maxBlockSize)
    {
        sampleRate = newSampleRate;
        line.setMaximumDelayInSamples ((int) (sampleRate * 2.1));
        line.prepare ({ sampleRate, (juce::uint32) maxBlockSize, 2 });
        delayMs.reset (sampleRate, 0.12);

        for (auto& f : repeatLowPass)
            f.set (ArrayCoeffs::makeLowPass (sampleRate, 4200.0f, 0.707f));
        for (auto& f : repeatHighPass)
            f.set (ArrayCoeffs::makeHighPass (sampleRate, 140.0f, 0.707f));

        reset();
    }

    void reset()
    {
        line.reset();
        for (auto& f : repeatLowPass)
            f.reset();
        for (auto& f : repeatHighPass)
            f.reset();
    }

    void setParams (float timeMs, float newFeedback, float newMix)
    {
        delayMs.setTargetValue (timeMs);
        feedback = newFeedback;
        mix = newMix;
    }

    // feedInput false: switched off but still ringing out (trails), nothing new enters the line.
    void process (float* left, float* right, int numSamples, bool feedInput = true) noexcept
    {
        for (int i = 0; i < numSamples; ++i)
        {
            const float delaySamples = delayMs.getNextValue() * 0.001f * (float) sampleRate;
            const float wetLeft = line.popSample (0, delaySamples);
            const float wetRight = line.popSample (1, delaySamples);

            const float input = feedInput ? 0.5f * (left[i] + right[i]) : 0.0f;
            line.pushSample (0, input + feedback * repeatLowPass[0] (repeatHighPass[0] (wetRight)));
            line.pushSample (1, feedback * repeatLowPass[1] (repeatHighPass[1] (wetLeft)));

            left[i] += mix * wetLeft;
            right[i] += mix * wetRight;
        }
    }

private:
    double sampleRate = 44100.0;
    juce::dsp::DelayLine<float, juce::dsp::DelayLineInterpolationTypes::Linear> line;
    juce::SmoothedValue<float> delayMs { 380.0f };
    std::array<Biquad, 2> repeatLowPass, repeatHighPass;
    float feedback = 0.3f, mix = 0.25f;
};

// Studio reverb: an 8-line feedback delay network (orthogonal Hadamard mixing, so the loop only
// loses energy through the per-line decay gains), with input diffusion, pre-delay, frequency-
// dependent decay (one-pole damping per line, so highs die first as in a real space) and slow
// delay modulation that keeps the tail from ringing metallic. Room adds discrete early reflections.
class Ambience
{
public:
    enum class Type { room, plate, hall, count };
    static juce::StringArray typeNames() { return { "Room", "Plate", "Hall" }; }

    void prepare (double newSampleRate, int)
    {
        sampleRate = newSampleRate;
        const auto samples = [this] (double ms) { return (int) std::ceil (ms * 0.001 * sampleRate); };
        // Longest settings: hall line 97 ms x 1.4 size, plus modulation; 120 ms pre-delay + reflections.
        for (auto& line : lines)
            line.assign ((size_t) samples (97.0 * 1.4 + 4.0) + 4, 0.0f);
        for (auto& ch : diffusers)
            for (auto& d : ch)
                d.buffer.assign ((size_t) samples (12.0) + 2, 0.0f);
        preDelay.assign ((size_t) samples (160.0) + 2, 0.0f);
        inputHighPass.set (ArrayCoeffs::makeHighPass (sampleRate, 90.0, 0.7));
        updateDesign();
        reset();
    }

    void reset()
    {
        for (auto& line : lines)
            std::fill (line.begin(), line.end(), 0.0f);
        for (auto& ch : diffusers)
            for (auto& d : ch)
                std::fill (d.buffer.begin(), d.buffer.end(), 0.0f), d.pos = 0;
        std::fill (preDelay.begin(), preDelay.end(), 0.0f);
        damping = {};
        linePos = {};
        inputHighPass.reset();
        preDelayPos = 0;
        lfoPhase = 0.0;
    }

    // size 0..1 (dimensions and decay), mix 0..1, tone 0..1 (dark..bright tail), type as above.
    void setParams (float newSize, float newMix, Type newType = Type::plate, float newTone = 0.5f)
    {
        mix = newMix;
        if (newSize != size || (int) newType != type || newTone != tone)
        {
            size = newSize;
            type = (int) newType;
            tone = newTone;
            updateDesign();
        }
    }

    // feedInput false: switched off but still ringing out (trails); the dry signal passes untouched.
    void process (juce::dsp::AudioBlock<float>& block, bool feedInput = true) noexcept
    {
        auto* left = block.getChannelPointer (0);
        auto* right = block.getChannelPointer (1);
        const int n = (int) block.getNumSamples();
        // Same dry/wet law as the first reverb, so existing presets keep their balance.
        const float wet = mix * wetScale, dry = feedInput ? 1.0f - 0.35f * mix : 1.0f;
        const int preSize = (int) preDelay.size();
        const double lfoStep = juce::MathConstants<double>::twoPi * 0.7 / sampleRate;

        for (int i = 0; i < n; ++i)
        {
            const float inL = left[i], inR = right[i];

            // Pre-delay (and early reflection taps read from the same buffer).
            preDelay[(size_t) preDelayPos] = inputHighPass (feedInput ? 0.5f * (inL + inR) : 0.0f);
            const auto tap = [&] (int delaySamples) { return preDelay[(size_t) ((preDelayPos - delaySamples + preSize) % preSize)]; };
            float mono = tap (preDelaySamples);
            float earlyL = 0.0f, earlyR = 0.0f;
            for (int e = 0; e < numEarly; ++e)
            {
                const float r = tap (earlyDelay[(size_t) e]) * earlyGain[(size_t) e];
                (e % 2 == 0 ? earlyL : earlyR) += r;
            }
            preDelayPos = (preDelayPos + 1) % preSize;

            // Input diffusion smears the attack into a dense wash before it enters the loop.
            for (auto& d : diffusers[0])
                mono = d.process (mono, diffusion);

            // Read the 8 lines (lines 0-3 modulated by quadrature LFOs), damp, mix, feed back.
            lfoPhase += lfoStep;
            if (lfoPhase > juce::MathConstants<double>::twoPi)
                lfoPhase -= juce::MathConstants<double>::twoPi;
            std::array<float, numLines> out {};
            for (int l = 0; l < numLines; ++l)
            {
                auto& line = lines[(size_t) l];
                const int len = (int) line.size();
                double d = lineDelay[(size_t) l];
                if (l < 4)
                    d += modDepth * std::sin (lfoPhase + l * juce::MathConstants<double>::halfPi);
                const double readPos = (double) linePos[(size_t) l] - d;
                const int i0 = (int) std::floor (readPos);
                const float frac = (float) (readPos - i0);
                const float a = line[(size_t) ((i0 % len + len) % len)], b = line[(size_t) (((i0 + 1) % len + len) % len)];
                float y = a + frac * (b - a);
                auto& z = damping[(size_t) l];
                z = y + dampCoeff[(size_t) l] * (z - y); // one-pole low-pass: highs decay faster
                out[(size_t) l] = z * lineGain[(size_t) l];
            }

            const auto mixed = hadamard (out);
            for (int l = 0; l < numLines; ++l)
            {
                auto& line = lines[(size_t) l];
                auto& pos = linePos[(size_t) l];
                line[(size_t) pos] = mixed[(size_t) l] + (l % 2 == 0 ? mono : -mono) * 0.5f;
                pos = pos + 1 < (int) line.size() ? pos + 1 : 0;
            }

            // Decorrelated stereo taps: different sign patterns of the line outputs.
            const float tailL = out[0] + out[2] - out[4] + out[6] + 0.5f * (out[1] - out[5]);
            const float tailR = out[1] + out[3] - out[5] + out[7] + 0.5f * (out[2] - out[6]);
            left[i] = dry * inL + wet * (tailL + earlyL);
            right[i] = dry * inR + wet * (tailR + earlyR);
        }
    }

private:
    static constexpr int numLines = 8, numEarly = 6;

    struct Diffuser
    {
        std::vector<float> buffer;
        int pos = 0, length = 1;
        float process (float x, float g) noexcept
        {
            const float delayed = buffer[(size_t) pos];
            const float v = x + g * delayed;
            buffer[(size_t) pos] = v;
            pos = (pos + 1) % length;
            return delayed - g * v;
        }
    };

    static std::array<float, numLines> hadamard (std::array<float, numLines> x) noexcept
    {
        for (int h = 1; h < numLines; h *= 2)
            for (int i = 0; i < numLines; i += 2 * h)
                for (int j = i; j < i + h; ++j)
                {
                    const float a = x[(size_t) j], b = x[(size_t) (j + h)];
                    x[(size_t) j] = a + b;
                    x[(size_t) (j + h)] = a - b;
                }
        for (auto& v : x)
            v *= 0.35355339f; // 1/sqrt(8): orthonormal
        return x;
    }

    void updateDesign()
    {
        if (sampleRate <= 0.0 || lines[0].empty())
            return;

        // Per type: line lengths (ms, mutually prime-ish), RT60 range, pre-delay, diffusion, modulation.
        struct Design { std::array<double, numLines> ms; double rtMin, rtMax, preMs, diffusion, modMs, gain; };
        static constexpr Design designs[] {
            { { 11.3, 13.7, 16.1, 18.9, 21.4, 24.2, 27.3, 30.8 }, 0.35, 1.4, 4.0, 0.55, 0.12, 2.0 },  // room
            { { 19.1, 22.7, 26.3, 29.9, 33.7, 37.1, 41.3, 45.7 }, 1.0, 4.0, 10.0, 0.72, 0.25, 2.33 }, // plate
            { { 37.9, 44.3, 51.7, 58.1, 66.7, 74.3, 85.1, 97.0 }, 1.6, 5.5, 28.0, 0.65, 0.55, 1.9 },  // hall
        };
        const auto& d = designs[juce::jlimit (0, 2, type)];
        const double scale = 0.6 + 0.8 * size;
        const double rt60 = d.rtMin * std::pow (d.rtMax / d.rtMin, (double) size);
        // TONE: frequency where the decay is twice as fast, 1.5 kHz (dark) .. 9 kHz (bright).
        const double dampHz = 1500.0 * std::pow (6.0, (double) tone) * (type == (int) Type::plate ? 1.5 : 1.0);

        for (int l = 0; l < numLines; ++l)
        {
            const double delaySamples = d.ms[(size_t) l] * scale * 0.001 * sampleRate;
            lineDelay[(size_t) l] = delaySamples;
            lineGain[(size_t) l] = (float) std::pow (10.0, -3.0 * delaySamples / (rt60 * sampleRate));
            dampCoeff[(size_t) l] = (float) std::exp (-juce::MathConstants<double>::twoPi * dampHz / sampleRate);
        }
        modDepth = d.modMs * 0.001 * sampleRate;
        diffusion = (float) d.diffusion;
        wetScale = (float) d.gain * 0.35f;
        preDelaySamples = (int) (d.preMs * (0.5 + size) * 0.001 * sampleRate);

        static constexpr double diffuserMs[] { 4.7, 3.6, 12.0 * 0.85, 9.3 };
        for (auto& ch : diffusers)
            for (size_t k = 0; k < ch.size(); ++k)
            {
                ch[k].length = juce::jlimit (1, (int) ch[k].buffer.size(), (int) (diffuserMs[k] * 0.001 * sampleRate));
                ch[k].pos %= ch[k].length;
            }

        // Early reflections only for the room (walls, floor, ceiling), quieter for small sizes.
        static constexpr double earlyMs[numEarly] { 3.1, 5.3, 7.9, 11.2, 14.6, 19.3 };
        static constexpr float earlyLevel[numEarly] { 0.5f, 0.42f, 0.36f, 0.3f, 0.24f, 0.18f };
        for (int e = 0; e < numEarly; ++e)
        {
            earlyDelay[(size_t) e] = preDelaySamples + (int) (earlyMs[e] * scale * 0.001 * sampleRate);
            earlyGain[(size_t) e] = type == (int) Type::room ? earlyLevel[e] : 0.0f;
        }
    }

    double sampleRate = 48000.0;
    std::array<std::vector<float>, numLines> lines;
    std::array<double, numLines> lineDelay {};
    std::array<float, numLines> lineGain {}, dampCoeff {}, damping {};
    std::array<std::array<Diffuser, 4>, 1> diffusers;
    std::vector<float> preDelay;
    std::array<int, numLines> linePos {};
    std::array<int, numEarly> earlyDelay {};
    std::array<float, numEarly> earlyGain {};
    Biquad inputHighPass;
    int preDelayPos = 0, preDelaySamples = 0;
    double modDepth = 0.0, lfoPhase = 0.0;
    float diffusion = 0.6f, wetScale = 0.3f;
    float size = 0.5f, mix = 0.2f, tone = 0.5f;
    int type = (int) Type::plate;
};
} // namespace poman
