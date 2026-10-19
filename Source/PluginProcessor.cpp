#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::Identifier irPathProperty { "impulseResponsePath" };
const juce::Identifier irPathBProperty { "impulseResponsePathB" };
const juce::Identifier namPathProperty { "namModelPath" };

const juce::Identifier& slotProperty (PomanProcessor::FileSlot slot)
{
    switch (slot)
    {
        case PomanProcessor::FileSlot::irA:      return irPathProperty;
        case PomanProcessor::FileSlot::irB:      return irPathBProperty;
        case PomanProcessor::FileSlot::namModel: return namPathProperty;
    }
    return irPathProperty;
}

using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;
using juce::AudioParameterBool;
using juce::AudioParameterChoice;
using juce::AudioParameterFloat;
using juce::NormalisableRange;
using juce::ParameterID;

void addFloat (Layout& layout, const char* id, const char* name, NormalisableRange<float> range, float defaultValue, const char* unit = "")
{
    const juce::String label (unit);
    const bool normalised = label.isEmpty() && range.start == 0.0f && range.end <= 1.0f;

    // Text shown on knobs and in hosts: percentages for 0..1 amounts, units elsewhere.
    auto toText = [label, normalised] (float value, int)
    {
        if (normalised)
            return juce::String (juce::roundToInt (value * 100.0f)) + " %";
        if (label == "ms")
            return juce::String (juce::roundToInt (value)) + " ms";
        if (label == "dB")
            return juce::String (value, 1) + " dB";
        if (label == "Hz")
            return value >= 1000.0f ? juce::String (value / 1000.0f, 1) + " kHz"
                 : value >= 20.0f   ? juce::String (juce::roundToInt (value)) + " Hz"
                                    : juce::String (value, 2) + " Hz";
        return juce::String (value, 1);
    };
    auto fromText = [normalised] (const juce::String& text)
    {
        const float value = text.retainCharacters ("-0123456789.").getFloatValue();
        return normalised ? value / 100.0f : value;
    };

    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, defaultValue,
                                                       juce::AudioParameterFloatAttributes()
                                                           .withLabel (normalised ? "%" : unit)
                                                           .withStringFromValueFunction (toText)
                                                           .withValueFromStringFunction (fromText)));
}

void addKnob (Layout& layout, const char* id, const char* name, float defaultValue)
{
    addFloat (layout, id, name, { 0.0f, 10.0f, 0.01f }, defaultValue);
}

