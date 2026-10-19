#include "StudioView.h"
#include "Palette.h"

namespace
{
const std::array<juce::Colour, poman::MultitrackRecorder::numTracks> trackColours {
    palette::biolume, palette::ember, palette::aurora, palette::kelp
};
const juce::Colour recordRed { 0xffff3b30 };
} // namespace

//==============================================================================
StudioView::Lane::Lane (StudioView& o, int t, juce::Colour c) : owner (o), track (t), accent (c) {}

void StudioView::Lane::paint (juce::Graphics& g)
{
    auto& recorder = owner.recorder;
    const auto bounds = getLocalBounds().toFloat();
    const double samplesPerPixel = (double) owner.getVisibleSamples() / juce::jmax (1, getWidth());
    const float mid = bounds.getCentreY();

    g.setColour (palette::abyss);
    g.fillRoundedRectangle (bounds, 4.0f);

    // One faint line per second.
    const double sampleRate = recorder.getSampleRate();
    g.setColour (palette::panelEdge.withAlpha (0.5f));
    for (double s = 0.0; s < (double) owner.getVisibleSamples(); s += sampleRate)
        g.fillRect ((float) (s / samplesPerPixel), 0.0f, 1.0f, bounds.getHeight());

    const auto info = recorder.getTrackInfo (track);
    const bool recordingHere = recorder.getState() == poman::MultitrackRecorder::State::recording
                            && recorder.getRecordingTrack() == track;

    if (info.hasAudio && ! recordingHere)
    {
        const auto& peaks = *info.peaks;
        g.setColour (accent.withAlpha (recorder.isMuted (track) ? 0.3f : 0.9f));
        for (int x = 0; x < getWidth(); ++x)
        {
            const auto from = (juce::int64) (x * samplesPerPixel) - info.start;
            const auto to = (juce::int64) ((x + 1) * samplesPerPixel) - info.start;
            if (to < 0 || from >= info.length)
                continue;

            const auto p0 = (size_t) juce::jmax ((juce::int64) 0, from / poman::MultitrackRecorder::samplesPerPeak);
            const auto p1 = (size_t) juce::jmin ((juce::int64) peaks.size() - 1, to / poman::MultitrackRecorder::samplesPerPeak);
            float peak = 0.0f;
            for (auto p = p0; p <= p1; ++p)
                peak = juce::jmax (peak, peaks[p]);

            const float h = juce::jmax (1.0f, juce::jmin (1.0f, peak) * bounds.getHeight() * 0.9f);
            g.fillRect ((float) x, mid - h * 0.5f, 1.0f, h);
        }

        const auto startX = (float) (info.start / samplesPerPixel);
        g.setColour (accent.withAlpha (0.5f));
        g.fillRect (startX, 0.0f, 2.0f, bounds.getHeight());
    }

    if (recordingHere)
    {
        const auto x0 = (float) (recorder.getRecordingStart() / samplesPerPixel);
        const auto x1 = (float) (recorder.getPlayhead() / samplesPerPixel);
        g.setColour (recordRed.withAlpha (0.25f));
        g.fillRect (x0, 0.0f, juce::jmax (1.0f, x1 - x0), bounds.getHeight());
        g.setColour (recordRed);
        g.setFont (juce::FontOptions (12.0f, juce::Font::bold));
        g.drawText ("RECORDING", juce::Rectangle<float> (x0 + 6.0f, 4.0f, 100.0f, 16.0f), juce::Justification::centredLeft);
    }
    else if (! info.hasAudio)
    {
        g.setColour (palette::dimText.withAlpha (0.6f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawText ("empty", bounds.reduced (10.0f, 0.0f), juce::Justification::centredLeft);
    }

    const auto playheadX = (float) (recorder.getPlayhead() / samplesPerPixel);
    g.setColour (palette::text);
    g.fillRect (playheadX, 0.0f, 1.5f, bounds.getHeight());
}

void StudioView::Lane::seekTo (int x)
{
    const double samplesPerPixel = (double) owner.getVisibleSamples() / juce::jmax (1, getWidth());
    owner.recorder.setPlayhead ((juce::int64) (juce::jmax (0, x) * samplesPerPixel));
    owner.repaint();
}

void StudioView::Lane::mouseDown (const juce::MouseEvent& e) { seekTo (e.x); }
void StudioView::Lane::mouseDrag (const juce::MouseEvent& e) { seekTo (e.x); }

//==============================================================================
StudioView::StudioView (poman::MultitrackRecorder& r) : recorder (r)
{
    setOpaque (true);

    for (auto* b : { &rewindButton, &playButton, &stopButton, &recordButton, &clickButton, &newSessionButton, &exportButton, &folderButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, palette::biolume);
        addAndMakeVisible (*b);
    }
    recordButton.setColour (juce::TextButton::buttonOnColourId, recordRed);
    clickButton.setColour (juce::TextButton::buttonOnColourId, palette::kelp);

    rewindButton.onClick = [this] { recorder.rewind(); repaint(); };
    playButton.onClick = [this] { recorder.play(); refreshControls(); };
    stopButton.onClick = [this]
    {
        const bool wasRecording = recorder.getState() == poman::MultitrackRecorder::State::recording;
        recorder.stop();
        if (wasRecording)
            setStatus ("Take saved in " + recorder.getSessionFolder().getFileName());
        refreshControls();
    };
    recordButton.onClick = [this]
    {
        const auto result = recorder.record();
        if (result.failed())
            setStatus (result.getErrorMessage(), true);
        else
            setStatus ("Recording track " + juce::String (recorder.getRecordingTrack() + 1) + "...");
        refreshControls();
    };
    clickButton.onClick = [this] { recorder.setMetronome (! recorder.isMetronomeOn()); refreshControls(); };

    newSessionButton.onClick = [this]
    {
        auto options = juce::MessageBoxOptions::makeOptionsOkCancel (juce::MessageBoxIconType::QuestionIcon, "New session",
                                                                      "Clear all tracks and start a new session?\nRecorded WAV files stay on disk.",
                                                                      "New session", "Cancel", this);
        juce::NativeMessageBox::showAsync (options, [safe = juce::Component::SafePointer<StudioView> (this)] (int result)
        {
            if (safe != nullptr && result == 1)
            {
                safe->recorder.newSession();
                safe->setStatus ("New session: " + safe->recorder.getSessionFolder().getFileName());
                safe->refreshControls();
            }
        });
    };

    exportButton.onClick = [this]
    {
        juce::File written;
        const auto result = recorder.exportMix (written);
        setStatus (result.wasOk() ? "Exported " + written.getFullPathName() : result.getErrorMessage(), result.failed());
    };

    folderButton.onClick = [this]
    {
        auto folder = recorder.getSessionFolder();
        if (! folder.isDirectory())
            folder = folder.getParentDirectory();
        folder.createDirectory();
        folder.startAsProcess();
    };

    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setValue (recorder.getBpm(), juce::dontSendNotification);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 24);
    bpmSlider.onValueChange = [this] { recorder.setBpm (bpmSlider.getValue()); };
    addAndMakeVisible (bpmSlider);

    bpmLabel.setText ("BPM", juce::dontSendNotification);
    bpmLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (bpmLabel);

    timeLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 22.0f, juce::Font::bold));
    timeLabel.setColour (juce::Label::textColourId, palette::text);
    timeLabel.setColour (juce::Label::backgroundColourId, palette::abyss);
    timeLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (timeLabel);

    statusLabel.setFont (juce::FontOptions (12.0f));
    statusLabel.setJustificationType (juce::Justification::centredRight);
    addAndMakeVisible (statusLabel);
    setStatus ("Arm a track (R), press REC to record, STOP to keep the take.");

    for (int t = 0; t < poman::MultitrackRecorder::numTracks; ++t)
    {
        auto& tc = tracks[(size_t) t];
        const auto colour = trackColours[(size_t) t];

        tc.name.setText ("TRACK " + juce::String (t + 1), juce::dontSendNotification);
        tc.name.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        tc.name.setColour (juce::Label::textColourId, colour);
        addAndMakeVisible (tc.name);

        tc.arm.setColour (juce::TextButton::buttonOnColourId, recordRed);
        tc.mute.setColour (juce::TextButton::buttonOnColourId, juce::Colour (0xffffc94a));
        tc.solo.setColour (juce::TextButton::buttonOnColourId, palette::biolume);
        tc.clear.setColour (juce::TextButton::buttonOnColourId, palette::ember);
        tc.arm.setTooltip ("Arm for recording");
        tc.mute.setTooltip ("Mute");
        tc.solo.setTooltip ("Solo");
        tc.clear.setTooltip ("Clear this track (the WAV file stays on disk)");

        tc.arm.onClick = [this, t] { recorder.armTrack (recorder.getArmedTrack() == t ? -1 : t); refreshControls(); };
        tc.mute.onClick = [this, t] { recorder.setMuted (t, ! recorder.isMuted (t)); refreshControls(); };
        tc.solo.onClick = [this, t] { recorder.setSolo (t, ! recorder.isSolo (t)); refreshControls(); };
        tc.clear.onClick = [this, t] { recorder.clearTrack (t); refreshControls(); };

        for (auto* b : { &tc.arm, &tc.mute, &tc.solo, &tc.clear })
            addAndMakeVisible (*b);

        tc.volume.setSliderStyle (juce::Slider::LinearHorizontal);
        tc.volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        tc.volume.setRange (0.0, 1.25, 0.001);
        tc.volume.setValue (recorder.getVolume (t), juce::dontSendNotification);
        tc.volume.setColour (juce::Slider::trackColourId, colour.withAlpha (0.7f));
        tc.volume.setColour (juce::Slider::thumbColourId, colour);
        tc.volume.setColour (juce::Slider::backgroundColourId, palette::abyss);
        tc.volume.onValueChange = [this, t] { recorder.setVolume (t, (float) tracks[(size_t) t].volume.getValue()); };
        addAndMakeVisible (tc.volume);

        tc.lane = std::make_unique<Lane> (*this, t, colour);
        addAndMakeVisible (*tc.lane);
    }

    refreshControls();
}

