#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
const juce::Identifier irPathProperty { "impulseResponsePath" };

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
            return juce::String (value, 2) + " Hz";
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
                                                        poman::AmpModel::modelNames(), 1));
    addKnob (layout, "ampGain", "Amp Gain", 6.0f);
    addKnob (layout, "ampBass", "Bass", 5.5f);
    addKnob (layout, "ampMid", "Mid", 4.0f);
    addKnob (layout, "ampTreble", "Treble", 6.0f);
    addKnob (layout, "ampPresence", "Presence", 5.5f);
    addKnob (layout, "ampDepth", "Depth", 6.0f);
    addKnob (layout, "ampMaster", "Master", 5.0f);

    addSwitch (layout, "cabOn", "Cab On", true);
    addFloat (layout, "cabMic", "Mic Position", { 0.0f, 1.0f, 0.001f }, 0.5f);

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
    static constexpr const char* ids[] { "input", "output", "gateOn", "gateThreshold", "octOn", "octSub", "octUp", "octDry", "odOn", "odType", "odDrive", "odTone", "odLevel", "ampChannel", "ampGain", "ampBass", "ampMid", "ampTreble", "ampPresence", "ampDepth", "ampMaster", "cabOn", "cabMic", "harmOn", "harmMode", "harmKey", "harmScale", "harmV1Steps", "harmV1Level", "harmV2Steps", "harmV2Level", "harmDry", "modOn", "modType", "modRate", "modDepth", "modMix", "modFeedback", "dlyOn", "dlyTime", "dlyFeedback", "dlyMix", "revOn", "revSize", "revMix" };
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
    octaver.prepare (sampleRate);
    overdrive.prepare (sampleRate);
    amp.prepare (sampleRate, maxChunkSize);
    cab.prepare (sampleRate, maxChunkSize);
    delay.prepare (sampleRate, maxChunkSize);
    reverb.prepare (sampleRate, maxChunkSize);
    harmonizer.prepare (sampleRate);
    modulation.prepare (sampleRate, maxChunkSize);
    recorder->prepare (sampleRate);
    tuner.prepare (sampleRate);

    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (Param::input)));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (Param::output)));

    updateParameters();
    setLatencySamples (amp.getLatencySamples());
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
    ampParams.channel = (int) param (Param::ampChannel);
    ampParams.gain = param (Param::ampGain);
    ampParams.bass = param (Param::ampBass);
    ampParams.mid = param (Param::ampMid);
    ampParams.treble = param (Param::ampTreble);
    ampParams.presence = param (Param::ampPresence);
    ampParams.depth = param (Param::ampDepth);
    ampParams.master = param (Param::ampMaster);
    amp.setParams (ampParams);

    cab.setMic (param (Param::cabMic));

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
    delay.setParams (param (Param::dlyTime), param (Param::dlyFeedback), param (Param::dlyMix));
    reverb.setParams (param (Param::revSize), param (Param::revMix));
}

void PomanProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateParameters();

    for (int start = 0; start < buffer.getNumSamples(); start += maxChunkSize)
        processChunk (buffer, start, juce::jmin (maxChunkSize, buffer.getNumSamples() - start));
}

void PomanProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    // Guitar is mono: sum the inputs, run the rig, then widen with the stereo effects.
    auto* mono = monoBuffer.getWritePointer (0);
    const int numInputs = juce::jmax (1, getTotalNumInputChannels());

    for (int i = 0; i < numSamples; ++i)
    {
        float sum = 0.0f;
        for (int ch = 0; ch < getTotalNumInputChannels(); ++ch)
            sum += buffer.getReadPointer (ch)[startSample + i];
        mono[i] = sum / (float) numInputs * inputGain.getNextValue();
    }

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
    if (enabled (Param::octOn))
        octaver.process (mono, numSamples);
    if (enabled (Param::odOn))
        overdrive.process (mono, numSamples);

    juce::dsp::AudioBlock<float> monoBlock (monoBuffer.getArrayOfWritePointers(), 1, 0, (size_t) numSamples);
    amp.process (monoBlock);

    if (enabled (Param::cabOn))
        cab.process (monoBlock);

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

    if (tunerOn)
    {
        juce::FloatVectorOperations::clear (left, numSamples);
        juce::FloatVectorOperations::clear (right, numSamples);
    }

    storePeak (outputPeak, juce::jmax (blockPeak (left, numSamples), blockPeak (right, numSamples)));
}

void PomanProcessor::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    cab.loadImpulseResponse (file);

    // Standalone keeps IRs inside the portable folder relative, so the folder can move between machines.
    const auto root = poman::portableRoot();
    const bool portable = wrapperType == wrapperType_Standalone && file.isAChildOf (root);
    apvts.state.setProperty (irPathProperty, portable ? file.getRelativePathFrom (root) : file.getFullPathName(), nullptr);
}

juce::File PomanProcessor::resolveImpulseResponsePath (const juce::String& path) const
{
    if (path.isEmpty())
        return {};

    return juce::File::isAbsolutePath (path) ? juce::File (path) : poman::portableRoot().getChildFile (path);
}

void PomanProcessor::clearImpulseResponse()
{
    cab.clearImpulseResponse();
    apvts.state.removeProperty (irPathProperty, nullptr);
}

juce::String PomanProcessor::getImpulseResponseName() const
{
    const auto path = apvts.state.getProperty (irPathProperty).toString();
    return resolveImpulseResponsePath (path).getFileName();
}

void PomanProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void PomanProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    applyState (juce::ValueTree::fromXml (*xml));
}

void PomanProcessor::applyState (const juce::ValueTree& state)
{
    apvts.replaceState (state.createCopy());

    const auto file = resolveImpulseResponsePath (apvts.state.getProperty (irPathProperty).toString());
    if (file.existsAsFile())
        cab.loadImpulseResponse (file);
    else
        cab.clearImpulseResponse();
}

juce::AudioProcessorEditor* PomanProcessor::createEditor()
{
    return new PomanEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new PomanProcessor();
}