void addSwitch (Layout& layout, const char* id, const char* name, bool defaultValue)
{
    layout.add (std::make_unique<AudioParameterBool> (ParameterID { id, 1 }, name, defaultValue));
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout PomanProcessor::createParameterLayout()
{
    Layout layout;

    addFloat (layout, "input", "Input", { -24.0f, 24.0f, 0.1f }, 0.0f, "dB");
    addFloat (layout, "output", "Output", { -36.0f, 12.0f, 0.1f }, 0.0f, "dB");

    addSwitch (layout, "gateOn", "Gate On", true);
    addFloat (layout, "gateThreshold", "Gate Threshold", { -90.0f, -20.0f, 0.1f }, -62.0f, "dB");

    addSwitch (layout, "compOn", "Comp On", false);
    addFloat (layout, "compSustain", "Comp Sustain", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat (layout, "compAttack", "Comp Attack", { 0.0f, 1.0f, 0.001f }, 0.3f);
    addFloat (layout, "compLevel", "Comp Level", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB");
    addFloat (layout, "compBlend", "Comp Blend", { 0.0f, 1.0f, 0.001f }, 1.0f);

    addSwitch (layout, "octOn", "Octave On", false);
    addFloat (layout, "octSub", "Octave Sub", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat (layout, "octUp", "Octave Up", { 0.0f, 1.0f, 0.001f }, 0.0f);
    addFloat (layout, "octDry", "Octave Dry", { 0.0f, 1.0f, 0.001f }, 1.0f);

    addSwitch (layout, "odOn", "Drive On", true);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "odType", 1 }, "Drive Type",
                                                        poman::Overdrive::typeNames(), 0));
    addFloat (layout, "odDrive", "Drive", { 0.0f, 1.0f, 0.001f }, 0.15f);
    addFloat (layout, "odTone", "Drive Tone", { 0.0f, 1.0f, 0.001f }, 0.55f);
    addFloat (layout, "odLevel", "Drive Level", { -12.0f, 12.0f, 0.1f }, 3.0f, "dB");

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ampChannel", 1 }, "Amp Model",
                                                        ampModelChoices(), 1));
    addKnob (layout, "ampGain", "Amp Gain", 6.0f);
    addKnob (layout, "ampBass", "Bass", 5.5f);
    addKnob (layout, "ampMid", "Mid", 4.0f);
    addKnob (layout, "ampTreble", "Treble", 6.0f);
    addKnob (layout, "ampPresence", "Presence", 5.5f);
    addKnob (layout, "ampDepth", "Depth", 6.0f);
    addKnob (layout, "ampMaster", "Master", 5.0f);

    addSwitch (layout, "cabOn", "Cab On", true);
    addFloat (layout, "cabMic", "Mic Position", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat (layout, "cabMix", "Cab A/B Mix", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat (layout, "cabLowCut", "Cab Low Cut", { 20.0f, 300.0f, 1.0f, 0.5f }, 20.0f, "Hz");
    addFloat (layout, "cabHighCut", "Cab High Cut", { 2000.0f, 20000.0f, 10.0f, 0.4f }, 20000.0f, "Hz");

    addSwitch (layout, "eqOn", "EQ On", false);
    {
        const auto labels = poman::GraphicEQ::bandLabels();
        const char* ids[] { "eq31", "eq63", "eq125", "eq250", "eq500", "eq1k", "eq2k", "eq4k", "eq8k", "eq16k" };
        for (int b = 0; b < poman::GraphicEQ::numBands; ++b)
            addFloat (layout, ids[b], ("EQ " + labels[b]).toRawUTF8(), { -12.0f, 12.0f, 0.1f }, 0.0f, "dB");
    }
    addFloat (layout, "eqLowCut", "EQ Low Cut", { 20.0f, 400.0f, 1.0f, 0.4f }, 20.0f, "Hz");
    addFloat (layout, "eqHighCut", "EQ High Cut", { 2000.0f, 20000.0f, 10.0f, 0.4f }, 20000.0f, "Hz");
    addFloat (layout, "eqLevel", "EQ Level", { -12.0f, 12.0f, 0.1f }, 0.0f, "dB");

    addSwitch (layout, "harmOn", "Harmonizer On", false);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "harmMode", 1 }, "Harmonizer Mode", poman::Harmonizer::modeNames(), 1));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "harmKey", 1 }, "Harmonizer Key", poman::Harmonizer::keyNames(), 4));
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "harmScale", 1 }, "Harmonizer Scale", poman::Harmonizer::scaleNames(), 1));
    // Steps are scale degrees in Diatonic mode and semitones in Chromatic mode.
    layout.add (std::make_unique<juce::AudioParameterInt> (ParameterID { "harmV1Steps", 1 }, "Voice 1 Interval", -14, 14, 2));
    addFloat (layout, "harmV1Level", "Voice 1 Level", { 0.0f, 1.0f, 0.001f }, 0.8f);
    layout.add (std::make_unique<juce::AudioParameterInt> (ParameterID { "harmV2Steps", 1 }, "Voice 2 Interval", -14, 14, 4));
    addFloat (layout, "harmV2Level", "Voice 2 Level", { 0.0f, 1.0f, 0.001f }, 0.0f);
    addFloat (layout, "harmDry", "Harmonizer Dry", { 0.0f, 1.0f, 0.001f }, 1.0f);

    addSwitch (layout, "modOn", "Mod On", false);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "modType", 1 }, "Mod Type", poman::Modulation::typeNames(), 0));
    addFloat (layout, "modRate", "Mod Rate", { 0.05f, 10.0f, 0.01f, 0.4f }, 0.8f, "Hz");
    addFloat (layout, "modDepth", "Mod Depth", { 0.0f, 1.0f, 0.001f }, 0.5f);
    // For the tremolo these two act as wave shape and stereo spread (see Modulation).
    addFloat (layout, "modMix", "Mod Mix / Shape", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat (layout, "modFeedback", "Mod Feedback / Stereo", { 0.0f, 1.0f, 0.001f }, 0.3f);

    addSwitch (layout, "dlyOn", "Delay On", false);
    addFloat (layout, "dlyTime", "Delay Time", { 40.0f, 1500.0f, 1.0f, 0.5f }, 380.0f, "ms");
    addFloat (layout, "dlyFeedback", "Delay Feedback", { 0.0f, 0.9f, 0.001f }, 0.3f);
    addFloat (layout, "dlyMix", "Delay Mix", { 0.0f, 1.0f, 0.001f }, 0.25f);
    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "dlySync", 1 }, "Delay Sync", poman::delayDivisionNames(), 0));

    addSwitch (layout, "revOn", "Reverb On", false);
    addFloat (layout, "revSize", "Reverb Size", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat (layout, "revMix", "Reverb Mix", { 0.0f, 1.0f, 0.001f }, 0.2f);

    return layout;
}

