#include "DrumView.h"
#include "Palette.h"
#include "RockStyle.h"
#include "Localisation.h"

namespace
{
using poman::DrumMachine;

const std::array<juce::Colour, DrumMachine::numInstruments> instrumentColours {
    palette::ember, palette::biolume, palette::coral, palette::sunlight,
    palette::sunlight.darker (0.3f), palette::aurora, palette::aurora.brighter (0.3f), palette::kelp
};

void styleHorizontal (juce::Slider& s, juce::Colour colour)
{
    s.setSliderStyle (juce::Slider::LinearHorizontal);
    s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    s.setColour (juce::Slider::trackColourId, colour.withAlpha (0.7f));
    s.setColour (juce::Slider::thumbColourId, colour);
    s.setColour (juce::Slider::backgroundColourId, palette::abyss);
}
} // namespace

//==============================================================================
DrumView::Grid::Grid (DrumView& o) : owner (o) {}

bool DrumView::Grid::cellAt (juce::Point<int> p, int& instrument, int& step) const
{
    if (! getLocalBounds().contains (p))
        return false;
    instrument = juce::jlimit (0, DrumMachine::numInstruments - 1, p.y * DrumMachine::numInstruments / juce::jmax (1, getHeight()));
    step = juce::jlimit (0, DrumMachine::numSteps - 1, p.x * DrumMachine::numSteps / juce::jmax (1, getWidth()));
    return true;
}

void DrumView::Grid::mouseDown (const juce::MouseEvent& e)
{
    int inst, step;
    if (! cellAt (e.getPosition(), inst, step))
        return;

    const int current = owner.drums.getStep (inst, step);
    // Left click toggles a hit; right click (or Ctrl) toggles an accent. Dragging paints the same value.
    if (e.mods.isPopupMenu() || e.mods.isCtrlDown())
        paintValue = current == 2 ? 1 : 2;
    else
        paintValue = current == 0 ? 1 : 0;

    owner.drums.setStep (inst, step, paintValue);
    owner.markEdited();
    repaint();
}

void DrumView::Grid::mouseDrag (const juce::MouseEvent& e)
{
    int inst, step;
    if (paintValue < 0 || ! cellAt (e.getPosition(), inst, step) || owner.drums.getStep (inst, step) == paintValue)
        return;
    owner.drums.setStep (inst, step, paintValue);
    owner.markEdited();
    repaint();
}

void DrumView::Grid::paint (juce::Graphics& g)
{
    const float cellW = (float) getWidth() / DrumMachine::numSteps;
    const float cellH = (float) getHeight() / DrumMachine::numInstruments;

    for (int step = 0; step < DrumMachine::numSteps; ++step)
    {
        // Shade every other beat so the four groups of 16ths read at a glance.
        const auto column = juce::Rectangle<float> (step * cellW, 0.0f, cellW, (float) getHeight());
        if ((step / 4) % 2 == 0)
        {
            g.setColour (palette::panelEdge.withAlpha (0.25f));
            g.fillRect (column);
        }

        for (int inst = 0; inst < DrumMachine::numInstruments; ++inst)
        {
            const auto cell = juce::Rectangle<float> (step * cellW, inst * cellH, cellW, cellH).reduced (3.0f);
            const int value = owner.drums.getStep (inst, step);
            const auto colour = instrumentColours[(size_t) inst];

            g.setColour (value == 0 ? palette::abyss : colour.withAlpha (value == 2 ? 1.0f : 0.55f));
            g.fillRoundedRectangle (cell, 4.0f);
            if (value == 2)
            {
                g.setColour (palette::text);
                g.fillRoundedRectangle (cell.withHeight (3.0f).reduced (4.0f, 0.0f), 1.5f);
            }
        }

        if (step == playingStep)
        {
            g.setColour (palette::text.withAlpha (0.85f));
            g.drawRoundedRectangle (column.reduced (1.0f), 4.0f, 2.0f);
        }
    }
}