StudioView::~StudioView() { stopTimer(); }

void StudioView::visibilityChanged()
{
    if (isVisible())
        startTimerHz (30);
    else
        stopTimer();
}

void StudioView::setStatus (const juce::String& text, bool error)
{
    statusLabel.setText (text, juce::dontSendNotification);
    statusLabel.setColour (juce::Label::textColourId, error ? recordRed : palette::dimText);
}

juce::String StudioView::formatPosition (juce::int64 samples) const
{
    const double seconds = (double) samples / recorder.getSampleRate();
    const auto minutes = (int) (seconds / 60.0);
    const double beats = seconds * recorder.getBpm() / 60.0;
    return juce::String::formatted ("%02d:%06.3f   %3d.%d", minutes, seconds - minutes * 60.0,
                                    (int) (beats / 4.0) + 1, (int) std::fmod (beats, 4.0) + 1);
}

void StudioView::refreshControls()
{
    using State = poman::MultitrackRecorder::State;
    const auto state = recorder.getState();
    playButton.setToggleState (state == State::playing, juce::dontSendNotification);
    recordButton.setToggleState (state == State::recording, juce::dontSendNotification);
    clickButton.setToggleState (recorder.isMetronomeOn(), juce::dontSendNotification);

    for (int t = 0; t < poman::MultitrackRecorder::numTracks; ++t)
    {
        auto& tc = tracks[(size_t) t];
        tc.arm.setToggleState (recorder.getArmedTrack() == t, juce::dontSendNotification);
        tc.mute.setToggleState (recorder.isMuted (t), juce::dontSendNotification);
        tc.solo.setToggleState (recorder.isSolo (t), juce::dontSendNotification);
        tc.clear.setEnabled (recorder.getTrackInfo (t).hasAudio);
    }
}