PomanProcessor::PomanProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "PomanState", createParameterLayout())
{
    static constexpr const char* ids[] { "input", "output", "gateOn", "gateThreshold", "compOn", "compSustain", "compAttack", "compLevel", "compBlend", "octOn", "octSub", "octUp", "octDry", "odOn", "odType", "odDrive", "odTone", "odLevel", "ampChannel", "ampGain", "ampBass", "ampMid", "ampTreble", "ampPresence", "ampDepth", "ampMaster", "cabOn", "cabMic", "cabMix", "cabLowCut", "cabHighCut", "eqOn", "eq31", "eq63", "eq125", "eq250", "eq500", "eq1k", "eq2k", "eq4k", "eq8k", "eq16k", "eqLowCut", "eqHighCut", "eqLevel", "harmOn", "harmMode", "harmKey", "harmScale", "harmV1Steps", "harmV1Level", "harmV2Steps", "harmV2Level", "harmDry", "modOn", "modType", "modRate", "modDepth", "modMix", "modFeedback", "dlyOn", "dlyTime", "dlyFeedback", "dlyMix", "dlySync", "revOn", "revSize", "revMix" };
    static_assert (std::size (ids) == (size_t) Param::count);

    for (size_t i = 0; i < rawParameters.size(); ++i)
    {
        rawParameters[i] = apvts.getRawParameterValue (ids[i]);
        jassert (rawParameters[i] != nullptr);
    }

    // The standalone keeps recordings in the portable folder; the plugin (and read-only installs) in Documents.
    const bool portable = wrapperType == wrapperType_Standalone && poman::portableRoot().hasWriteAccess();
    recorder = std::make_unique<poman::MultitrackRecorder> (
        portable ? poman::portableRoot().getChildFile ("Recordings")
                 : juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("POMAN").getChildFile ("Recordings"));

    presetManager = std::make_unique<poman::PresetManager> (
        apvts, wrapperType == wrapperType_Standalone, [this] (const juce::ValueTree& state) { applyState (state); });
}

bool PomanProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void PomanProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    maxChunkSize = juce::jmax (32, samplesPerBlock);
    monoBuffer.setSize (1, maxChunkSize);

    gate.prepare (sampleRate);
    compressor.prepare (sampleRate);
    octaver.prepare (sampleRate);
    overdrive.prepare (sampleRate, maxChunkSize);
    amp.prepare (sampleRate, maxChunkSize);
    namAmp.prepare (sampleRate, maxChunkSize);
    cab.prepare (sampleRate, maxChunkSize);
    delay.prepare (sampleRate, maxChunkSize);
    reverb.prepare (sampleRate, maxChunkSize);
    graphicEq.prepare (sampleRate);
    harmonizer.prepare (sampleRate);
    modulation.prepare (sampleRate, maxChunkSize);
    recorder->prepare (sampleRate);
    drums.prepare (sampleRate);
    tuner.prepare (sampleRate);

    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (Param::input)));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (Param::output)));

    updateParameters();
    updateLatency();
}

