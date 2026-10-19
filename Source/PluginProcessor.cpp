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
    layout.add (std::make_unique<AudioParameterFloat> (ParameterID { id, 1 }, name, range, defaultValue,
                                                       juce::AudioParameterFloatAttributes().withLabel (unit)));
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

juce::AudioProcessorValueTreeState::ParameterLayout AbyssalProcessor::createParameterLayout()
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
    addFloat (layout, "odDrive", "Drive", { 0.0f, 1.0f, 0.001f }, 0.15f);
    addFloat (layout, "odTone", "Drive Tone", { 0.0f, 1.0f, 0.001f }, 0.55f);
    addFloat (layout, "odLevel", "Drive Level", { -12.0f, 12.0f, 0.1f }, 3.0f, "dB");

    layout.add (std::make_unique<AudioParameterChoice> (ParameterID { "ampChannel", 1 }, "Amp Channel",
                                                        juce::StringArray { "Clean", "Rhythm", "Lead" }, 1));
    addKnob (layout, "ampGain", "Amp Gain", 6.0f);
    addKnob (layout, "ampBass", "Bass", 5.5f);
    addKnob (layout, "ampMid", "Mid", 4.0f);
    addKnob (layout, "ampTreble", "Treble", 6.0f);
    addKnob (layout, "ampPresence", "Presence", 5.5f);
    addKnob (layout, "ampDepth", "Depth", 6.0f);
    addKnob (layout, "ampMaster", "Master", 5.0f);

    addSwitch (layout, "cabOn", "Cab On", true);
    addFloat (layout, "cabMic", "Mic Position", { 0.0f, 1.0f, 0.001f }, 0.5f);

    addSwitch (layout, "dlyOn", "Delay On", false);
    addFloat (layout, "dlyTime", "Delay Time", { 40.0f, 1500.0f, 1.0f, 0.5f }, 380.0f, "ms");
    addFloat (layout, "dlyFeedback", "Delay Feedback", { 0.0f, 0.9f, 0.001f }, 0.3f);
    addFloat (layout, "dlyMix", "Delay Mix", { 0.0f, 1.0f, 0.001f }, 0.25f);

    addSwitch (layout, "revOn", "Reverb On", false);
    addFloat (layout, "revSize", "Reverb Size", { 0.0f, 1.0f, 0.001f }, 0.5f);
    addFloat (layout, "revMix", "Reverb Mix", { 0.0f, 1.0f, 0.001f }, 0.2f);

    return layout;
}

AbyssalProcessor::AbyssalProcessor()
    : AudioProcessor (BusesProperties()
                          .withInput ("Input", juce::AudioChannelSet::stereo(), true)
                          .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      apvts (*this, nullptr, "AbyssalState", createParameterLayout())
{
    static constexpr const char* ids[] { "input", "output", "gateOn", "gateThreshold", "octOn", "octSub", "octUp", "octDry", "odOn", "odDrive", "odTone", "odLevel", "ampChannel", "ampGain", "ampBass", "ampMid", "ampTreble", "ampPresence", "ampDepth", "ampMaster", "cabOn", "cabMic", "dlyOn", "dlyTime", "dlyFeedback", "dlyMix", "revOn", "revSize", "revMix" };
    static_assert (std::size (ids) == (size_t) Param::count);

    for (size_t i = 0; i < rawParameters.size(); ++i)
    {
        rawParameters[i] = apvts.getRawParameterValue (ids[i]);
        jassert (rawParameters[i] != nullptr);
    }
}

bool AbyssalProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto in = layouts.getMainInputChannelSet();
    return layouts.getMainOutputChannelSet() == juce::AudioChannelSet::stereo()
        && (in == juce::AudioChannelSet::mono() || in == juce::AudioChannelSet::stereo());
}

void AbyssalProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
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

    inputGain.reset (sampleRate, 0.03);
    outputGain.reset (sampleRate, 0.03);
    inputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (Param::input)));
    outputGain.setCurrentAndTargetValue (juce::Decibels::decibelsToGain (param (Param::output)));

    updateParameters();
    setLatencySamples (amp.getLatencySamples());
}

void AbyssalProcessor::updateParameters()
{
    inputGain.setTargetValue (juce::Decibels::decibelsToGain (param (Param::input)));
    outputGain.setTargetValue (juce::Decibels::decibelsToGain (param (Param::output)));

    gate.setThreshold (param (Param::gateThreshold));
    octaver.setMix (param (Param::octSub), param (Param::octUp), param (Param::octDry));
    overdrive.setParams (param (Param::odDrive), param (Param::odTone), param (Param::odLevel));

    abyssal::AmpParams ampParams;
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
    delay.setParams (param (Param::dlyTime), param (Param::dlyFeedback), param (Param::dlyMix));
    reverb.setParams (param (Param::revSize), param (Param::revMix));
}

void AbyssalProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer&)
{
    juce::ScopedNoDenormals noDenormals;

    for (auto ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());

    updateParameters();

    for (int start = 0; start < buffer.getNumSamples(); start += maxChunkSize)
        processChunk (buffer, start, juce::jmin (maxChunkSize, buffer.getNumSamples() - start));
}

void AbyssalProcessor::processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
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
    juce::FloatVectorOperations::copy (left, mono, numSamples);
    juce::FloatVectorOperations::copy (right, mono, numSamples);

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
}

void AbyssalProcessor::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return;

    cab.loadImpulseResponse (file);

    // Standalone keeps IRs inside the portable folder relative, so the folder can move between machines.
    const auto root = abyssal::portableRoot();
    const bool portable = wrapperType == wrapperType_Standalone && file.isAChildOf (root);
    apvts.state.setProperty (irPathProperty, portable ? file.getRelativePathFrom (root) : file.getFullPathName(), nullptr);
}

juce::File AbyssalProcessor::resolveImpulseResponsePath (const juce::String& path) const
{
    if (path.isEmpty())
        return {};

    return juce::File::isAbsolutePath (path) ? juce::File (path) : abyssal::portableRoot().getChildFile (path);
}

void AbyssalProcessor::clearImpulseResponse()
{
    cab.clearImpulseResponse();
    apvts.state.removeProperty (irPathProperty, nullptr);
}

juce::String AbyssalProcessor::getImpulseResponseName() const
{
    const auto path = apvts.state.getProperty (irPathProperty).toString();
    return resolveImpulseResponsePath (path).getFileName();
}

void AbyssalProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = apvts.copyState().createXml())
        copyXmlToBinary (*xml, destData);
}

void AbyssalProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    const auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (apvts.state.getType()))
        return;

    apvts.replaceState (juce::ValueTree::fromXml (*xml));

    const auto file = resolveImpulseResponsePath (apvts.state.getProperty (irPathProperty).toString());
    if (file.existsAsFile())
        cab.loadImpulseResponse (file);
    else
        cab.clearImpulseResponse();
}

juce::AudioProcessorEditor* AbyssalProcessor::createEditor()
{
    return new AbyssalEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AbyssalProcessor();
}
