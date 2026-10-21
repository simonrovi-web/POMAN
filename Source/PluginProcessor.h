#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>

#include "dsp/Ambience.h"
#include "dsp/AmpModel.h"
#include "dsp/CabSim.h"
#include "dsp/Compressor.h"
#include "dsp/Tempo.h"
#include "dsp/DrumMachine.h"
#include "dsp/GraphicEQ.h"
#include "dsp/Harmonizer.h"
#include "dsp/Looper.h"
#include "dsp/NamAmp.h"
#include "dsp/Modulation.h"
#include "dsp/NoiseGate.h"
#include "dsp/Octaver.h"
#include "dsp/Overdrive.h"
#include "PortablePaths.h"
#include "MidiRouter.h"
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
    bool acceptsMidi() const override { return true; }
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

    // Files the rig depends on. Paths live in the state (relative inside the portable folder), so
    // sessions and user presets restore them; a missing file leaves the slot empty but remembered.
    enum class FileSlot { irA, irB, namModel };
    juce::Result loadFile (FileSlot slot, const juce::File& file); // message thread
    void clearFile (FileSlot slot);                                // message thread
    juce::String getFileName (FileSlot slot) const;                // empty when nothing is set
    bool isFileSlotLoaded (FileSlot slot) const;

    // AMPLIFIER choices: the built-in models followed by the NAM capture slot.
    static juce::StringArray ampModelChoices();
    static int namModelIndex() { return poman::AmpModel::modelNames().size(); }
    double getNamModelRate() const { return namAmp.getModelSampleRate(); }
    int getNamResamplingLatency() const { return namAmp.getLatencySamples(); }
    int getCabAlignmentSamples() const { return cab.getAlignmentSamples(); }

    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Replaces all parameters and the IR from a saved state tree (session restore and user presets).
    void applyState (const juce::ValueTree& state);

    poman::PresetManager& getPresetManager() { return *presetManager; }
    poman::MultitrackRecorder& getRecorder() { return *recorder; }
    poman::DrumMachine& getDrums() { return drums; }
    poman::Looper& getLooper() { return looper; }
    // Export layer that renders the drum pattern (empty when the drums are off).
    poman::MultitrackRecorder::ExportLayer makeDrumExportLayer();

    // Single reader of the EQ parameters, used by the audio path and the editor's response curve.
    poman::GraphicEQ::Settings getEqSettings() const;
    bool isEqEnabled() const { return enabled (Param::eqOn); }

    // Peak levels since the last call (UI thread), for the meters.
    float takeInputPeak() { return inputPeak.exchange (0.0f); }
    // Peak of the guitar DI before INPUT gain, for the input calibration.
    float takeRawInputPeak() { return rawInputPeak.exchange (0.0f); }
    float getCompressorGainReductionDb() const { return enabled (Param::compOn) ? compressor.getGainReductionDb() : 0.0f; }
    // Tempo for synced effects: the host's in a DAW, otherwise the studio/drums tempo.
    double getCurrentBpm() const;
    float takeOutputPeak() { return outputPeak.exchange (0.0f); }

    // While the tuner is open the output is muted, like a tuner pedal.
    void setTunerActive (bool active) { tunerActive.store (active); }
    bool isTunerActive() const { return tunerActive.load(); }

    // MIDI learn / Program Change (message thread). Actions are MidiRouter targets named "action:<name>".
    poman::MidiRouter& getMidiRouter() { return midiRouter; }
    void performAction (const juce::String& name);
    poman::TunerReading analyseTuner() { return tuner.analyse(); }

    // Harmonizer state for the UI: tracked input note (-1 none) and each voice's interval in semitones.
    int getHarmonizerNote() const { return harmonizer.getDetectedNote(); }
    int getHarmonizerSemitones (int voice) const { return harmonizer.getVoiceSemitones (voice); }

    juce::AudioProcessorValueTreeState apvts;

private:
    void updateParameters();
    juce::File resolveStoredPath (const juce::String& path) const;
    void restoreFiles();
    void updateLatency();
    std::array<juce::String, 3> loadedPaths; // per FileSlot: what the engines currently hold
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);
    enum class Param
    {
        input, output, gateOn, gateThreshold, compOn, compSustain, compAttack, compLevel, compBlend, octOn, octSub, octUp, octDry, odOn, odType, odDrive, odTone, odLevel, ampChannel, ampGain, ampBass, ampMid, ampTreble, ampPresence, ampDepth, ampMaster, cabOn, cabMic, cabMix, cabLowCut, cabHighCut, eqOn, eq31, eq63, eq125, eq250, eq500, eq1k, eq2k, eq4k, eq8k, eq16k, eqLowCut, eqHighCut, eqLevel, harmOn, harmMode, harmKey, harmScale, harmV1Steps, harmV1Level, harmV2Steps, harmV2Level, harmDry, modOn, modType, modRate, modDepth, modMix, modFeedback, dlyOn, dlyTime, dlyFeedback, dlyMix, dlySync, revOn, revSize, revMix,
        count
    };

    float param (Param p) const { return rawParameters[(size_t) p]->load(); }
    bool enabled (Param p) const { return param (p) > 0.5f; }

    std::array<std::atomic<float>*, (size_t) Param::count> rawParameters {};

    poman::NoiseGate gate;
    poman::Compressor compressor;
    poman::Octaver octaver;
    poman::Overdrive overdrive;
    poman::AmpModel amp;
    poman::CabSim cab;
    poman::GraphicEQ graphicEq;
    poman::DrumMachine drums;
    poman::Looper looper;
    poman::NamAmp namAmp;
    // The built-in amp and the NAM path differ in latency; the shorter one is padded so the
    // reported latency stays the same whichever amp is selected.
    struct PathDelay
    {
        std::array<float, 8192> line {};
        int write = 0;
        void process (float* x, int n, int delay) noexcept
        {
            for (int i = 0; i < n; ++i)
            {
                line[(size_t) write] = x[i];
                x[i] = line[(size_t) ((write - delay) & 8191)];
                write = (write + 1) & 8191;
            }
        }
    } pathDelay;
    std::atomic<int> ampPathPad { 0 }, namPathPad { 0 };
    poman::Harmonizer harmonizer;
    poman::Modulation modulation;
    poman::PingPongDelay delay;
    poman::Ambience reverb;

    juce::AudioBuffer<float> monoBuffer;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Multiplicative> inputGain { 1.0f }, outputGain { 1.0f };
    int maxChunkSize = 512;

    std::unique_ptr<poman::PresetManager> presetManager;

    poman::MidiRouter midiRouter;
    void handleMidi(); // message thread, ~100 Hz
    struct MidiTimer : juce::Timer
    {
        explicit MidiTimer (PomanProcessor& p) : owner (p) {}
        void timerCallback() override { owner.handleMidi(); }
        PomanProcessor& owner;
    } midiTimer { *this };
    std::unique_ptr<poman::MultitrackRecorder> recorder;
    poman::Tuner tuner;
    std::atomic<bool> tunerActive { false };
    std::atomic<float> inputPeak { 0.0f }, outputPeak { 0.0f }, rawInputPeak { 0.0f };
    std::atomic<double> hostBpm { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PomanProcessor)
};