void PomanProcessor::updateParameters()
{
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (param (Param::input)));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (Param::output)));

    gate.setThreshold (param (Param::gateThreshold));
    octaver.setMix (param (Param::octSub), param (Param::octUp), param (Param::octDry));
    overdrive.setParams ((poman::Overdrive::Type) juce::jlimit (0, (int) poman::Overdrive::Type::count - 1, (int) param (Param::odType)),
                         param (Param::odDrive), param (Param::odTone), param (Param::odLevel));

    poman::AmpParams ampParams;
    // With NAM selected but no capture loaded, the built-in Glass Clean stands in.
    const int channel = juce::roundToInt (param (Param::ampChannel));
    ampParams.channel = channel >= namModelIndex() ? 0 : channel;
    ampParams.gain = param (Param::ampGain);
    ampParams.bass = param (Param::ampBass);
    ampParams.mid = param (Param::ampMid);
    ampParams.treble = param (Param::ampTreble);
    ampParams.presence = param (Param::ampPresence);
    ampParams.depth = param (Param::ampDepth);
    ampParams.master = param (Param::ampMaster);
    amp.setParams (ampParams);
    namAmp.setParams (ampParams);

    cab.setMic (param (Param::cabMic));
    cab.setMix (param (Param::cabMix));
    cab.setCuts (param (Param::cabLowCut), param (Param::cabHighCut));
    graphicEq.setSettings (getEqSettings());

    poman::Harmonizer::Params harmParams;
    harmParams.mode = param (Param::harmMode) > 0.5f ? poman::Harmonizer::Mode::diatonic : poman::Harmonizer::Mode::chromatic;
    harmParams.key = juce::roundToInt (param (Param::harmKey));
    harmParams.scale = juce::roundToInt (param (Param::harmScale));
    harmParams.steps = { juce::roundToInt (param (Param::harmV1Steps)), juce::roundToInt (param (Param::harmV2Steps)) };
    harmParams.level = { param (Param::harmV1Level), param (Param::harmV2Level) };
    harmParams.dry = param (Param::harmDry);
    harmonizer.setParams (harmParams);

    modulation.setParams ((poman::Modulation::Type) juce::jlimit (0, (int) poman::Modulation::Type::count - 1, juce::roundToInt (param (Param::modType))),
                          param (Param::modRate), param (Param::modDepth), param (Param::modMix), param (Param::modFeedback));
    compressor.setParams (param (Param::compSustain), param (Param::compAttack), param (Param::compLevel), param (Param::compBlend));
    delay.setParams (poman::syncedDelayMs (getCurrentBpm(), juce::roundToInt (param (Param::dlySync)), param (Param::dlyTime)),
                     param (Param::dlyFeedback), param (Param::dlyMix));
    reverb.setParams (param (Param::revSize), param (Param::revMix));
}

poman::GraphicEQ::Settings PomanProcessor::getEqSettings() const
{
    constexpr Param bands[] { Param::eq31, Param::eq63, Param::eq125, Param::eq250, Param::eq500, Param::eq1k, Param::eq2k, Param::eq4k, Param::eq8k, Param::eq16k };
    poman::GraphicEQ::Settings s;
    for (int b = 0; b < poman::GraphicEQ::numBands; ++b)
        s.gainsDb[(size_t) b] = param (bands[b]);
    s.lowCutHz = param (Param::eqLowCut);
    s.highCutHz = param (Param::eqHighCut);
    s.levelDb = param (Param::eqLevel);
    return s;
}

double PomanProcessor::getCurrentBpm() const
{
    const double host = hostBpm.load();
    return host > 0.0 ? host : recorder->getBpm();
}

void PomanProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    // In a DAW, synced effects follow the host tempo when it reports one.
    double bpm = 0.0;
    if (wrapperType != wrapperType_Standalone)
        if (auto* playHead = getPlayHead())
            if (const auto position = playHead->getPosition())
                bpm = position->getBpm().orFallback (0.0);
    hostBpm.store (bpm);

    updateParameters();

    for (int start = 0; start < buffer.getNumSamples(); start += maxChunkSize)
        processChunk (buffer, start, juce::jmin (maxChunkSize, buffer.getNumSamples() - start));
}

void PomanProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    // Guitar is mono: sum the inputs, run the rig, then widen with the stereo effects.
    auto* mono = monoBuffer.getWritePointer (0);
    const int numInputs = juce::jmax (1, getTotalNumInputChannels());

    float rawPeak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
            sum += buffer.getReadPointer (ch)[startSample + i];
        const float raw = sum / (float) numInputs;
        rawPeak = juce::jmax (rawPeak, std::abs (raw));
        mono[i] = raw * inputGain.getNextValue();
    }
    for (float previous = rawInputPeak.load(); rawPeak > previous && ! rawInputPeak.compare_exchange_weak (previous, rawPeak);) {}

    const auto blockPeak = [] (const float* data, int n)
    {
        const auto range = juce::FloatVectorOperations::findMinAndMax (data, n);
        return juce::jmax (-range.getStart(), range.getEnd());
    };
    const auto storePeak = [] (std::atomic<float>& peak, float value)
    {
        float previous = peak.load();
        while (value > previous && ! peak.compare_exchange_weak (previous, value)) {}
    };

    storePeak (inputPeak, blockPeak (mono, numSamples));

    const bool tunerOn = tunerActive.load();
    if (tunerOn)
        tuner.push (mono, numSamples);

    const bool harmonizerOn = enabled (Param::harmOn);
    if (harmonizerOn)
        harmonizer.track (mono, numSamples);

    if (enabled (Param::gateOn))
        gate.process (mono, numSamples);
    if (enabled (Param::compOn))
        compressor.process (mono, numSamples);
    if (enabled (Param::octOn))
        octaver.process (mono, numSamples);
    if (enabled (Param::odOn))
        overdrive.process (mono, numSamples);

    juce::dsp::AudioBlock<float> monoBlock (monoBuffer.getArrayOfWritePointers(), 1, 0, (size_t) numSamples);
    const bool useNam = juce::roundToInt (param (Param::ampChannel)) == namModelIndex() && namAmp.isLoaded();
    if (useNam)
        namAmp.process (monoBlock);
    else
        amp.process (monoBlock);
    pathDelay.process (mono, numSamples, useNam ? namPathPad.load() : ampPathPad.load());

    if (enabled (Param::cabOn))
        cab.process (monoBlock);

    if (enabled (Param::eqOn))
        graphicEq.process (mono, numSamples);

    auto* left = buffer.getWritePointer (0, startSample);
    auto* right = buffer.getWritePointer (1, startSample);
    if (harmonizerOn)
    {
        harmonizer.process (mono, left, right, numSamples);
    }
    else
    {
        juce::FloatVectorOperations::copy (left, mono, numSamples);
        juce::FloatVectorOperations::copy (right, mono, numSamples);
    }

    if (enabled (Param::modOn))
        modulation.process (left, right, numSamples);

    if (enabled (Param::dlyOn))
        delay.process (left, right, numSamples);

    if (enabled (Param::revOn))
    {
        auto stereoBlock = juce::dsp::AudioBlock<float> (buffer).getSubsetChannelBlock (0, 2)
                               .getSubBlock ((size_t) startSample, (size_t) numSamples);
        reverb.process (stereoBlock);
    }

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = outputGain.getNextValue();
        left[i] *= g;
        right[i] *= g;
    }

    // Records the finished rig sound, then layers the other tracks and the click on top.
    recorder->process (left, right, numSamples);

    // Drums follow the studio transport and sit on top of the recording, so they are never
    // captured into a guitar take. When stopped, ringing hits just decay.
    if (recorder->getState() != poman::MultitrackRecorder::State::stopped)
        drums.process (left, right, numSamples, recorder->getPlayhead() - numSamples, recorder->getBpm());
    else
        drums.processTails (left, right, numSamples);

    if (tunerOn)
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
    }

    storePeak (outputPeak, juce::jmax (blockPeak (left, numSamples), blockPeak (right, numSamples)));
}

juce::StringArray PomanProcessor::ampModelChoices()
{
    auto names = poman::AmpModel::modelNames();
    names.add ("NAM Capture");
    return names;
}

juce::Result PomanProcessor::loadFile (FileSlot slot, const juce::File& file)
{
    if (! file.existsAsFile())
        return juce::Result::fail ("File not found: " + file.getFullPathName());

    const auto result = slot == FileSlot::namModel ? namAmp.load (file)
                                                   : cab.loadImpulseResponse (slot == FileSlot::irA ? 0 : 1, file);
    if (result.failed())
        return result;

    // Standalone keeps files inside the portable folder relative, so the folder can move between machines.
    const auto root = poman::portableRoot();
    const bool portable = wrapperType == wrapperType_Standalone && file.isAChildOf (root);
    apvts.state.setProperty (slotProperty (slot), portable ? file.getRelativePathFrom (root) : file.getFullPathName(), nullptr);
    loadedPaths[(size_t) slot] = file.getFullPathName();
    if (slot == FileSlot::namModel)
        updateLatency();
    return result;
}

