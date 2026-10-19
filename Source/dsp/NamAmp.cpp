#include "NamAmp.h"

#include "NAM/dsp.h"
#include "NAM/get_dsp.h"

namespace poman
{
struct NamAmp::Engine
{
    std::unique_ptr<nam::DSP> dsp;
    double rate = 48000.0;
    float loudnessGain = 1.0f;
    ResampledProcess<juce::WindowedSincInterpolator> resampler;
    std::array<Biquad, 2> antiAlias; // before returning to a lower host rate
    bool needsAntiAlias = false;
    std::vector<float> output;

    void prepare (double hostRate, int hostBlock)
    {
        resampler.prepare (hostRate, rate, hostBlock);
        const int innerBlock = resampler.isActive() ? resampler.getMaxInnerBlock() : hostBlock;
        dsp->Reset (rate, innerBlock); // also prewarms the model
        output.assign ((size_t) innerBlock, 0.0f);

        needsAntiAlias = rate > hostRate * 1.0001;
        for (auto& f : antiAlias)
        {
            f.set (ArrayCoeffs::makeLowPass (rate, 0.45 * hostRate, 0.707));
            f.reset();
        }
    }

    void run (float* samples, int numSamples) noexcept
    {
        float* in[] { samples };
        float* out[] { output.data() };
        dsp->process (in, out, numSamples);

        for (int i = 0; i < numSamples; ++i)
        {
            float y = output[(size_t) i] * loudnessGain;
            if (needsAntiAlias)
                y = antiAlias[1] (antiAlias[0] (y));
            samples[i] = y;
        }
    }
};

NamAmp::NamAmp() = default;
NamAmp::~NamAmp() = default;

std::unique_ptr<NamAmp::Engine> NamAmp::makeEngine (const juce::File& file, juce::String& error) const
{
    auto e = std::make_unique<Engine>();
    try
    {
        e->dsp = nam::get_dsp (std::filesystem::path (file.getFullPathName().toWideCharPointer()));
    }
    catch (const std::exception& ex)
    {
        error = "Could not read the capture: " + juce::String (ex.what());
        return nullptr;
    }

    if (e->dsp == nullptr)
    {
        error = "Could not read the capture.";
        return nullptr;
    }
    if (e->dsp->NumInputChannels() != 1 || e->dsp->NumOutputChannels() != 1)
    {
        error = "Only mono (1 in / 1 out) captures are supported.";
        return nullptr;
    }

    const double expected = e->dsp->GetExpectedSampleRate();
    e->rate = expected > 0.0 ? expected : 48000.0; // older files omit it; NAM trains at 48 kHz
    if (e->dsp->HasLoudness())
        e->loudnessGain = dbToGain ((float) (-18.0 - e->dsp->GetLoudness()));
    e->prepare (hostRate, maxBlock);
    return e;
}

juce::Result NamAmp::load (const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("File not found: " + file.getFullPathName());

    juce::String error;
    auto next = makeEngine (file, error);
    if (next == nullptr)
        return juce::Result::fail (error);

    modelRate.store (next->rate);
    latency.store (next->resampler.getLatencySamples());
    modelName = file.getFileNameWithoutExtension();
    swapEngine (std::move (next));
    loaded.store (true);
    return juce::Result::ok();
}

void NamAmp::unload()
{
    loaded.store (false);
    swapEngine (nullptr);
    modelName.clear();
    modelRate.store (0.0);
    latency.store (0);
}

void NamAmp::swapEngine (std::unique_ptr<Engine> next)
{
    {
        const juce::SpinLock::ScopedLockType lock (engineLock);
        std::swap (engine, next);
    }
    // `next` holds the previous engine and is freed here, outside the lock.
}

void NamAmp::prepare (double hostSampleRate, int maxBlockSize)
{
    hostRate = hostSampleRate;
    maxBlock = maxBlockSize;
    inputGain.reset (hostRate, 0.05);
    outputGain.reset (hostRate, 0.05);
    designed = false;
    for (auto* f : { &bass, &mid, &treble, &presence, &depth })
        f->reset();

    const juce::SpinLock::ScopedLockType lock (engineLock);
    if (engine != nullptr)
    {
        engine->prepare (hostRate, maxBlock);
        latency.store (engine->resampler.getLatencySamples());
    }
}

void NamAmp::setParams (const AmpParams& p)
{
    // GAIN 0..10 drives the capture by -12..+12 dB; MASTER sets the output level the same way.
    inputGain.setTargetValue (dbToGain (knobToDb (p.gain, 12.0f)));
    outputGain.setTargetValue (dbToGain (knobToDb (p.master, 12.0f)));

    if (! designed || p.bass != cached.bass)
        bass.set (ArrayCoeffs::makeLowShelf (hostRate, 110.0, 0.7, dbToGain (knobToDb (p.bass, 12.0f))));
    if (! designed || p.mid != cached.mid)
        mid.set (ArrayCoeffs::makePeakFilter (hostRate, 650.0, 0.9, dbToGain (knobToDb (p.mid, 12.0f))));
    if (! designed || p.treble != cached.treble)
        treble.set (ArrayCoeffs::makeHighShelf (hostRate, 3000.0, 0.7, dbToGain (knobToDb (p.treble, 12.0f))));
    if (! designed || p.presence != cached.presence)
        presence.set (ArrayCoeffs::makeHighShelf (hostRate, 4500.0, 0.7, dbToGain (knobToDb (p.presence, 8.0f))));
    if (! designed || p.depth != cached.depth)
        depth.set (ArrayCoeffs::makePeakFilter (hostRate, 85.0, 1.0, dbToGain (knobToDb (p.depth, 8.0f))));
    cached = p;
    designed = true;
}

void NamAmp::process (juce::dsp::AudioBlock<float>& block) noexcept
{
    auto* x = block.getChannelPointer (0);
    const int n = (int) block.getNumSamples();

    const juce::SpinLock::ScopedTryLockType lock (engineLock);
    if (! lock.isLocked() || engine == nullptr)
    {
        juce::FloatVectorOperations::clear (x, n); // a model is being swapped in: one silent block
        return;
    }

    for (int i = 0; i < n; ++i)
        x[i] *= inputGain.getNextValue();

    engine->resampler.process (x, n, [this] (float* s, int count) { engine->run (s, count); });

    for (int i = 0; i < n; ++i)
        x[i] = depth (presence (treble (mid (bass (x[i]))))) * outputGain.getNextValue();
}
} // namespace poman