void StudioView::timerCallback()
{
    // Show at least a minute, and always some room after the last take and the playhead.
    const double sr = recorder.getSampleRate();
    const auto wanted = juce::jmax ((juce::int64) (60.0 * sr),
                                    (juce::int64) ((double) recorder.getSessionLength() * 1.1),
                                    recorder.getPlayhead() + (juce::int64) (10.0 * sr));
    if (wanted > visibleSamples)
        visibleSamples = wanted;

    timeLabel.setText (formatPosition (recorder.getPlayhead()), juce::dontSendNotification);
    refreshControls();
    repaint();
}

void StudioView::paint (juce::Graphics& g)
{
    g.fillAll (palette::trench);

    const auto panelArea = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).toFloat();
    g.setColour (palette::panel.withAlpha (0.92f));
    g.fillRoundedRectangle (panelArea, 8.0f);
    g.setColour (palette::panelEdge);
    g.drawRoundedRectangle (panelArea, 8.0f, 1.0f);

    // Ruler: seconds along the top of the lanes.
    if (! rulerArea.isEmpty())
    {
        const double sr = recorder.getSampleRate();
        const double samplesPerPixel = (double) visibleSamples / juce::jmax (1, rulerArea.getWidth());
        const double totalSeconds = (double) visibleSamples / sr;
        const int step = totalSeconds > 240.0 ? 30 : (totalSeconds > 120.0 ? 10 : 5);

        g.setFont (juce::FontOptions (11.0f));
        for (int s = 0; s <= (int) totalSeconds; s += step)
        {
            const float x = (float) rulerArea.getX() + (float) (s * sr / samplesPerPixel);
            g.setColour (palette::panelEdge.brighter (0.4f));
            g.fillRect (x, (float) rulerArea.getBottom() - 6.0f, 1.0f, 6.0f);
            g.setColour (palette::dimText);
            g.drawText (juce::String::formatted ("%d:%02d", s / 60, s % 60), (int) x + 3, rulerArea.getY(), 50, rulerArea.getHeight() - 4,
                        juce::Justification::centredLeft);
        }
    }
}