void PomanProcessor::clearFile (FileSlot slot)
{
    if (slot == FileSlot::namModel)
        namAmp.unload();
    else
        cab.clearImpulseResponse (slot == FileSlot::irA ? 0 : 1);
    apvts.state.removeProperty (slotProperty (slot), nullptr);
    loadedPaths[(size_t) slot].clear();
    if (slot == FileSlot::namModel)
        updateLatency();
}

juce::String PomanProcessor::getFileName (FileSlot slot) const
{
    const auto path = apvts.state.getProperty (slotProperty (slot)).toString();
    return path.isEmpty() ? juce::String() : resolveStoredPath (path).getFileNameWithoutExtension();
}

bool PomanProcessor::isFileSlotLoaded (FileSlot slot) const
{
    return slot == FileSlot::namModel ? namAmp.isLoaded() : cab.hasImpulseResponse (slot == FileSlot::irA ? 0 : 1);
}

juce::File PomanProcessor::resolveStoredPath (const juce::String& path) const
{
    if (path.isEmpty())
        return {};

    return juce::File::isAbsolutePath (path) ? juce::File (path) : poman::portableRoot().getChildFile (path);
}

// Brings the engines in line with the paths in the state. Unchanged files are not reloaded, so
// switching presets that share a capture is instant.
void PomanProcessor::restoreFiles()
{
    for (auto slot : { FileSlot::irA, FileSlot::irB, FileSlot::namModel })
    {
        const auto file = resolveStoredPath (apvts.state.getProperty (slotProperty (slot)).toString());
        auto& loaded = loadedPaths[(size_t) slot];
        if (file.existsAsFile())
        {
            if (loaded == file.getFullPathName())
                continue;
            const auto result = slot == FileSlot::namModel ? namAmp.load (file)
                                                           : cab.loadImpulseResponse (slot == FileSlot::irA ? 0 : 1, file);
            loaded = result.wasOk() ? file.getFullPathName() : juce::String();
        }
        else if (loaded.isNotEmpty() || isFileSlotLoaded (slot))
        {
            if (slot == FileSlot::namModel)
                namAmp.unload();
            else
                cab.clearImpulseResponse (slot == FileSlot::irA ? 0 : 1);
            loaded.clear();
        }
    }
    updateLatency();
}

void PomanProcessor::updateLatency()
{
    const int ampLatency = amp.getLatencySamples();
    const int namLatency = namAmp.isLoaded() ? namAmp.getLatencySamples() : 0;
    const int pathLatency = juce::jmax (ampLatency, namLatency);
    ampPathPad.store (pathLatency - ampLatency);
    namPathPad.store (pathLatency - namLatency);
    setLatencySamples (pathLatency + overdrive.getLatencySamples()); // the drive oversampler is always in the path
}

void PomanProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // The drum pattern travels with the session state but stays out of apvts.state, so presets
    // (which save apvts.state) never carry a drum pattern.
    auto state = apvts.copyState();
    state.appendChild (drums.toValueTree(), nullptr);
    if (auto xml = state.createXml())
        copyXmlToBinary (*xml, destData);
}

void PomanProcessor::renderDrumsOffline (juce::AudioBuffer<float>& mix)
{
    if (! drums.isEnabled() || mix.getNumChannels() < 2)
        return;

    poman::DrumMachine offline;
    offline.copySettingsFrom (drums);
    offline.prepare (getSampleRate() > 0.0 ? getSampleRate() : 48000.0);
    offline.process (mix.getWritePointer (0), mix.getWritePointer (1), mix.getNumSamples(), 0, recorder->getBpm());
}

void PomanProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    const auto drumState = state.getChildWithName ("Drums");
    if (drumState.isValid())
    {
        drums.fromValueTree (drumState);
        state.removeChild (drumState, nullptr);
    }
    applyState (state);
}

void PomanProcessor::applyState (const juce::ValueTree& state)
{
    apvts.replaceState (state.createCopy());
    restoreFiles();
}

juce::AudioProcessorEditor* PomanProcessor::createEditor()
{
    return new PomanEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PomanProcessor();
}
