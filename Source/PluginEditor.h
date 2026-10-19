#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "StudioView.h"

class PomanLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PomanLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
};

// Vertical peak meter with decay and a clip LED that latches for a moment.
class LevelMeter : public juce::Component
{
public:
    void pushPeak (float peak);
    void paint (juce::Graphics&) override;

private:
    float levelDb = -100.0f;
    int clipHoldFrames = 0;
};

class TunerDisplay : public juce::Component
{
public:
    void setReading (const poman::TunerReading& reading);
    void paint (juce::Graphics&) override;

private:
    poman::TunerReading current;
    float smoothedCents = 0.0f;
    int silentFrames = 100;
};

class PomanEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit PomanEditor (PomanProcessor&);
    ~PomanEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct Knob
    {
        juce::Slider slider;
        juce::Label label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        juce::Colour accent;
        std::unique_ptr<juce::ToggleButton> power;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& name, bool large = false);
    Section& addSection (const juce::String& title, juce::Colour accent, const juce::String& powerParamId,
                         std::initializer_list<Knob*> knobs);
    void layoutKnobs (Section&, juce::Rectangle<int> area);
    void refreshImpulseResponseLabel();
    void refreshPresetBox();
    void showSavePresetDialog();
    void setTunerVisible (bool visible);
    juce::String intervalName (int steps) const;
    void refreshHarmonizerInfo();
    void timerCallback() override;

    PomanProcessor& processor;
    PomanLookAndFeel lookAndFeel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Section>> sections;
    Section* ampSection = nullptr;
    Section* cabSection = nullptr;
    Section* driveSection = nullptr;
    Section* harmonizerSection = nullptr;

    Knob* inputKnob = nullptr;
    Knob* outputKnob = nullptr;

    juce::ComboBox ampModelBox, driveTypeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampModelAttachment, driveTypeAttachment;

    juce::ComboBox harmModeBox, harmKeyBox, harmScaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> harmModeAttachment, harmKeyAttachment, harmScaleAttachment;
    std::array<Knob*, 2> harmIntervalKnobs {};
    juce::Label harmInfoLabel;
    bool shownDiatonic = true;
    int shownScale = -1;

    juce::TextButton loadIrButton { "LOAD IR" }, clearIrButton { "BUILT-IN" };
    juce::Label irLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::ComboBox presetBox;
    juce::TextButton previousPresetButton { "<" }, nextPresetButton { ">" }, savePresetButton { "SAVE" };
    juce::TextButton tunerButton { "TUNER" };
    std::unique_ptr<juce::AlertWindow> saveDialog;
    juce::String shownPresetName;

    LevelMeter inputMeter, outputMeter;

    // Standalone only: RIG / STUDIO pages (a DAW does its own recording).
    juce::TextButton rigPageButton { "RIG" }, studioPageButton { "STUDIO" };
    std::unique_ptr<StudioView> studioView;
    void showStudio (bool studio);
    TunerDisplay tunerDisplay;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PomanEditor)
};