void StudioView::resized()
{
    auto area = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).reduced (12, 10);

    auto transport = area.removeFromTop (40);
    for (auto* b : { &rewindButton, &playButton, &stopButton, &recordButton })
        b->setBounds (transport.removeFromLeft (b == &rewindButton ? 44 : 76).reduced (3, 2));
    transport.removeFromLeft (10);
    timeLabel.setBounds (transport.removeFromLeft (230).reduced (3, 2));
    transport.removeFromLeft (10);
    clickButton.setBounds (transport.removeFromLeft (70).reduced (3, 2));
    bpmLabel.setBounds (transport.removeFromLeft (36));
    bpmSlider.setBounds (transport.removeFromLeft (110).reduced (0, 4));

    // Session actions share the bottom bar with the status line.
    auto footer = area.removeFromBottom (34).withTrimmedTop (4);
    newSessionButton.setBounds (footer.removeFromLeft (120).reduced (3, 2));
    exportButton.setBounds (footer.removeFromLeft (112).reduced (3, 2));
    folderButton.setBounds (footer.removeFromLeft (118).reduced (3, 2));
    statusLabel.setBounds (footer.withTrimmedLeft (10));
    area.removeFromTop (8);

    constexpr int headerWidth = 210;
    rulerArea = area.removeFromTop (20).withTrimmedLeft (headerWidth);

    const int laneHeight = area.getHeight() / poman::MultitrackRecorder::numTracks;
    for (auto& tc : tracks)
    {
        auto row = area.removeFromTop (laneHeight).reduced (0, 4);
        auto header = row.removeFromLeft (headerWidth).reduced (0, 2);
        row.removeFromLeft (0);

        auto top = header.removeFromTop (header.getHeight() / 2);
        tc.name.setBounds (top.removeFromLeft (80));
        for (auto* b : { &tc.arm, &tc.mute, &tc.solo, &tc.clear })
            b->setBounds (top.removeFromLeft (30).reduced (2, 4));
        tc.volume.setBounds (header.reduced (4, 2).withTrimmedRight (10));
        tc.lane->setBounds (row);
    }
}
