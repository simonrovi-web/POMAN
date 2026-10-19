#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <cmath>
#include <vector>

namespace poman
{
// Runs a mono process at another sample rate inside a host-rate stream (e.g. a NAM model trained
// at 48 kHz on a 44.1 kHz interface). Host samples are resampled to the inner rate, processed,
// and resampled back. The number of inner samples per block follows the exact long-run ratio, so
// the two FIFOs never drift; each is primed with a few zeros so the interpolators never run dry.
// The resulting fixed latency is measured with an impulse in prepare().
template <typename Interpolator>
class ResampledProcess
{
public:
    void prepare (double newHostRate, double newInnerRate, int maxHostBlock)
    {
        hostRate = newHostRate;
        innerRate = newInnerRate;
        ratio = innerRate / hostRate;
        active = std::abs (ratio - 1.0) > 1.0e-9;
        maxInnerBlock = (int) std::ceil (maxHostBlock * ratio) + 4;

        upFifo.assign ((size_t) (maxHostBlock + pad + 16), 0.0f);
        downFifo.assign ((size_t) (maxInnerBlock + pad + 16), 0.0f);
        inner.assign ((size_t) maxInnerBlock, 0.0f);

        latency = active ? measureLatency (maxHostBlock) : 0;
        reset();
    }

    void reset()
    {
        std::fill (upFifo.begin(), upFifo.end(), 0.0f);
        std::fill (downFifo.begin(), downFifo.end(), 0.0f);
        upCount = downCount = pad;
        hostTotal = innerTotal = 0;
        upInterpolator.reset();
        downInterpolator.reset();
    }

    bool isActive() const { return active; }
    int getMaxInnerBlock() const { return maxInnerBlock; }
    int getLatencySamples() const { return latency; } // host samples

    // innerProcess (float* samples, int numSamples) runs in place at the inner rate.
    template <typename Fn>
    void process (float* samples, int numSamples, Fn&& innerProcess) noexcept
    {
        if (! active)
        {
            innerProcess (samples, numSamples);
            return;
        }

        std::copy (samples, samples + numSamples, upFifo.begin() + upCount);
        upCount += numSamples;

        const auto innerTarget = (juce::int64) std::floor ((double) (hostTotal + numSamples) * ratio);
        const int innerCount = (int) (innerTarget - innerTotal);
        hostTotal += numSamples;
        innerTotal = innerTarget;

        consume (upFifo, upCount, upInterpolator.process (1.0 / ratio, upFifo.data(), inner.data(), innerCount));
        innerProcess (inner.data(), innerCount);

        std::copy (inner.begin(), inner.begin() + innerCount, downFifo.begin() + downCount);
        downCount += innerCount;
        consume (downFifo, downCount, downInterpolator.process (ratio, downFifo.data(), samples, numSamples));
    }

private:
    static void consume (std::vector<float>& fifo, int& count, int used) noexcept
    {
        used = juce::jmin (used, count);
        std::move (fifo.begin() + used, fifo.begin() + count, fifo.begin());
        count -= used;
    }

    int measureLatency (int maxHostBlock)
    {
        reset();
        const int length = juce::jmax (4096, (int) (hostRate * 0.05));
        std::vector<float> block ((size_t) maxHostBlock);
        int best = 0;
        float bestValue = 0.0f;
        for (int pos = 0; pos < length; pos += maxHostBlock)
        {
            const int n = juce::jmin (maxHostBlock, length - pos);
            std::fill (block.begin(), block.end(), 0.0f);
            if (pos == 0)
                block[0] = 1.0f;
            process (block.data(), n, [] (float*, int) {});
            for (int i = 0; i < n; ++i)
                if (std::abs (block[(size_t) i]) > bestValue)
                    bestValue = std::abs (block[(size_t) i]), best = pos + i;
        }
        return best;
    }

    static constexpr int pad = 8;

    double hostRate = 48000.0, innerRate = 48000.0, ratio = 1.0;
    bool active = false;
    int maxInnerBlock = 0, latency = 0;
    std::vector<float> upFifo, downFifo, inner;
    int upCount = pad, downCount = pad;
    juce::int64 hostTotal = 0, innerTotal = 0;
    Interpolator upInterpolator, downInterpolator;
};
} // namespace poman
