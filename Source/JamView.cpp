#include "JamView.h"
#include "Localisation.h"
#include "Palette.h"
#include "RockStyle.h"

using poman::i18n::tr;

JamView::JamView (poman::BackingTrack& t) : track (t)
{
    setOpaque (true);
    addAndMakeVisible (waveform);

    for (auto* b : { &loadButton, &rewindButton, &playButton, &setAButton, &setBButton, &loopButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, palette::coral);
        addAndMakeVisible (*b);
    }
    playButton.getProperties().set ("fontSize", 20);
    loopButton.setClickingTogglesState (true);

    loadButton.onClick = [this] { chooseSong(); };
    rewindButton.onClick = [this] { track.seek (track.isLooping() ? track.getLoopStart() : 0.0); };
    playButton.onClick = [this] { track.setPlaying (! track.isPlaying()); };
    setAButton.setTooltip ("Set the loop start here");
    setBButton.setTooltip ("Set the loop end here and loop");
    loopButton.setTooltip ("Repeat the A-B section");
    setAButton.onClick = [this]
    {
        const double a = track.getPositionSeconds();
        track.setLoop (a, juce::jmax (a + 1.0, track.getLoopEnd()), track.isLooping());
    };
    setBButton.onClick = [this]
    {
        const double b = track.getPositionSeconds();
        track.setLoop (juce::jmin (track.getLoopStart(), b - 0.5), b, true);
        track.seek (track.getLoopStart());
    };
    loopButton.onClick = [this] { track.setLoop (track.getLoopStart(), track.getLoopEnd(), loopButton.getToggleState()); };

    const auto setupSlider = [this] (juce::Slider& s, double lo, double hi, double step, double value)
    {
        s.setSliderStyle (juce::Slider::LinearHorizontal);
        s.setTextBoxStyle (juce::Slider::TextBoxRight, false, 70, 22);
        s.setRange (lo, hi, step);
        s.setValue (value, juce::dontSendNotification);
        s.setColour (juce::Slider::trackColourId, palette::coral.withAlpha (0.7f));
        s.setColour (juce::Slider::thumbColourId, palette::coral);
        s.setColour (juce::Slider::backgroundColourId, palette::abyss);
        addAndMakeVisible (s);
    };
    setupSlider (speedSlider, 50.0, 150.0, 1.0, track.getSpeed() * 100.0);
    speedSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v)) + " %"; };
    speedSlider.setDoubleClickReturnValue (true, 100.0);
    speedSlider.onValueChange = [this] { track.setSpeed (speedSlider.getValue() / 100.0); };
    setupSlider (pitchSlider, -12.0, 12.0, 1.0, track.getSemitones());
    pitchSlider.textFromValueFunction = [] (double v) { return (v > 0 ? "+" : "") + juce::String (juce::roundToInt (v)) + " st"; };
    pitchSlider.setDoubleClickReturnValue (true, 0.0);
    pitchSlider.onValueChange = [this] { track.setSemitones (juce::roundToInt (pitchSlider.getValue())); };
    setupSlider (volumeSlider, 0.0, 1.5, 0.01, track.getVolume());
    volumeSlider.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    volumeSlider.onValueChange = [this] { track.setVolume ((float) volumeSlider.getValue()); };
    for (auto* s : { &speedSlider, &pitchSlider, &volumeSlider })
        s->updateText();

    for (auto [label, text] : { std::pair { &speedLabel, "SPEED" }, std::pair { &pitchLabel, "PITCH" }, std::pair { &volumeLabel, "VOLUME" } })
    {
        label->setText (text, juce::dontSendNotification);
        label->setFont (juce::FontOptions (13.0f, juce::Font::bold));
        addAndMakeVisible (*label);
    }
    songLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
    timeLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    timeLabel.setJustificationType (juce::Justification::centredRight);
    hintLabel.setText ("Load a song (WAV, MP3, FLAC, AIFF, OGG). SPEED slows it down without changing the pitch; PITCH changes "
                       "the key without changing the speed. Click the waveform to jump; A and B mark a section to loop. "
                       "The song is heard but never recorded into your takes.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setColour (juce::Label::textColourId, palette::dimText);
    for (auto* l : { &songLabel, &timeLabel, &hintLabel })
        addAndMakeVisible (*l);

    refreshSong();
}

JamView::~JamView() { stopTimer(); }

void JamView::visibilityChanged()
{
    if (isVisible())
        startTimerHz (30);
    else
        stopTimer();
}

void JamView::chooseSong()
{
    chooser = std::make_unique<juce::FileChooser> (tr ("Choose a song"), juce::File::getSpecialLocation (juce::File::userMusicDirectory),
                                                   "*.wav;*.mp3;*.flac;*.aif;*.aiff;*.ogg");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& c)
                          {
                              const auto file = c.getResult();
                              if (! file.existsAsFile())
                                  return;
                              const auto result = track.load (file);
                              if (result.failed())
                                  juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Backing track"), result.getErrorMessage());
                              refreshSong();
                          });
}

