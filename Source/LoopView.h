#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "RightClickable.h"
#include "dsp/Looper.h"

// LOOP page of the standalone app: one big REC/PLAY/DUB switch like a looper pedal, plus STOP,
// UNDO/REDO and CLEAR. Every button can be MIDI-learned (right-click) for a footswitch.
class LoopView : public juce::Component, private juce::Timer
{
public:
    using MidiMenu = std::function<void (const juce::String& target, juce::Component& anchor)>;
    using CcLookup = std::function<int (const juce::String& target)>;

    LoopView (poman::Looper&, MidiMenu showMidiMenu, CcLookup ccFor);
    ~LoopView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;

    poman::Looper& looper;
    MidiMenu showMidiMenu;
    CcLookup ccFor;

    MidiTextButton mainButton { "REC" }, stopButton { "STOP" }, undoButton { "UNDO" }, clearButton { "CLEAR" };
    juce::TextButton syncButton { "SYNC" };
    juce::Slider levelSlider;
    juce::Label levelLabel, hintLabel, midiLabel;
    juce::Rectangle<float> ringArea;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LoopView)
};
