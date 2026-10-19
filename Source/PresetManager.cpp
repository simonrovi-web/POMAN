#include "PresetManager.h"
#include "PortablePaths.h"

namespace poman
{
const juce::Identifier PresetManager::presetNameProperty { "presetName" };

namespace
{
struct FactoryPreset
{
    const char* name;
    std::vector<std::pair<const char*, float>> values; // anything not listed uses its default
};

// ampChannel = AmpModel::modelNames() index, odType = Overdrive::typeNames() index. Amp knobs 0..10.
const std::vector<FactoryPreset>& factoryPresets()
{
    static const std::vector<FactoryPreset> presets {
        { "Init", {} },
        { "Clean Glass",
          { { "ampChannel", 0 }, { "ampGain", 4.0f }, { "ampBass", 5.0f }, { "ampMid", 5.5f }, { "ampTreble", 6.0f },
            { "ampPresence", 6.0f }, { "ampDepth", 5.0f }, { "ampMaster", 4.0f }, { "odOn", 0 }, { "gateThreshold", -72.0f },
            { "dlyOn", 1 }, { "dlyTime", 420.0f }, { "dlyFeedback", 0.25f }, { "dlyMix", 0.18f },
            { "revOn", 1 }, { "revSize", 0.55f }, { "revMix", 0.25f } } },
        { "Edge Crunch",
          { { "ampChannel", 1 }, { "ampGain", 3.8f }, { "ampBass", 5.5f }, { "ampMid", 6.2f }, { "ampTreble", 6.0f },
            { "ampPresence", 5.5f }, { "ampDepth", 5.0f }, { "ampMaster", 5.5f },
            { "odOn", 1 }, { "odDrive", 0.25f }, { "odTone", 0.5f }, { "odLevel", 2.0f }, { "gateThreshold", -66.0f } } },
        { "Modern Rhythm",
          { { "ampChannel", 1 }, { "ampGain", 7.0f }, { "ampBass", 6.0f }, { "ampMid", 4.0f }, { "ampTreble", 6.5f },
            { "ampPresence", 6.0f }, { "ampDepth", 6.5f }, { "ampMaster", 5.0f },
            { "odOn", 1 }, { "odDrive", 0.05f }, { "odTone", 0.6f }, { "odLevel", 6.0f }, { "gateThreshold", -58.0f } } },
        { "Drop Tune Chug",
          { { "ampChannel", 2 }, { "ampGain", 6.0f }, { "ampBass", 5.0f }, { "ampMid", 3.5f }, { "ampTreble", 6.5f },
            { "ampPresence", 6.5f }, { "ampDepth", 7.0f }, { "ampMaster", 5.0f },
            { "odOn", 1 }, { "odDrive", 0.0f }, { "odTone", 0.65f }, { "odLevel", 8.0f }, { "gateThreshold", -54.0f },
            { "cabMic", 0.6f } } },
        { "Sub Octave Riff",
          { { "ampChannel", 1 }, { "ampGain", 6.5f }, { "ampBass", 5.0f }, { "ampMid", 5.0f }, { "ampTreble", 6.0f },
            { "ampDepth", 5.5f }, { "octOn", 1 }, { "octSub", 0.7f }, { "octUp", 0.0f }, { "octDry", 1.0f },
            { "odOn", 1 }, { "odDrive", 0.1f }, { "odLevel", 4.0f }, { "gateThreshold", -58.0f } } },
        { "Soaring Lead",
          { { "ampChannel", 2 }, { "ampGain", 7.5f }, { "ampBass", 5.0f }, { "ampMid", 6.5f }, { "ampTreble", 6.0f },
            { "ampPresence", 5.5f }, { "ampDepth", 5.0f }, { "ampMaster", 6.0f },
            { "odOn", 1 }, { "odDrive", 0.25f }, { "odLevel", 3.0f }, { "gateThreshold", -64.0f },
            { "dlyOn", 1 }, { "dlyTime", 450.0f }, { "dlyFeedback", 0.35f }, { "dlyMix", 0.25f },
            { "revOn", 1 }, { "revSize", 0.6f }, { "revMix", 0.2f } } },
        { "US Sparkle",
          { { "ampChannel", 3 }, { "ampGain", 4.5f }, { "ampBass", 5.5f }, { "ampMid", 5.0f }, { "ampTreble", 6.5f },
            { "ampPresence", 6.0f }, { "ampMaster", 5.0f }, { "odOn", 0 }, { "gateThreshold", -72.0f },
            { "revOn", 1 }, { "revSize", 0.45f }, { "revMix", 0.22f } } },
        { "Chime Jangle",
          { { "ampChannel", 4 }, { "ampGain", 5.5f }, { "ampBass", 4.5f }, { "ampMid", 5.5f }, { "ampTreble", 6.0f },
            { "ampPresence", 5.0f }, { "ampMaster", 6.0f }, { "odOn", 1 }, { "odType", 4 }, { "odDrive", 0.35f },
            { "odTone", 0.5f }, { "odLevel", 0.0f }, { "gateThreshold", -70.0f }, { "revOn", 1 }, { "revMix", 0.15f } } },
        { "Brit Stack",
          { { "ampChannel", 5 }, { "ampGain", 7.0f }, { "ampBass", 5.0f }, { "ampMid", 6.5f }, { "ampTreble", 6.5f },
            { "ampPresence", 6.0f }, { "ampMaster", 6.5f }, { "odOn", 1 }, { "odType", 1 }, { "odDrive", 0.3f },
            { "odTone", 0.55f }, { "odLevel", 2.0f }, { "gateThreshold", -64.0f } } },
        { "Hot Rod Solo",
          { { "ampChannel", 6 }, { "ampGain", 6.5f }, { "ampBass", 5.0f }, { "ampMid", 6.0f }, { "ampTreble", 6.0f },
            { "ampPresence", 5.5f }, { "ampMaster", 6.0f }, { "odOn", 1 }, { "odType", 0 }, { "odDrive", 0.1f },
            { "odTone", 0.55f }, { "odLevel", 6.0f }, { "gateThreshold", -62.0f },
            { "dlyOn", 1 }, { "dlyTime", 380.0f }, { "dlyFeedback", 0.3f }, { "dlyMix", 0.2f } } },
        { "Rect Wall",
          { { "ampChannel", 7 }, { "ampGain", 6.5f }, { "ampBass", 5.5f }, { "ampMid", 4.5f }, { "ampTreble", 6.0f },
            { "ampPresence", 6.0f }, { "ampDepth", 6.0f }, { "ampMaster", 5.0f }, { "odOn", 1 }, { "odType", 0 },
            { "odDrive", 0.0f }, { "odTone", 0.6f }, { "odLevel", 7.0f }, { "gateThreshold", -56.0f } } },
        { "Djent Machine",
          { { "ampChannel", 8 }, { "ampGain", 6.0f }, { "ampBass", 5.0f }, { "ampMid", 5.5f }, { "ampTreble", 6.0f },
            { "ampPresence", 6.5f }, { "ampDepth", 6.5f }, { "ampMaster", 5.0f }, { "odOn", 1 }, { "odType", 0 },
            { "odDrive", 0.0f }, { "odTone", 0.7f }, { "odLevel", 9.0f }, { "gateThreshold", -52.0f }, { "cabMic", 0.65f } } },
        { "Fuzz Lead",
          { { "ampChannel", 0 }, { "ampGain", 6.0f }, { "ampBass", 5.5f }, { "ampMid", 6.0f }, { "ampTreble", 5.0f },
            { "ampMaster", 6.0f }, { "odOn", 1 }, { "odType", 3 }, { "odDrive", 0.7f }, { "odTone", 0.55f },
            { "odLevel", -2.0f }, { "gateThreshold", -60.0f }, { "dlyOn", 1 }, { "dlyTime", 420.0f },
            { "dlyFeedback", 0.3f }, { "dlyMix", 0.2f }, { "revOn", 1 }, { "revMix", 0.18f } } },
        { "Grunge Dist",
          { { "ampChannel", 5 }, { "ampGain", 4.0f }, { "ampBass", 6.0f }, { "ampMid", 5.0f }, { "ampTreble", 5.5f },
            { "ampMaster", 6.0f }, { "odOn", 1 }, { "odType", 2 }, { "odDrive", 0.6f }, { "odTone", 0.45f },
            { "odLevel", 0.0f }, { "gateThreshold", -60.0f } } },
        { "Twin Harmony Lead",
          { { "ampChannel", 6 }, { "ampGain", 6.5f }, { "ampBass", 5.0f }, { "ampMid", 6.0f }, { "ampTreble", 6.0f },
            { "ampPresence", 5.5f }, { "ampMaster", 6.0f }, { "odOn", 1 }, { "odDrive", 0.1f }, { "odLevel", 5.0f },
            { "gateThreshold", -62.0f }, { "harmOn", 1 }, { "harmMode", 1 }, { "harmKey", 4 }, { "harmScale", 1 },
            { "harmV1Steps", 2 }, { "harmV1Level", 0.8f }, { "harmV2Level", 0.0f }, { "harmDry", 1.0f },
            { "dlyOn", 1 }, { "dlyTime", 380.0f }, { "dlyFeedback", 0.25f }, { "dlyMix", 0.18f },
            { "revOn", 1 }, { "revMix", 0.15f } } },
        { "Octave Shimmer",
          { { "ampChannel", 0 }, { "ampGain", 3.5f }, { "ampTreble", 6.0f }, { "ampMaster", 4.5f }, { "odOn", 0 },
            { "harmOn", 1 }, { "harmMode", 0 }, { "harmV1Steps", 12 }, { "harmV1Level", 0.5f },
            { "harmV2Steps", -12 }, { "harmV2Level", 0.4f }, { "harmDry", 1.0f },
            { "dlyOn", 1 }, { "dlyTime", 520.0f }, { "dlyFeedback", 0.4f }, { "dlyMix", 0.3f },
            { "revOn", 1 }, { "revSize", 0.85f }, { "revMix", 0.4f } } },
        { "51 Chug",
          { { "ampChannel", 9 }, { "ampGain", 6.0f }, { "ampBass", 5.5f }, { "ampMid", 5.0f }, { "ampTreble", 6.0f },
            { "ampPresence", 6.0f }, { "ampDepth", 6.5f }, { "ampMaster", 5.0f }, { "odOn", 1 }, { "odType", 0 },
            { "odDrive", 0.0f }, { "odTone", 0.6f }, { "odLevel", 8.0f }, { "gateThreshold", -54.0f } } },
        { "51 Lead",
          { { "ampChannel", 9 }, { "ampGain", 7.0f }, { "ampBass", 5.0f }, { "ampMid", 6.0f }, { "ampTreble", 5.5f },
            { "ampPresence", 5.5f }, { "ampDepth", 5.5f }, { "ampMaster", 5.5f }, { "odOn", 1 }, { "odDrive", 0.1f },
            { "odLevel", 5.0f }, { "gateThreshold", -60.0f }, { "dlyOn", 1 }, { "dlyTime", 400.0f },
            { "dlyFeedback", 0.3f }, { "dlyMix", 0.2f }, { "revOn", 1 }, { "revMix", 0.15f } } },
        { "Chorus Clean",
          { { "ampChannel", 0 }, { "ampGain", 3.5f }, { "ampBass", 5.0f }, { "ampMid", 5.0f }, { "ampTreble", 6.0f },
            { "ampMaster", 4.5f }, { "odOn", 0 }, { "gateThreshold", -72.0f }, { "modOn", 1 }, { "modType", 0 },
            { "modRate", 0.7f }, { "modDepth", 0.55f }, { "modMix", 0.5f }, { "modFeedback", 0.1f },
            { "revOn", 1 }, { "revSize", 0.5f }, { "revMix", 0.22f } } },
        { "Jet Flanger",
          { { "ampChannel", 5 }, { "ampGain", 6.0f }, { "ampMid", 6.0f }, { "ampMaster", 6.0f }, { "odOn", 0 },
            { "gateThreshold", -64.0f }, { "modOn", 1 }, { "modType", 1 }, { "modRate", 0.25f }, { "modDepth", 0.8f },
            { "modMix", 0.5f }, { "modFeedback", 0.7f } } },
        { "Surf Tremolo",
          { { "ampChannel", 3 }, { "ampGain", 4.0f }, { "ampTreble", 6.5f }, { "ampMaster", 5.0f }, { "odOn", 0 },
            { "gateThreshold", -72.0f }, { "modOn", 1 }, { "modType", 2 }, { "modRate", 5.5f }, { "modDepth", 0.7f },
            { "modMix", 0.2f }, { "modFeedback", 0.0f }, { "revOn", 1 }, { "revSize", 0.7f }, { "revMix", 0.35f } } },
        { "Ambient Swell",
          { { "ampChannel", 0 }, { "ampGain", 3.0f }, { "ampTreble", 5.5f }, { "ampMaster", 4.0f }, { "odOn", 0 },
            { "octOn", 1 }, { "octSub", 0.0f }, { "octUp", 0.35f }, { "octDry", 1.0f },
            { "dlyOn", 1 }, { "dlyTime", 600.0f }, { "dlyFeedback", 0.55f }, { "dlyMix", 0.35f },
            { "revOn", 1 }, { "revSize", 0.9f }, { "revMix", 0.45f } } },
    };
    return presets;
}

constexpr const char* fileExtension = ".poman";
} // namespace

PresetManager::PresetManager (juce::AudioProcessorValueTreeState& state, bool standalone, StateLoader loader)
    : apvts (state), loadFullState (std::move (loader))
{
    const auto portable = portableRoot().getChildFile ("Presets");
    if (standalone && portableRoot().hasWriteAccess())
        userFolder = portable;
    else
        userFolder = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("POMAN").getChildFile ("Presets");

    refreshUserPresets();
}

juce::File PresetManager::getUserFolder() const { return userFolder; }

void PresetManager::refreshUserPresets()
{
    userPresets.clear();
    if (userFolder.isDirectory())
        userPresets = userFolder.findChildFiles (juce::File::findFiles, false, juce::String ("*") + fileExtension);

    std::sort (userPresets.begin(), userPresets.end(),
               [] (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()) < 0; });
}

