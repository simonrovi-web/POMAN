#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"

class AbyssalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    AbyssalLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
};

class AbyssalEditor : public juce::AudioProcessorEditor
{
public:
    explicit AbyssalEditor (AbyssalProcessor&);
    ~AbyssalEditor() override;

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
    void refreshChannelButtons (int channel);
    void refreshImpulseResponseLabel();

    AbyssalProcessor& processor;
    AbyssalLookAndFeel lookAndFeel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Section>> sections;
    Section* ampSection = nullptr;
    Section* cabSection = nullptr;

    Knob* inputKnob = nullptr;
    Knob* outputKnob = nullptr;

    std::array<juce::TextButton, 3> channelButtons;
    std::unique_ptr<juce::ParameterAttachment> channelAttachment;

    juce::TextButton loadIrButton { "LOAD IR" }, clearIrButton { "BUILT-IN" };
    juce::Label irLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AbyssalEditor)
};
