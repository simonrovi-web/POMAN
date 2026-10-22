#include "LiveView.h"
#include "Localisation.h"
#include "Palette.h"
#include "RockStyle.h"
#include "PluginProcessor.h"
#include "BinaryData.h"

namespace
{
using poman::i18n::tr;

struct StompDefinition { const char* paramId; const char* name; juce::Colour colour; };

const StompDefinition stompDefinitions[] {
    { "odOn", "DRIVE", palette::ember },
    { "wahOn", "WAH", palette::sunlight },
    { "modOn", "MOD", palette::coral },
    { "dlyOn", "DELAY", palette::pressure },
    { "revOn", "REVERB", palette::pressure },
};

juce::String looperText (poman::Looper::State s)
{
    using S = poman::Looper::State;
    switch (s)
    {
        case S::empty:       return {};
        case S::recording:   return tr ("RECORDING");
        case S::playing:     return tr ("PLAYING");
        case S::overdubbing: return tr ("OVERDUB");
        case S::stopped:     return tr ("STOPPED");
        case S::waiting:     return tr ("WAITING");
    }
    return {};
}
} // namespace

LiveView::LiveView (PomanProcessor& p, MidiMenu menu, TunerSource source)
    : processor (p), showMidiMenu (std::move (menu)), tunerReading (std::move (source))
{
    setOpaque (true);
    auto& presets = processor.getPresetManager();

    for (auto [button, action, step] : { std::tuple { &previousButton, "action:presetPrev", -1 }, std::tuple { &nextButton, "action:presetNext", 1 } })
    {
        button->getProperties().set ("fontSize", 44);
        button->onClick = [&presets, step = step] { step < 0 ? presets.loadPrevious() : presets.loadNext(); };
        button->onRightClick = [this, target = juce::String (action), b = button] { showMidiMenu (target, *b); };
        addAndMakeVisible (*button);
    }

    for (const auto& def : stompDefinitions)
    {
        Stomp stomp;
        stomp.button = std::make_unique<MidiTextButton> (def.name);
        stomp.button->setClickingTogglesState (true);
        stomp.button->setColour (juce::TextButton::buttonOnColourId, def.colour);
        stomp.button->getProperties().set ("fontSize", 22);
        stomp.button->onRightClick = [this, id = juce::String (def.paramId), b = stomp.button.get()] { showMidiMenu (id, *b); };
        stomp.attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, def.paramId, *stomp.button);
        addAndMakeVisible (*stomp.button);
        stomps.push_back (std::move (stomp));
    }

    tapButton.getProperties().set ("fontSize", 22);
    tapButton.onClick = [this]
    {
        if (const double bpm = tapTempo.tap (juce::Time::getMillisecondCounterHiRes() * 0.001); bpm > 0.0)
            processor.getRecorder().setBpm (bpm);
    };
    addAndMakeVisible (tapButton);

    tunerButton.setClickingTogglesState (true);
    tunerButton.setColour (juce::TextButton::buttonOnColourId, palette::kelp);
    tunerButton.getProperties().set ("fontSize", 22);
    tunerButton.onClick = [this] { processor.setTunerActive (tunerButton.getToggleState()); };
    tunerButton.onRightClick = [this] { showMidiMenu ("action:tuner", tunerButton); };
    addAndMakeVisible (tunerButton);

    for (int i = 0; i < gridSize; ++i)
    {
        auto& b = gridButtons[(size_t) i];
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonOnColourId, palette::biolume.withAlpha (0.8f));
        b.getProperties().set ("fontSize", 17);
        b.onClick = [this, i, &presets] { presets.loadPreset (page * gridSize + i); };
        addAndMakeVisible (b);
    }
    pagePrevButton.onClick = [this] { page = juce::jmax (0, page - 1); refreshGrid(); };
    pageNextButton.onClick = [this, &presets]
    {
        page = juce::jmin ((presets.getPresetNames().size() - 1) / gridSize, page + 1);
        refreshGrid();
    };
    addAndMakeVisible (pagePrevButton);
    addAndMakeVisible (pageNextButton);

    hintLabel.setText ("Click a preset to load it. Right-click a switch, < or > to assign a MIDI footswitch.", juce::dontSendNotification);
    hintLabel.setJustificationType (juce::Justification::centred);
    hintLabel.setColour (juce::Label::textColourId, palette::dimText);
    hintLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (hintLabel);
}

LiveView::~LiveView() { stopTimer(); }

void LiveView::visibilityChanged()
{
    if (isVisible())
    {
        // Open on the page that holds the current preset.
        page = juce::jmax (0, processor.getPresetManager().getCurrentIndex()) / gridSize;
        shownIndex = -2;
        refreshGrid();
        startTimerHz (30);
    }
    else
    {
        stopTimer();
    }
}

void LiveView::refreshGrid()
{
    auto& presets = processor.getPresetManager();
    const auto names = presets.getPresetNames();
    const int current = presets.getCurrentIndex();
    for (int i = 0; i < gridSize; ++i)
    {
        const int index = page * gridSize + i;
        auto& b = gridButtons[(size_t) i];
        b.setVisible (index < names.size());
        b.setButtonText (index < names.size() ? juce::String (index + 1) + "  " + names[index] : juce::String());
        b.setToggleState (index == current, juce::dontSendNotification);
    }
    pagePrevButton.setEnabled (page > 0);
    pageNextButton.setEnabled ((page + 1) * gridSize < names.size());
    shownCount = names.size();
    repaint();
}

