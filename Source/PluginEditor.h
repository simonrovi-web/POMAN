#pragma once

#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "StudioView.h"
#include "DrumView.h"
#include "LoopView.h"
#include "LiveView.h"
#include "RightClickable.h"

class PomanLookAndFeel : public juce::LookAndFeel_V4
{
public:
    PomanLookAndFeel();

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos,
                           float startAngle, float endAngle, juce::Slider&) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& background,
                               bool highlighted, bool down) override;
    void drawLinearSlider (juce::Graphics&, int x, int y, int width, int height, float sliderPos, float minPos,
                           float maxPos, juce::Slider::SliderStyle, juce::Slider&) override;
    // A button's "fontSize" property overrides the default text size (e.g. the looper's big switch).
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
};

// Vertical peak meter with decay and a clip LED that latches for a moment.
class LevelMeter : public juce::Component
{
public:
    void pushPeak (float peak);
    // Marks the dB range a calibrated guitar should peak in (input meter only).
    void setTargetZone (float lowDb, float highDb) { targetLow = lowDb; targetHigh = highDb; }
    void paint (juce::Graphics&) override;

private:
    float levelDb = -100.0f;
    float targetLow = 1.0f, targetHigh = 1.0f; // > 0 dB means no zone
    int clipHoldFrames = 0;
};

// Frequency response of the graphic EQ, drawn from the same filter design the audio path uses.
class EqCurve : public juce::Component
{
public:
    void setResponse (const poman::GraphicEQ::Settings& settings, double sampleRate, bool enabled);
    void paint (juce::Graphics&) override;

private:
    poman::GraphicEQ::Settings settings;
    double sampleRate = 48000.0;
    bool enabled = false;
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
        MidiSlider slider;
        juce::Label label;
        juce::String paramId, baseName; // the label shows baseName plus its MIDI CC
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    struct Section
    {
        juce::String title;
        juce::Colour accent;
        std::unique_ptr<MidiToggle> power;
        juce::String powerParamId;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> powerAttachment;
        std::vector<Knob*> knobs;
        juce::Rectangle<int> bounds;
    };

    Knob& addKnob (const juce::String& paramId, const juce::String& name, bool large = false);
    Section& addSection (const juce::String& title, juce::Colour accent, const juce::String& powerParamId,
                         std::initializer_list<Knob*> knobs);
    void layoutKnobs (Section&, juce::Rectangle<int> area);
    void refreshFileLabels();
    void chooseFile (PomanProcessor::FileSlot slot);
    void stepIr (int direction); // loads the previous/next IR of slot A's folder into slot A
    void refreshPresetBox();
    void showSavePresetDialog();
    void setTunerVisible (bool visible);
    void showMidiMenu (const juce::String& target, juce::Component& anchor);
    void refreshMidiLabels();
    juce::String intervalName (int steps) const;
    void refreshHarmonizerInfo();
    void timerCallback() override;

    PomanProcessor& processor;
    PomanLookAndFeel lookAndFeel;

    std::vector<std::unique_ptr<Knob>> knobs;
    std::vector<std::unique_ptr<Section>> sections;
    // Rows are laid out from these named sections, never from creation order.
    Section* gateSection = nullptr;
    Section* octaveSection = nullptr;
    Section* delaySection = nullptr;
    Section* reverbSection = nullptr;
    Section* ampSection = nullptr;
    Section* cabSection = nullptr;
    Section* driveSection = nullptr;
    Section* harmonizerSection = nullptr;
    Section* modSection = nullptr;
    Section* eqSection = nullptr;

    std::array<juce::Slider, poman::GraphicEQ::numBands> eqSliders;
    std::array<juce::Label, poman::GraphicEQ::numBands> eqLabels;
    std::array<std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment>, poman::GraphicEQ::numBands> eqAttachments;
    EqCurve eqCurve;
    poman::GraphicEQ::Settings shownEq;
    bool shownEqEnabled = false;
    bool eqCurveInitialised = false;

    Knob* inputKnob = nullptr;
    Knob* outputKnob = nullptr;

    juce::ComboBox ampModelBox, driveTypeBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> ampModelAttachment, driveTypeAttachment;

    juce::ComboBox harmModeBox, harmKeyBox, harmScaleBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> harmModeAttachment, harmKeyAttachment, harmScaleAttachment;
    std::array<Knob*, 2> harmIntervalKnobs {};
    juce::Label harmInfoLabel;

    juce::ComboBox modTypeBox, revTypeBox, wahModeBox;
    juce::TextButton wahAutoButton { "PEDAL ON" };
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> wahAutoAttachment;

    Section* compSection = nullptr;
    Section* wahSection = nullptr;
    juce::Label compGrLabel;
    Knob* delayTimeKnob = nullptr;
    juce::ComboBox delaySyncBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> delaySyncAttachment;
    juce::TextButton tapButton { "TAP" }, calButton { "CAL" };
    poman::TapTempo tapTempo;
    double tapShownUntil = 0.0, calibrationEnds = 0.0, calibrationShownUntil = 0.0;
    float calibrationPeak = 0.0f;
    void startCalibration();
    void updateCalibration (double now);
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> modTypeAttachment, revTypeAttachment, wahModeAttachment;
    std::array<Knob*, 2> modVariableKnobs {}; // mix/feedback, relabelled as shape/stereo for the tremolo
    int shownModType = -1;
    void refreshModLabels();
    bool shownDiatonic = true;
    int shownScale = -1;

    juce::TextButton irPrevButton { "<" }, irNextButton { ">" };
    juce::TextButton irAButton { "IR A" }, irBButton { "IR B" }, builtInCabButton { "BUILT-IN" }, loadNamButton { "LOAD NAM" };
    juce::Label irLabel, namLabel;
    std::unique_ptr<juce::FileChooser> fileChooser;

    juce::ComboBox presetBox;
    MidiTextButton previousPresetButton { "<" }, nextPresetButton { ">" };
    juce::TextButton savePresetButton { "SAVE" };
    MidiTextButton tunerButton { "TUNER" };
    std::unique_ptr<juce::AlertWindow> saveDialog;
    juce::String shownPresetName;

    LevelMeter inputMeter, outputMeter;

    // Standalone only: RIG / STUDIO pages (a DAW does its own recording).
    juce::TextButton rigPageButton { "RIG" }, studioPageButton { "STUDIO" }, drumsPageButton { "DRUMS" }, loopPageButton { "LOOP" },
                     livePageButton { "LIVE" };
    std::unique_ptr<StudioView> studioView;
    std::unique_ptr<DrumView> drumView;
    std::unique_ptr<LoopView> loopView;
    std::unique_ptr<LiveView> liveView;
    enum class Page { rig, studio, drums, loop, live };
    void showPage (Page page);
    TunerDisplay tunerDisplay;
    poman::TunerReading lastTunerReading; // shared with the LIVE page (one consumer of the tuner FIFO)

    // EN / ES: switches the interface language (saved for next time).
    juce::TextButton languageButton;
    void applyLanguage();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PomanEditor)
};
