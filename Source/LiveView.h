#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "RightClickable.h"
#include "dsp/Tempo.h"
#include "Tuner.h"

class PomanProcessor;

// LIVE page of the standalone app, for playing on stage: the current preset in large type with big
// previous/next switches, large stomp switches for the main effects, TAP and TUNER, and a paged grid
// of presets. Every switch can be MIDI-learned (right-click) for a footswitch.
class LiveView : public juce::Component, private juce::Timer
{
public:
    using MidiMenu = std::function<void (const juce::String& target, juce::Component& anchor)>;
    using TunerSource = std::function<poman::TunerReading()>;

    LiveView (PomanProcessor&, MidiMenu showMidiMenu, TunerSource tunerReading);
    ~LiveView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    void timerCallback() override;
    void refreshGrid();

    PomanProcessor& processor;
    MidiMenu showMidiMenu;
    TunerSource tunerReading;

    MidiTextButton previousButton { "<" }, nextButton { ">" };
    struct Stomp
    {
        std::unique_ptr<MidiTextButton> button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    std::vector<Stomp> stomps;
    MidiTextButton tapButton { "TAP" }, tunerButton { "TUNER" };
    poman::TapTempo tapTempo;

    static constexpr int gridSize = 8;
    std::array<juce::TextButton, gridSize> gridButtons;
    juce::TextButton pagePrevButton { "<" }, pageNextButton { ">" };
    juce::Label hintLabel;
    int page = 0, shownIndex = -2, shownCount = -1;
    juce::String shownName;
    poman::TunerReading tuner;
    bool tunerOn = false;

    juce::Rectangle<int> nameArea;
    juce::Image badge; // the POMAN emblem drawn faintly behind the preset name (loaded on first paint)

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LiveView)
};
