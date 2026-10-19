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

// Channel: 0 Clean, 1 Rhythm, 2 Lead. Amp knobs 0..10.
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