//==============================================================================
DrumView::DrumView (DrumMachine& d, poman::MultitrackRecorder& r) : drums (d), recorder (r)
{
    setOpaque (true);

    for (auto* b : { &rewindButton, &playButton, &stopButton, &drumsOnButton, &clearButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, palette::biolume);
        addAndMakeVisible (*b);
    }
    drumsOnButton.setColour (juce::TextButton::buttonOnColourId, palette::ember);

    rewindButton.onClick = [this] { recorder.rewind(); };
    playButton.onClick = [this]
    {
        if (! drums.isEnabled())
            drums.setEnabled (true); // pressing play on the drum page means "I want drums"
        recorder.play();
        refreshControls();
    };
    stopButton.onClick = [this] { recorder.stop(); refreshControls(); };
    drumsOnButton.onClick = [this] { drums.setEnabled (! drums.isEnabled()); refreshControls(); };
    clearButton.onClick = [this]
    {
        for (int inst = 0; inst < DrumMachine::numInstruments; ++inst)
            for (int s = 0; s < DrumMachine::numSteps; ++s)
                drums.setStep (inst, s, 0);
        markEdited();
        grid.repaint();
    };

    const auto& presets = DrumMachine::patternPresets();
    for (int i = 0; i < (int) presets.size(); ++i)
        patternBox.addItem (presets[(size_t) i].name, i + 1);
    patternBox.setTextWhenNothingSelected ("Custom");
    patternBox.setColour (juce::ComboBox::outlineColourId, palette::ember.withAlpha (0.6f));
    patternBox.onChange = [this]
    {
        if (const int id = patternBox.getSelectedId(); id > 0)
        {
            drums.loadPreset (id - 1);
            swingSlider.setValue (drums.getSwing(), juce::dontSendNotification);
            grid.repaint();
        }
    };
    addAndMakeVisible (patternBox);

    kitBox.addItemList (DrumMachine::kitNames(), 1);
    kitBox.setColour (juce::ComboBox::outlineColourId, palette::ember.withAlpha (0.6f));
    kitBox.onChange = [this]
    {
        const int k = kitBox.getSelectedId() - 1;
        if (k == (int) DrumMachine::Kit::samples && ! drums.hasSampleKit())
        {
            chooseSampleKit(); // nothing to play yet: ask for a folder
            kitBox.setSelectedId (drums.getKit() + 1, juce::dontSendNotification);
            return;
        }
        drums.setKit (k);
        roomSlider.setEnabled (drums.getKit() != (int) DrumMachine::Kit::electronic);
        refreshKitTooltip();
    };
    addAndMakeVisible (kitBox);

    loadKitButton.setTooltip ("Load a folder of WAV samples as the drum kit (kick, snare, hats, toms, cymbals named in the file names)");
    loadKitButton.setColour (juce::TextButton::buttonOnColourId, palette::ember);
    loadKitButton.onClick = [this] { chooseSampleKit(); };
    addAndMakeVisible (loadKitButton);
    refreshKitTooltip();

    bpmSlider.setSliderStyle (juce::Slider::IncDecButtons);
    bpmSlider.setRange (40.0, 240.0, 1.0);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxLeft, false, 44, 24);
    bpmSlider.onValueChange = [this] { recorder.setBpm (bpmSlider.getValue()); recorder.saveSession(); };
    addAndMakeVisible (bpmSlider);

    styleHorizontal (swingSlider, palette::ember);
    swingSlider.setRange (0.0, 1.0, 0.01);
    swingSlider.onValueChange = [this] { drums.setSwing ((float) swingSlider.getValue()); };
    styleHorizontal (volumeSlider, palette::ember);
    volumeSlider.setRange (0.0, 1.25, 0.001);
    volumeSlider.onValueChange = [this] { drums.setMasterLevel ((float) volumeSlider.getValue()); };
    styleHorizontal (roomSlider, palette::ember);
    roomSlider.setRange (0.0, 1.0, 0.01);
    roomSlider.setTooltip ("Room ambience (Studio kit)");
    roomSlider.onValueChange = [this] { drums.setRoom ((float) roomSlider.getValue()); };
    addAndMakeVisible (swingSlider);
    addAndMakeVisible (volumeSlider);
    addAndMakeVisible (roomSlider);

    for (auto [label, text] : { std::pair { &bpmLabel, "BPM" }, std::pair { &swingLabel, "SWING" }, std::pair { &volumeLabel, "VOLUME" },
                                std::pair { &roomLabel, "ROOM" }, std::pair { &kitLabel, "KIT" } })
    {
        label->setText (text, juce::dontSendNotification);
        label->setFont (juce::FontOptions (12.0f, juce::Font::bold));
        addAndMakeVisible (*label);
    }

    positionLabel.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), 18.0f, juce::Font::bold));
    positionLabel.setColour (juce::Label::textColourId, palette::text);
    positionLabel.setColour (juce::Label::backgroundColourId, palette::abyss);
    positionLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (positionLabel);

    hintLabel.setText ("Click: hit on/off   Right-click: accent   Drag: paint.   Drums play with the studio transport "
                       "and are not recorded into your tracks; EXPORT MIX includes them.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hintLabel);

    const auto instrumentNames = DrumMachine::instrumentNames();
    for (int inst = 0; inst < DrumMachine::numInstruments; ++inst)
    {
        auto& n = names[(size_t) inst];
        n.setText (instrumentNames[inst], juce::dontSendNotification);
        n.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        n.setColour (juce::Label::textColourId, instrumentColours[(size_t) inst]);
        addAndMakeVisible (n);

        auto& l = levels[(size_t) inst];
        styleHorizontal (l, instrumentColours[(size_t) inst]);
        l.setRange (0.0, 1.25, 0.001);
        l.setValue (drums.getLevel (inst), juce::dontSendNotification);
        l.onValueChange = [this, inst] { drums.setLevel (inst, (float) levels[(size_t) inst].getValue()); };
        addAndMakeVisible (l);
    }

    addAndMakeVisible (grid);
    patternBox.setSelectedId (drums.matchingPreset() + 1, juce::dontSendNotification);
    refreshControls();
}

