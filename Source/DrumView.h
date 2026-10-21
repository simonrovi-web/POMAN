#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "dsp/DrumMachine.h"
#include "recorder/MultitrackRecorder.h"

// DRUMS page of the standalone app: step grid, pattern presets and drum mixer. It shares the
// studio transport and tempo, so the drums play along with (and under) recording.
class DrumView : public juce::Component, private juce::Timer
{
public:
    DrumView (poman::DrumMachine&, poman::MultitrackRecorder&);
    ~DrumView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    class Grid : public juce::Component
    {
    public:
        explicit Grid (DrumView& owner);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;
        void mouseUp (const juce::MouseEvent&) override { paintValue = -1; }
        int playingStep = -1;

    private:
        bool cellAt (juce::Point<int> p, int& instrument, int& step) const;
        DrumView& owner;
        int paintValue = -1; // value painted while dragging
    };

    void timerCallback() override;
    void refreshControls();
    void markEdited();

    poman::DrumMachine& drums;
    poman::MultitrackRecorder& recorder;

    juce::TextButton rewindButton { "|<" }, playButton { "PLAY" }, stopButton { "STOP" }, drumsOnButton { "DRUMS ON" }, clearButton { "CLEAR" };
    juce::TextButton loadKitButton { "LOAD KIT" };
    std::unique_ptr<juce::FileChooser> kitChooser;
    void chooseSampleKit();
    void refreshKitTooltip();
    juce::ComboBox patternBox, kitBox;
    juce::Slider bpmSlider, swingSlider, volumeSlider, roomSlider;
    juce::Label bpmLabel, swingLabel, volumeLabel, roomLabel, kitLabel, hintLabel, positionLabel;
    std::array<juce::Label, poman::DrumMachine::numInstruments> names;
    std::array<juce::Slider, poman::DrumMachine::numInstruments> levels;
    Grid grid { *this };
    juce::Rectangle<int> stepNumberArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (DrumView)
};
