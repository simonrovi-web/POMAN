#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Ambience.h"
#include "dsp/AmpModel.h"
#include "dsp/CabSim.h"
#include "dsp/Harmonizer.h"
#include "dsp/NoiseGate.h"
#include "dsp/Octaver.h"
#include "dsp/Overdrive.h"
#include "PortablePaths.h"
#include "PresetManager.h"
#include "Tuner.h"
#include "recorder/MultitrackRecorder.h"

class PomanProcessor : public juce::AudioProcessor
{
public:
    PomanProcessor();

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "POMAN"; }
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

    // Replaces all parameters and the IR from a saved state tree (session restore and user presets).
    void applyState (const juce::ValueTree& state);

    poman::PresetManager& getPresetManager() { return *presetManager; }
    poman::MultitrackRecorder& getRecorder() { return *recorder; }

    // Peak levels since the last call (UI thread), for the meters.
    float takeInputPeak() { return inputPeak.exchange (0.0f); }
    float takeOutputPeak() { return outputPeak.exchange (0.0f); }

    // While the tuner is open the output is muted, like a tuner pedal.
    void setTunerActive (bool active) { tunerActive.store (active); }
    poman::TunerReading analyseTuner() { return tuner.analyse(); }

    // Harmonizer state for the UI: tracked input note (-1 none) and each voice's interval in semitones.
    int getHarmonizerNote() const { return harmonizer.getDetectedNote(); }
    int getHarmonizerSemitones (int voice) const { return harmonizer.getVoiceSemitones (voice); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void updateParameters();
    juce::File resolveImpulseResponsePath (const juce::String& path) const;
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    enum class Param
    {
        input, output, gateOn, gateThreshold, octOn, octSub, octUp, octDry, odOn, odType, odDrive, odTone, odLevel, ampChannel, ampGain, ampBass, ampMid, ampTreble, ampPresence, ampDepth, ampMaster, cabOn, cabMic, harmOn, harmMode, harmKey, harmScale, harmV1Steps, harmV1Level, harmV2Steps, harmV2Level, harmDry, dlyOn, dlyTime, dlyFeedback, dlyMix, revOn, revSize, revMix,
        count
    };

    float param (Param p) const { return rawParameters[(size_t) p]->load(); }
    bool enabled (Param p) const { return param (p) > 0.5f; }

    std::array<std::atomic<float>*, (size_t) Param::count> rawParameters {};

    poman::NoiseGate gate;
    poman::Octaver octaver;
    poman::Overdrive overdrive;
    poman::AmpModel amp;
    poman::CabSim cab;
    poman::Harmonizer harmonizer;
    poman::PingPongDelay delay;
    poman::Ambience reverb;

    juce::AudioBuffer<float> monoBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    int maxChunkSize = 512;

    std::unique_ptr<poman::PresetManager> presetManager;
    std::unique_ptr<poman::MultitrackRecorder> recorder;
    poman::Tuner tuner;
    std::atomic<bool> tunerActive { false };
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PomanProcessor)
};