void JamView::refreshSong()
{
    shownSong = track.getFileName();
    songLabel.setText (shownSong.isEmpty() ? tr ("No song loaded") : shownSong, juce::dontSendNotification);
    waveform.peaks = track.getOverview();
    speedSlider.setValue (track.getSpeed() * 100.0, juce::dontSendNotification);
    pitchSlider.setValue (track.getSemitones(), juce::dontSendNotification);
    volumeSlider.setValue (track.getVolume(), juce::dontSendNotification);
    waveform.repaint();
}

juce::String JamView::timeText (double seconds)
{
    const int s = (int) seconds;
    return juce::String::formatted ("%d:%02d", s / 60, s % 60);
}

void JamView::timerCallback()
{
    if (track.getFileName() != shownSong) // e.g. a session restore
        refreshSong();
    const bool loaded = track.isLoaded();
    for (auto* c : std::initializer_list<juce::Component*> { &rewindButton, &playButton, &setAButton, &setBButton, &loopButton })
        c->setEnabled (loaded);
    playButton.setButtonText (track.isPlaying() ? "PAUSE" : "PLAY");
    playButton.setToggleState (track.isPlaying(), juce::dontSendNotification);
    loopButton.setToggleState (track.isLooping(), juce::dontSendNotification);
    timeLabel.setText (loaded ? timeText (track.getPositionSeconds()) + " / " + timeText (track.getLengthSeconds()) : juce::String(),
                       juce::dontSendNotification);
    poman::i18n::retranslate (*this);
    waveform.repaint();
}

void JamView::paint (juce::Graphics& g)
{
    rockstyle::paintBackground (g, getLocalBounds().toFloat());
    const auto panel = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).toFloat();
    rockstyle::paintPanel (g, panel, palette::coral);
}

void JamView::Waveform::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds().toFloat();
    g.setColour (palette::abyss);
    g.fillRoundedRectangle (area, 6.0f);
    auto& t = owner.track;
    const double length = t.getLengthSeconds();
    if (peaks.empty() || length <= 0.0)
    {
        g.setColour (palette::dimText);
        g.drawText (tr ("No song loaded"), area, juce::Justification::centred);
        return;
    }
    const auto xOf = [&] (double seconds) { return area.getX() + (float) (seconds / length) * area.getWidth(); };

    if (t.isLooping())
    {
        g.setColour (palette::coral.withAlpha (0.18f));
        g.fillRect (juce::Rectangle<float>::leftTopRightBottom (xOf (t.getLoopStart()), area.getY(), xOf (t.getLoopEnd()), area.getBottom()));
    }

    g.setColour (palette::coral.withAlpha (0.75f));
    const float mid = area.getCentreY(), half = area.getHeight() * 0.45f;
    for (int x = 0; x < (int) area.getWidth(); ++x)
    {
        const auto index = (size_t) juce::jlimit (0, (int) peaks.size() - 1, (int) ((float) x / area.getWidth() * (float) peaks.size()));
        const float h = peaks[index] * half;
        g.drawVerticalLine ((int) area.getX() + x, mid - h, mid + h);
    }

    for (auto [seconds, label] : { std::pair { t.getLoopStart(), "A" }, std::pair { t.getLoopEnd(), "B" } })
        if (t.isLooping())
        {
            g.setColour (palette::text.withAlpha (0.6f));
            g.drawVerticalLine ((int) xOf (seconds), area.getY(), area.getBottom());
            g.drawText (label, juce::Rectangle<float> (xOf (seconds) + 3.0f, area.getY() + 2.0f, 16.0f, 14.0f), juce::Justification::centredLeft);
        }

    g.setColour (palette::text);
    g.fillRect (juce::Rectangle<float> (xOf (t.getPositionSeconds()) - 1.0f, area.getY(), 2.0f, area.getHeight()));
}

void JamView::Waveform::mouseDown (const juce::MouseEvent& e)
{
    auto& t = owner.track;
    if (t.isLoaded())
        t.seek ((double) e.position.x / (double) getWidth() * t.getLengthSeconds());
}

void JamView::resized()
{
    auto area = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).reduced (24, 18);
    hintLabel.setBounds (area.removeFromBottom (40));
    area.removeFromBottom (8);

    auto top = area.removeFromTop (40);
    loadButton.setBounds (top.removeFromLeft (130).reduced (2, 3));
    top.removeFromLeft (12);
    timeLabel.setBounds (top.removeFromRight (150));
    songLabel.setBounds (top);
    area.removeFromTop (10);

    auto controls = area.removeFromBottom (170);
    auto transport = controls.removeFromTop (60);
    rewindButton.setBounds (transport.removeFromLeft (60).reduced (3));
    playButton.setBounds (transport.removeFromLeft (150).reduced (3));
    transport.removeFromLeft (24);
    setAButton.setBounds (transport.removeFromLeft (60).reduced (3));
    setBButton.setBounds (transport.removeFromLeft (60).reduced (3));
    loopButton.setBounds (transport.removeFromLeft (90).reduced (3));
    controls.removeFromTop (10);
    for (auto [label, slider] : { std::pair { &speedLabel, &speedSlider }, std::pair { &pitchLabel, &pitchSlider }, std::pair { &volumeLabel, &volumeSlider } })
    {
        auto row = controls.removeFromTop (32).withWidth (juce::jmin (controls.getWidth(), 620));
        label->setBounds (row.removeFromLeft (90));
        slider->setBounds (row);
    }

    area.removeFromBottom (12);
    waveform.setBounds (area);
}