DrumView::~DrumView() { stopTimer(); }

void DrumView::visibilityChanged()
{
    if (isVisible())
    {
        // The pattern or mixer may have been restored from saved state while hidden.
        for (int inst = 0; inst < DrumMachine::numInstruments; ++inst)
            levels[(size_t) inst].setValue (drums.getLevel (inst), juce::dontSendNotification);
        patternBox.setSelectedId (drums.matchingPreset() + 1, juce::dontSendNotification);
        grid.repaint();
        startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

void DrumView::markEdited()
{
    patternBox.setSelectedId (0, juce::dontSendNotification); // now a custom pattern
}

void DrumView::refreshControls()
{
    using State = poman::MultitrackRecorder::State;
    const auto state = recorder.getState();
    playButton.setToggleState (state != State::stopped, juce::dontSendNotification);
    drumsOnButton.setToggleState (drums.isEnabled(), juce::dontSendNotification);

    if (! bpmSlider.isMouseButtonDown())
        bpmSlider.setValue (recorder.getBpm(), juce::dontSendNotification);
    if (! swingSlider.isMouseButtonDown())
        swingSlider.setValue (drums.getSwing(), juce::dontSendNotification);
    if (! volumeSlider.isMouseButtonDown())
        volumeSlider.setValue (drums.getMasterLevel(), juce::dontSendNotification);
    if (! roomSlider.isMouseButtonDown())
        roomSlider.setValue (drums.getRoom(), juce::dontSendNotification);
    if (! kitBox.isPopupActive() && kitBox.getSelectedId() != drums.getKit() + 1)
        kitBox.setSelectedId (drums.getKit() + 1, juce::dontSendNotification);
    roomSlider.setEnabled (drums.getKit() != (int) DrumMachine::Kit::electronic);
}

void DrumView::timerCallback()
{
    if (! kitBox.isPopupActive())
        refreshKitTooltip(); // a session restore may bring a sample kit
    const bool running = recorder.getState() != poman::MultitrackRecorder::State::stopped;
    const auto position = recorder.getPlayhead();
    const int step = running && drums.isEnabled() ? drums.stepAt (position, recorder.getBpm()) : -1;
    if (step != grid.playingStep)
    {
        grid.playingStep = step;
        grid.repaint();
    }

    const double beats = (double) position / recorder.getSampleRate() * recorder.getBpm() / 60.0;
    positionLabel.setText (recorder.isCountingIn() ? juce::String ("COUNT-IN")
                                                   : juce::String::formatted ("BAR %3d  BEAT %d", (int) std::floor (beats / 4.0) + 1,
                                                                              (int) std::fmod (std::floor (beats), 4.0) + 1),
                           juce::dontSendNotification);
    refreshControls();
}

void DrumView::paint (juce::Graphics& g)
{
    rockstyle::paintBackground (g, getLocalBounds().toFloat());
    const auto panelArea = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).toFloat();
    rockstyle::paintPanel (g, panelArea, palette::ember);

    g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    const float cellW = (float) stepNumberArea.getWidth() / DrumMachine::numSteps;
    for (int s = 0; s < DrumMachine::numSteps; ++s)
    {
        g.setColour (s % 4 == 0 ? palette::text : palette::dimText);
        g.drawText (juce::String (s + 1), juce::Rectangle<float> (stepNumberArea.getX() + s * cellW, (float) stepNumberArea.getY(),
                                                                  cellW, (float) stepNumberArea.getHeight()),
                    juce::Justification::centred);
    }
}

void DrumView::chooseSampleKit()
{
    kitChooser = std::make_unique<juce::FileChooser> (poman::i18n::tr ("Choose a folder with drum samples (WAV)"),
                                                      juce::File::getSpecialLocation (juce::File::userMusicDirectory));
    kitChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                             [this] (const juce::FileChooser& chooser)
                             {
                                 const auto folder = chooser.getResult();
                                 if (! folder.isDirectory())
                                     return;
                                 const auto result = drums.loadSampleKit (folder);
                                 if (result.failed())
                                     juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                             poman::i18n::tr ("Drum kit"), result.getErrorMessage());
                                 kitBox.setSelectedId (drums.getKit() + 1, juce::dontSendNotification);
                                 roomSlider.setEnabled (drums.getKit() != (int) DrumMachine::Kit::electronic);
                                 refreshKitTooltip();
                             });
}

