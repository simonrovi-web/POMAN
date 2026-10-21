#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "dsp/BackingTrack.h"

// JAM page of the standalone app: play along with a song file, slowed down without changing its pitch
// or transposed without changing its speed, with an A-B loop for practising a passage.
class JamView : public juce::Component, private juce::Timer
{
public:
    explicit JamView (poman::BackingTrack&);
    ~JamView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

private:
    struct Waveform : juce::Component
    {
        explicit Waveform (JamView& o) : owner (o) {}
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent& e) override { mouseDown (e); }
        JamView& owner;
        std::vector<float> peaks;
    };

    void timerCallback() override;
    void chooseSong();
    void refreshSong();
    static juce::String timeText (double seconds);

    poman::BackingTrack& track;
    Waveform waveform { *this };
    juce::TextButton loadButton { "LOAD SONG" }, rewindButton { "|<" }, playButton { "PLAY" },
                     setAButton { "A" }, setBButton { "B" }, loopButton { "LOOP" };
    juce::Slider speedSlider, pitchSlider, volumeSlider;
    juce::Label songLabel, timeLabel, speedLabel, pitchLabel, volumeLabel, hintLabel;
    std::unique_ptr<juce::FileChooser> chooser;
    juce::String shownSong;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (JamView)
};
