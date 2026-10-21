#include "LoopView.h"
#include "Localisation.h"
#include "Palette.h"

namespace
{
using State = poman::Looper::State;
using Command = poman::Looper::Command;

const juce::Colour recordRed { 0xffff3b30 };

juce::Colour stateColour (State s)
{
    switch (s)
    {
        case State::recording:   return recordRed;
        case State::overdubbing: return palette::sunlight;
        case State::playing:     return palette::kelp;
        case State::waiting:     return palette::pressure;
        default:                 return palette::dimText;
    }
}

juce::String stateText (State s)
{
    switch (s)
    {
        case State::empty:       return "EMPTY";
        case State::recording:   return "RECORDING";
        case State::playing:     return "PLAYING";
        case State::overdubbing: return "OVERDUB";
        case State::stopped:     return "STOPPED";
        case State::waiting:     return "WAITING";
    }
    return {};
}

// What the main switch does next, like the label on a looper pedal.
juce::String nextAction (State s)
{
    switch (s)
    {
        case State::empty:       return "REC";
        case State::recording:   return "PLAY";
        case State::playing:     return "DUB";
        case State::overdubbing: return "PLAY";
        case State::stopped:     return "PLAY";
        case State::waiting:     return "CANCEL";
    }
    return {};
}
} // namespace

LoopView::LoopView (poman::Looper& l, MidiMenu menu, CcLookup lookup)
    : looper (l), showMidiMenu (std::move (menu)), ccFor (std::move (lookup))
{
    setOpaque (true);

    const std::array<std::pair<MidiTextButton*, std::pair<Command, const char*>>, 4> buttons { {
        { &mainButton, { Command::main, "action:looperMain" } },
        { &stopButton, { Command::stop, "action:looperStop" } },
        { &undoButton, { Command::undo, "action:looperUndo" } },
        { &clearButton, { Command::clear, "action:looperClear" } },
    } };
    for (const auto& [button, binding] : buttons)
    {
        const auto [command, target] = binding;
        button->setColour (juce::TextButton::buttonOnColourId, palette::kelp);
        button->onClick = [this, command = command] { looper.post (command); };
        button->onRightClick = [this, target = juce::String (target), button = button] { showMidiMenu (target, *button); };
        addAndMakeVisible (*button);
    }

    mainButton.getProperties().set ("fontSize", 34);
    for (auto* b : { &stopButton, &undoButton, &clearButton })
        b->getProperties().set ("fontSize", 17);

    levelSlider.setSliderStyle (juce::Slider::LinearHorizontal);
    levelSlider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    levelSlider.setRange (0.0, 1.25, 0.001);
    levelSlider.setValue (looper.getLevel(), juce::dontSendNotification);
    levelSlider.setColour (juce::Slider::trackColourId, palette::kelp.withAlpha (0.7f));
    levelSlider.setColour (juce::Slider::thumbColourId, palette::kelp);
    levelSlider.setColour (juce::Slider::backgroundColourId, palette::abyss);
    levelSlider.onValueChange = [this] { looper.setLevel ((float) levelSlider.getValue()); };
    addAndMakeVisible (levelSlider);

    syncButton.setClickingTogglesState (true);
    syncButton.setToggleState (looper.isSynced(), juce::dontSendNotification);
    syncButton.setColour (juce::TextButton::buttonOnColourId, palette::pressure);
    syncButton.setTooltip ("Tempo sync: loops are whole 4/4 bars at the studio/drums tempo; while the drums run, "
                           "recording starts on the next beat and the loop stays locked to them");
    syncButton.onClick = [this] { looper.setSync (syncButton.getToggleState()); };
    addAndMakeVisible (syncButton);

    levelLabel.setText ("LOOP LEVEL", juce::dontSendNotification);
    levelLabel.setFont (juce::FontOptions (12.0f, juce::Font::bold));
    addAndMakeVisible (levelLabel);

    hintLabel.setText ("REC starts the loop, PLAY closes it, DUB layers on top. UNDO removes the last layer (press again to bring it back). "
                       "Up to 60 s. SYNC locks loops to the drums' tempo in whole bars. Loops play into STUDIO recordings. "
                       "Right-click a button to assign a MIDI footswitch.",
                       juce::dontSendNotification);
    hintLabel.setFont (juce::FontOptions (12.0f));
    hintLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (hintLabel);

    midiLabel.setFont (juce::FontOptions (12.0f));
    midiLabel.setJustificationType (juce::Justification::centred);
    midiLabel.setColour (juce::Label::textColourId, palette::dimText);
    addAndMakeVisible (midiLabel);
}

LoopView::~LoopView() { stopTimer(); }

void LoopView::visibilityChanged()
{
    if (isVisible())
        startTimerHz (30);
    else
        stopTimer();
}