int PresetManager::getNumFactoryPresets() const { return (int) factoryPresets().size(); }

juce::StringArray PresetManager::getPresetNames() const
{
    juce::StringArray names;
    for (const auto& p : factoryPresets())
        names.add (p.name);
    for (const auto& f : userPresets)
        names.add (f.getFileNameWithoutExtension());
    return names;
}

juce::String PresetManager::getCurrentName() const
{
    return apvts.state.getProperty (presetNameProperty, "Init").toString();
}

int PresetManager::getCurrentIndex() const
{
    return getPresetNames().indexOf (getCurrentName());
}

void PresetManager::applyFactory (int index)
{
    const auto& preset = factoryPresets()[(size_t) index];

    for (auto* p : apvts.processor.getParameters())
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (p))
            if (ranged->getParameterID() != "input" && ranged->getParameterID() != "output")
                ranged->setValueNotifyingHost (ranged->getDefaultValue());

    for (const auto& [id, value] : preset.values)
        if (auto* p = apvts.getParameter (id))
            p->setValueNotifyingHost (p->convertTo0to1 (value));

    apvts.state.setProperty (presetNameProperty, preset.name, nullptr);
}

void PresetManager::loadPreset (int index)
{
    if (index < 0)
        return;

    if (index < getNumFactoryPresets())
    {
        applyFactory (index);
        return;
    }

    const auto file = userPresets[index - getNumFactoryPresets()];
    if (const auto xml = juce::parseXML (file); xml != nullptr && xml->hasTagName (apvts.state.getType()))
    {
        auto tree = juce::ValueTree::fromXml (*xml);
        tree.setProperty (presetNameProperty, file.getFileNameWithoutExtension(), nullptr);
        loadFullState (tree);
    }
}

void PresetManager::loadNext()
{
    const int count = getPresetNames().size();
    loadPreset ((getCurrentIndex() + 1 + count) % count);
}

void PresetManager::loadPrevious()
{
    const int count = getPresetNames().size();
    const int current = getCurrentIndex();
    loadPreset (((current < 0 ? 0 : current) - 1 + count) % count);
}

bool PresetManager::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty() || ! userFolder.createDirectory())
        return false;

    apvts.state.setProperty (presetNameProperty, clean, nullptr);
    const auto xml = apvts.copyState().createXml();
    if (xml == nullptr || ! xml->writeTo (userFolder.getChildFile (clean + fileExtension)))
        return false;

    refreshUserPresets();
    return true;
}
} // namespace poman
