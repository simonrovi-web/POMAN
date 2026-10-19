#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Ambience.h"
#include "dsp/AmpModel.h"
#include "dsp/CabSim.h"
#include "dsp/NoiseGate.h"
#include "dsp/Octaver.h"
#include "dsp/Overdrive.h"
#include "PortablePaths.h"

class AbyssalProcessor : public juce::AudioProcessor
{
public:
    AbyssalProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Abyssal"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void loadImpulseResponse (const juce::File& file);
    void clearImpulseResponse();
    juce::String getImpulseResponseName() const;

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState apvts;

private:
    void updateParameters();
    juce::File resolveImpulseResponsePath (const juce::String& path) const;
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    enum class Param
    {
        input, output, gateOn, gateThreshold, octOn, octSub, octUp, octDry, odOn, odDrive, odTone, odLevel, ampChannel, ampGain, ampBass, ampMid, ampTreble, ampPresence, ampDepth, ampMaster, cabOn, cabMic, dlyOn, dlyTime, dlyFeedback, dlyMix, revOn, revSize, revMix,
        count
    };

    float param (Param p) const { return rawParameters[(size_t) p]->load(); }
    bool enabled (Param p) const { return param (p) > 0.5f; }

    std::array<std::atomic<float>*, (size_t) Param::count> rawParameters {};

    abyssal::NoiseGate gate;
    abyssal::Octaver octaver;
    abyssal::Overdrive overdrive;
    abyssal::AmpModel amp;
    abyssal::CabSim cab;
    abyssal::PingPongDelay delay;
    abyssal::Ambience reverb;

    juce::AudioBuffer<float> monoBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    int maxChunkSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbyssalProcessor)
};
