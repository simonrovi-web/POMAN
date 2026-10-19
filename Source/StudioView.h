#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "recorder/MultitrackRecorder.h"

// Studio page of the standalone app: transport, metronome and four track lanes with waveforms.
class StudioView : public juce::Component, private juce::Timer
{
public:
    explicit StudioView (poman::MultitrackRecorder&);
    ~StudioView() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void visibilityChanged() override;

    juce::int64 getVisibleSamples() const { return visibleSamples; }

    // Extra sources for EXPORT MIX (set by the editor to add the drum machine).
    std::function<void (juce::AudioBuffer<float>&)> exportLayer;

private:
    class Lane : public juce::Component
    {
    public:
        Lane (StudioView& owner, int track, juce::Colour accent);
        void paint (juce::Graphics&) override;
        void mouseDown (const juce::MouseEvent&) override;
        void mouseDrag (const juce::MouseEvent&) override;

    private:
        void seekTo (int x);
        StudioView& owner;
        const int track;
        const juce::Colour accent;
    };

    struct TrackControls
    {
        juce::Label name;
        juce::TextButton arm { "R" }, mute { "M" }, solo { "S" }, clear { "X" };
        juce::Slider volume;
        std::unique_ptr<Lane> lane;
    };

    void timerCallback() override;
    void refreshControls();
    void setStatus (const juce::String& text, bool error = false);
    juce::String formatPosition (juce::int64 samples) const;

    poman::MultitrackRecorder& recorder;

    juce::TextButton rewindButton { "|<" }, playButton { "PLAY" }, stopButton { "STOP" }, recordButton { "REC" };
    juce::TextButton clickButton { "CLICK" }, countInButton { "COUNT-IN" };
    juce::TextButton newSessionButton { "NEW SESSION" }, openSessionButton { "OPEN SESSION" }, exportButton { "EXPORT MIX" }, folderButton { "OPEN FOLDER" };
    std::unique_ptr<juce::FileChooser> sessionChooser;
    juce::Slider bpmSlider;
    juce::Label timeLabel, statusLabel, bpmLabel;
    std::array<TrackControls, poman::MultitrackRecorder::numTracks> tracks;

    juce::Rectangle<int> rulerArea;
    juce::int64 visibleSamples = 1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (StudioView)
};