void DrumView::refreshKitTooltip()
{
    kitBox.setTooltip (drums.hasSampleKit() ? "Samples: " + drums.getSampleKitName() + "\n" + drums.describeSampleKit().joinIntoString ("\n")
                                            : juce::String());
}

void DrumView::resized()
{
    auto area = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).reduced (12, 10);

    auto bar1 = area.removeFromTop (40);
    rewindButton.setBounds (bar1.removeFromLeft (44).reduced (3, 2));
    playButton.setBounds (bar1.removeFromLeft (76).reduced (3, 2));
    stopButton.setBounds (bar1.removeFromLeft (76).reduced (3, 2));
    bar1.removeFromLeft (10);
    positionLabel.setBounds (bar1.removeFromLeft (180).reduced (3, 2));
    bar1.removeFromLeft (10);
    bpmLabel.setBounds (bar1.removeFromLeft (36));
    bpmSlider.setBounds (bar1.removeFromLeft (110).reduced (0, 4));
    bar1.removeFromLeft (20);
    kitLabel.setBounds (bar1.removeFromLeft (32));
    kitBox.setBounds (bar1.removeFromLeft (130).reduced (0, 5));
    loadKitButton.setBounds (bar1.removeFromLeft (110).reduced (4, 2));
    drumsOnButton.setBounds (bar1.removeFromRight (110).reduced (3, 2));

    area.removeFromTop (6);
    auto bar2 = area.removeFromTop (36);
    patternBox.setBounds (bar2.removeFromLeft (200).reduced (3, 3));
    clearButton.setBounds (bar2.removeFromLeft (80).reduced (3, 2));
    bar2.removeFromLeft (20);
    swingLabel.setBounds (bar2.removeFromLeft (52));
    swingSlider.setBounds (bar2.removeFromLeft (130));
    bar2.removeFromLeft (20);
    volumeLabel.setBounds (bar2.removeFromLeft (60));
    volumeSlider.setBounds (bar2.removeFromLeft (130));
    bar2.removeFromLeft (20);
    roomLabel.setBounds (bar2.removeFromLeft (48));
    roomSlider.setBounds (bar2.removeFromLeft (130));

    hintLabel.setBounds (area.removeFromBottom (24));
    area.removeFromTop (10);

    constexpr int headerWidth = 210;
    stepNumberArea = area.removeFromTop (22).withTrimmedLeft (headerWidth);
    const int rowHeight = area.getHeight() / DrumMachine::numInstruments;
    auto headers = area.removeFromLeft (headerWidth);
    for (int inst = 0; inst < DrumMachine::numInstruments; ++inst)
    {
        auto row = headers.removeFromTop (rowHeight).reduced (0, 4);
        names[(size_t) inst].setBounds (row.removeFromLeft (80));
        levels[(size_t) inst].setBounds (row.withTrimmedRight (14));
    }
    grid.setBounds (area.withHeight (rowHeight * DrumMachine::numInstruments));
}