void LiveView::timerCallback()
{
    auto& presets = processor.getPresetManager();
    const int current = presets.getCurrentIndex();
    const auto name = presets.getCurrentName();
    if (current != shownIndex || name != shownName || presets.getPresetNames().size() != shownCount)
    {
        // Follow preset changes from MIDI or other pages, flipping to the right page.
        if (current >= 0 && current / gridSize != page && shownIndex != -2)
            page = current / gridSize;
        shownIndex = current;
        shownName = name;
        refreshGrid();
    }

    tunerOn = processor.isTunerActive();
    tunerButton.setToggleState (tunerOn, juce::dontSendNotification);
    if (tunerOn)
        tuner = tunerReading();
    poman::i18n::retranslate (*this);
    repaint (nameArea.expanded (4));
}

void LiveView::paint (juce::Graphics& g)
{
    rockstyle::paintBackground (g, getLocalBounds().toFloat());
    const auto panel = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).toFloat();
    rockstyle::paintPanel (g, panel, palette::biolume);

    // The POMAN badge, faint, behind the preset name.
    if (! badge.isValid())
        badge = juce::ImageFileFormat::loadFrom (BinaryData::logo_badge_png, BinaryData::logo_badge_pngSize);
    if (badge.isValid() && ! nameArea.isEmpty())
    {
        g.setOpacity (0.13f);
        g.drawImage (badge, nameArea.toFloat().withSizeKeepingCentre ((float) nameArea.getHeight(), (float) nameArea.getHeight()),
                     juce::RectanglePlacement::centred);
        g.setOpacity (1.0f);
    }

    auto area = nameArea.toFloat();
    if (tunerOn)
    {
        // Big tuner: note name and a cents bar.
        const bool hasPitch = tuner.frequency > 0.0f;
        const bool inTune = hasPitch && std::abs (tuner.cents) < 3.0f;
        g.setColour (inTune ? palette::kelp : palette::text);
        g.setFont (juce::FontOptions (area.getHeight() * 0.55f, juce::Font::bold));
        g.drawText (hasPitch ? tuner.noteName() : juce::String ("--"), area.removeFromTop (area.getHeight() * 0.68f), juce::Justification::centred);
        auto bar = area.withSizeKeepingCentre (area.getWidth() * 0.7f, 10.0f);
        g.setColour (palette::abyss);
        g.fillRoundedRectangle (bar, 5.0f);
        if (hasPitch)
        {
            const float x = bar.getCentreX() + juce::jlimit (-50.0f, 50.0f, tuner.cents) / 50.0f * bar.getWidth() * 0.5f;
            g.setColour (inTune ? palette::kelp : palette::ember);
            g.fillEllipse (juce::Rectangle<float> (22.0f, 22.0f).withCentre ({ x, bar.getCentreY() }));
        }
        g.setColour (palette::dimText);
        g.fillRect (juce::Rectangle<float> (2.0f, 22.0f).withCentre (bar.getCentre()));
        return;
    }

    auto& presets = processor.getPresetManager();
    const int current = presets.getCurrentIndex();
    const auto count = presets.getPresetNames().size();
    g.setColour (palette::dimText);
    g.setFont (juce::FontOptions (16.0f, juce::Font::bold));
    const auto info = current < 0 ? juce::String()
                                  : juce::String (current + 1) + " / " + juce::String (count) + "   "
                                        + tr (current < presets.getNumFactoryPresets() ? "FACTORY" : "USER");
    g.drawText (info, area.removeFromTop (24.0f), juce::Justification::centred);

    const auto looper = looperText (processor.getLooper().getState());
    g.drawText (looper.isEmpty() ? juce::String() : tr ("LOOPER") + ": " + looper, area.removeFromBottom (22.0f), juce::Justification::centred);

    g.setColour (palette::text);
    g.setFont (juce::FontOptions (juce::jmin (area.getHeight() * 0.62f, 96.0f), juce::Font::bold));
    g.drawFittedText (shownName, area.toNearestInt(), juce::Justification::centred, 1, 0.5f);
}

void LiveView::resized()
{
    auto area = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).reduced (24, 18);
    hintLabel.setBounds (area.removeFromBottom (22));
    area.removeFromBottom (8);

    // Preset grid with page arrows.
    auto grid = area.removeFromBottom (juce::jmin (150, area.getHeight() / 4));
    pagePrevButton.setBounds (grid.removeFromLeft (44).reduced (2, 20));
    pageNextButton.setBounds (grid.removeFromRight (44).reduced (2, 20));
    const int columns = gridSize / 2, cellW = grid.getWidth() / columns, cellH = grid.getHeight() / 2;
    for (int i = 0; i < gridSize; ++i)
        gridButtons[(size_t) i].setBounds (juce::Rectangle<int> (grid.getX() + (i % columns) * cellW, grid.getY() + (i / columns) * cellH, cellW, cellH).reduced (4));
    area.removeFromBottom (14);

    // Stomp switches: effects, then TAP and TUNER.
    auto switches = area.removeFromBottom (juce::jmin (110, area.getHeight() / 3));
    const int numSwitches = (int) stomps.size() + 2, w = switches.getWidth() / numSwitches;
    for (auto& s : stomps)
        s.button->setBounds (switches.removeFromLeft (w).reduced (6));
    tapButton.setBounds (switches.removeFromLeft (w).reduced (6));
    tunerButton.setBounds (switches.reduced (6));
    area.removeFromBottom (14);

    // Preset name between the big previous / next switches.
    const int arrowW = juce::jmin (150, area.getWidth() / 6);
    previousButton.setBounds (area.removeFromLeft (arrowW).reduced (4, 20));
    nextButton.setBounds (area.removeFromRight (arrowW).reduced (4, 20));
    nameArea = area.reduced (12, 0);
}
