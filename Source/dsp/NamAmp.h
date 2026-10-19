#pragma once

#include "AmpModel.h"
#include "DspCommon.h"
#include "Resampler.h"
#include <atomic>
#include <memory>

namespace nam { class DSP; }

namespace poman
{
// Plays a Neural Amp Modeler capture (.nam) in place of the built-in amp.
//
// Loading parses and prewarms the model on the calling (message) thread, then swaps it in under a
// lock the audio thread only ever try-locks; the old model is freed outside the lock. Models run at
// their training rate (normally 48 kHz) through a windowed-sinc resampler when the host differs.
// The amp knobs keep working: GAIN drives the capture, BASS..DEPTH are a post tone stack and
// MASTER is the output level. Captures that publish their loudness are normalised to -18 dB.
class NamAmp
{
public:
    NamAmp();
    ~NamAmp();

    juce::Result load (const juce::File& file); // message thread
    void unload();                              // message thread

    bool isLoaded() const { return loaded.load(); }
    juce::String getModelName() const { return modelName; }   // message thread
    double getModelSampleRate() const { return modelRate.load(); }

    void prepare (double hostSampleRate, int maxBlockSize);   // audio setup, not concurrent with process()
    int getLatencySamples() const { return latency.load(); } // host samples

    void setParams (const AmpParams& params);                  // audio thread
    void process (juce::dsp::AudioBlock<float>& block) noexcept; // audio thread

private:
    struct Engine;
    std::unique_ptr<Engine> makeEngine (const juce::File& file, juce::String& error) const;
    void swapEngine (std::unique_ptr<Engine> next);

    juce::SpinLock engineLock;
    std::unique_ptr<Engine> engine;
    std::atomic<bool> loaded { false };
    std::atomic<double> modelRate { 0.0 };
    std::atomic<int> latency { 0 };
    juce::String modelName;

    double hostRate = 48000.0;
    int maxBlock = 512;

    AmpParams cached;
    bool designed = false;
    Biquad bass, mid, treble, presence, depth;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
};
} // namespace poman
