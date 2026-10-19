#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace poman
{
// Factory presets (defined in code) followed by user presets (.poman XML files in getUserFolder()).
class PresetManager
{
public:
    using StateLoader = std::function<void (const juce::ValueTree&)>;

    PresetManager (juce::AudioProcessorValueTreeState& state, bool standalone, StateLoader loadFullState);

    juce::StringArray getPresetNames() const;
    int getNumFactoryPresets() const;
    int getCurrentIndex() const;
    juce::String getCurrentName() const;

    void loadPreset (int index);
    void loadNext();
    void loadPrevious();
    bool saveUserPreset (const juce::String& name);
    void refreshUserPresets();
    juce::File getUserFolder() const;

    static const juce::Identifier presetNameProperty;

private:
    void applyFactory (int index);

    juce::AudioProcessorValueTreeState& apvts;
    StateLoader loadFullState;
    juce::File userFolder;
    juce::Array<juce::File> userPresets;
};
} // namespace poman