void LoopView::timerCallback()
{
    const auto s = looper.getState();
    mainButton.setButtonText (nextAction (s));
    // Lit in the state's colour while the loop is running, like a pedal's LED.
    mainButton.setColour (juce::TextButton::buttonOnColourId, stateColour (s));
    mainButton.setToggleState (s == State::recording || s == State::playing || s == State::overdubbing, juce::dontSendNotification);
    undoButton.setButtonText (looper.canRedo() ? "REDO" : "UNDO");
    syncButton.setToggleState (looper.isSynced(), juce::dontSendNotification); // sessions can change it
    undoButton.setEnabled (looper.canUndo() || looper.canRedo());

    juce::String midi;
    for (auto [name, target] : { std::pair { "REC/PLAY/DUB", "action:looperMain" }, std::pair { "STOP", "action:looperStop" },
                                 std::pair { "UNDO", "action:looperUndo" }, std::pair { "CLEAR", "action:looperClear" } })
        if (const int cc = ccFor (target); cc >= 0)
            midi << name << " = CC " << cc << "     ";
    midiLabel.setText (midi.isEmpty() ? juce::String ("No MIDI footswitch assigned") : midi.trim(), juce::dontSendNotification);
    poman::i18n::retranslate (*this);
    repaint (ringArea.toNearestInt().expanded (4));
}

void LoopView::paint (juce::Graphics& g)
{
    g.fillAll (palette::trench);
    const auto panel = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).toFloat();
    g.setColour (palette::panel.withAlpha (0.92f));
    g.fillRoundedRectangle (panel, 8.0f);
    g.setColour (palette::panelEdge);
    g.drawRoundedRectangle (panel, 8.0f, 1.0f);
    g.setColour (palette::kelp);
    g.fillRoundedRectangle (panel.withHeight (3.0f).reduced (12.0f, 0.0f), 1.5f);

    // Progress ring: position within the loop (or fill-up towards the 60 s limit while recording).
    const auto s = looper.getState();
    const auto colour = stateColour (s);
    const float radius = juce::jmin (ringArea.getWidth(), ringArea.getHeight()) * 0.5f - 10.0f;
    const auto centre = ringArea.getCentre();
    const double length = looper.getLengthSeconds();
    const double progress = s == State::recording ? looper.getPositionSeconds() / looper.getMaxSeconds()
                          : (length > 0.0 ? looper.getPositionSeconds() / length : 0.0);

    juce::Path track, arc;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, juce::MathConstants<float>::twoPi, true);
    g.setColour (palette::abyss);
    g.strokePath (track, juce::PathStrokeType (14.0f));
    arc.addCentredArc (centre.x, centre.y, radius, radius, 0.0f, 0.0f, (float) (juce::MathConstants<double>::twoPi * progress), true);
    g.setColour (colour);
    g.strokePath (arc, juce::PathStrokeType (14.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    g.setColour (colour);
    g.setFont (juce::FontOptions (radius * 0.22f, juce::Font::bold));
    g.drawText (poman::i18n::tr (stateText (s)), juce::Rectangle<float> (radius * 2.0f, radius * 0.3f).withCentre (centre.translated (0.0f, -radius * 0.18f)),
                juce::Justification::centred);

    juce::String time = s == State::recording ? juce::String (looper.getPositionSeconds(), 1) + " s"
                      : (length > 0.0 ? juce::String (looper.getPositionSeconds(), 1) + " / " + juce::String (length, 1) + " s" : juce::String ("--"));
    if (const int bars = looper.getBars(); bars > 0 && s != State::recording)
        time << "  (" << bars << poman::i18n::tr (bars == 1 ? " bar)" : " bars)");
    g.setColour (palette::text);
    g.setFont (juce::FontOptions (juce::Font::getDefaultMonospacedFontName(), radius * 0.16f, juce::Font::bold));
    g.drawText (time, juce::Rectangle<float> (radius * 2.0f, radius * 0.25f).withCentre (centre.translated (0.0f, radius * 0.2f)),
                juce::Justification::centred);
}

void LoopView::resized()
{
    auto area = getLocalBounds().reduced (16, 0).withTrimmedBottom (14).reduced (24, 20);
    midiLabel.setBounds (area.removeFromBottom (22));
    hintLabel.setBounds (area.removeFromBottom (40));
    area.removeFromBottom (10);

    auto controls = area.removeFromBottom (190);
    auto levelRow = controls.removeFromBottom (36).withSizeKeepingCentre (420, 36);
    syncButton.setBounds (levelRow.removeFromRight (70).reduced (6, 2));
    levelLabel.setBounds (levelRow.removeFromLeft (90));
    levelSlider.setBounds (levelRow);
    controls.removeFromBottom (12);

    auto row = controls.withSizeKeepingCentre (juce::jmin (controls.getWidth(), 760), controls.getHeight());
    mainButton.setBounds (row.removeFromLeft (row.getWidth() * 2 / 5).reduced (8));
    const int w = row.getWidth() / 3;
    for (auto* b : { &stopButton, &undoButton, &clearButton })
        b->setBounds (row.removeFromLeft (w).reduced (8, 30));

    ringArea = area.toFloat().withSizeKeepingCentre (juce::jmin ((float) area.getWidth(), (float) area.getHeight()),
                                                     juce::jmin ((float) area.getWidth(), (float) area.getHeight()));
}
