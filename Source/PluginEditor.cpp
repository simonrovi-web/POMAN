#include "PluginEditor.h"

#include <numeric>

namespace palette
{
const juce::Colour abyss { 0xff070b10 };
const juce::Colour trench { 0xff0f171f };
const juce::Colour panel { 0xff15202a };
const juce::Colour panelEdge { 0xff22313d };
const juce::Colour text { 0xffd9e6ee };
const juce::Colour dimText { 0xff7f94a3 };
const juce::Colour biolume { 0xff2fe0c8 };
const juce::Colour pressure { 0xff4c8dff };
const juce::Colour ember { 0xffff7a45 };
const juce::Colour kelp { 0xffa3e05a };
} // namespace palette

//==============================================================================
PomanLookAndFeel::PomanLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, palette::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, palette::dimText);
    setColour (juce::TextButton::textColourOffId, palette::dimText);
    setColour (juce::TextButton::textColourOnId, palette::abyss);
    setColour (juce::ComboBox::backgroundColourId, palette::trench);
    setColour (juce::ComboBox::outlineColourId, palette::panelEdge);
    setColour (juce::ComboBox::textColourId, palette::text);
    setColour (juce::ComboBox::arrowColourId, palette::dimText);
    setColour (juce::PopupMenu::backgroundColourId, palette::panel);
    setColour (juce::PopupMenu::textColourId, palette::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, palette::biolume.withAlpha (0.25f));
    setColour (juce::PopupMenu::highlightedTextColourId, palette::text);
    setColour (juce::PopupMenu::headerTextColourId, palette::biolume);
}

void PomanLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                           float startAngle, float endAngle, juce::Slider& slider)
{
    const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (4.0f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + sliderPos * (endAngle - startAngle);
    const float trackWidth = juce::jmax (2.5f, radius * 0.1f);
    const float arcRadius = radius - trackWidth * 0.5f;

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, endAngle, true);
    g.setColour (palette::abyss);
    g.strokePath (track, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, arcRadius, arcRadius, 0.0f, startAngle, angle, true);
    g.setColour (accent.withAlpha (0.25f));
    g.strokePath (value, juce::PathStrokeType (trackWidth * 2.2f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (trackWidth, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float capRadius = radius * 0.68f;
    juce::ColourGradient cap (palette::panelEdge.brighter (0.25f), centre.x, centre.y - capRadius,
                              palette::abyss, centre.x, centre.y + capRadius, false);
    g.setGradientFill (cap);
    g.fillEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre));
    g.setColour (palette::panelEdge);
    g.drawEllipse (juce::Rectangle<float> (capRadius * 2.0f, capRadius * 2.0f).withCentre (centre), 1.0f);

    juce::Path pointer;
    pointer.addRoundedRectangle (-1.5f, -capRadius * 0.92f, 3.0f, capRadius * 0.5f, 1.5f);
    g.setColour (palette::text);
    g.fillPath (pointer, juce::AffineTransform::rotation (angle).translated (centre));
}

void PomanLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    const auto accent = button.findColour (juce::ToggleButton::tickColourId);
    const auto area = button.getLocalBounds().toFloat().reduced (2.0f);
    const float size = juce::jmin (area.getWidth(), area.getHeight());
    const auto led = juce::Rectangle<float> (size, size).withCentre (area.getCentre());
    const bool on = button.getToggleState();

    if (on)
    {
        g.setColour (accent.withAlpha (0.3f));
        g.fillEllipse (led.expanded (3.0f));
    }

    g.setColour (on ? accent : palette::abyss);
    g.fillEllipse (led);
    g.setColour (highlighted ? palette::text : palette::panelEdge.brighter (0.3f));
    g.drawEllipse (led, 1.2f);
}

void PomanLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour&,
                                               bool highlighted, bool down)
{
    const auto area = button.getLocalBounds().toFloat().reduced (1.0f);
    const auto accent = button.findColour (juce::TextButton::buttonOnColourId);
    const bool on = button.getToggleState();

    g.setColour (on ? accent : (highlighted || down ? palette::panelEdge : palette::trench));
    g.fillRoundedRectangle (area, 4.0f);
    g.setColour (on ? accent.brighter (0.3f) : palette::panelEdge);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);
}

namespace
{
juce::File defaultIrFolder()
{
    const auto bundled = poman::portableRoot().getChildFile ("IRs");
    return bundled.isDirectory() ? bundled : juce::File();
}
} // namespace

//==============================================================================
PomanEditor::PomanEditor (PomanProcessor& p)
    : AudioProcessorEditor (&p), processor (p)
{
    setLookAndFeel (&lookAndFeel);

    inputKnob = &addKnob ("input", "INPUT");
    outputKnob = &addKnob ("output", "OUTPUT");
    for (auto* k : { inputKnob, outputKnob })
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, palette::text);

    addSection ("GATE", palette::kelp, "gateOn", { &addKnob ("gateThreshold", "THRESHOLD") });
    addSection ("SUBMERGE // OCTAVE", palette::pressure, "octOn",
                { &addKnob ("octSub", "SUB"), &addKnob ("octUp", "UP"), &addKnob ("octDry", "DRY") });
    driveSection = &addSection ("DRIVE", palette::ember, "odOn",
                { &addKnob ("odDrive", "DRIVE"), &addKnob ("odTone", "TONE"), &addKnob ("odLevel", "LEVEL") });

    ampSection = &addSection ("AMPLIFIER", palette::biolume, {},
                              { &addKnob ("ampGain", "GAIN", true), &addKnob ("ampBass", "BASS"), &addKnob ("ampMid", "MID"),
                                &addKnob ("ampTreble", "TREBLE"), &addKnob ("ampPresence", "PRESENCE"),
                                &addKnob ("ampDepth", "DEPTH"), &addKnob ("ampMaster", "MASTER", true) });

    cabSection = &addSection ("HULL // CABINET", palette::biolume, "cabOn", { &addKnob ("cabMic", "MIC POS") });
    addSection ("ECHOLOCATION // DELAY", palette::pressure, "dlyOn",
                { &addKnob ("dlyTime", "TIME"), &addKnob ("dlyFeedback", "FEEDBACK"), &addKnob ("dlyMix", "MIX") });
    addSection ("TRENCH // REVERB", palette::pressure, "revOn",
                { &addKnob ("revSize", "SIZE"), &addKnob ("revMix", "MIX") });

    // Item ids must follow the choice order: ComboBoxAttachment maps id - 1 to the parameter index.
    ampModelBox.addItemList (poman::AmpModel::modelNames(), 1);
    ampModelBox.setColour (juce::ComboBox::outlineColourId, palette::biolume.withAlpha (0.6f));
    ampModelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "ampChannel", ampModelBox);
    addAndMakeVisible (ampModelBox);

    driveTypeBox.addItemList (poman::Overdrive::typeNames(), 1);
    driveTypeBox.setColour (juce::ComboBox::outlineColourId, palette::ember.withAlpha (0.6f));
    driveTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "odType", driveTypeBox);
    addAndMakeVisible (driveTypeBox);

    for (auto* b : { &loadIrButton, &clearIrButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, palette::biolume);
        addAndMakeVisible (*b);
    }

    loadIrButton.onClick = [this]
    {
        fileChooser = std::make_unique<juce::FileChooser> ("Load cabinet impulse response", defaultIrFolder(), "*.wav;*.aif;*.aiff");
        fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                                  [this] (const juce::FileChooser& chooser)
                                  {
                                      const auto file = chooser.getResult();
                                      if (file.existsAsFile())
                                      {
                                          processor.loadImpulseResponse (file);
                                          refreshImpulseResponseLabel();
                                      }
                                  });
    };

    clearIrButton.onClick = [this]
    {
        processor.clearImpulseResponse();
        refreshImpulseResponseLabel();
    };

    irLabel.setJustificationType (juce::Justification::centred);
    irLabel.setFont (juce::FontOptions (12.0f));
    addAndMakeVisible (irLabel);
    refreshImpulseResponseLabel();

    // Presets
    presetBox.setTextWhenNothingSelected ("Preset");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getPresetManager().getCurrentIndex())
            processor.getPresetManager().loadPreset (index);
        refreshPresetBox();
        refreshImpulseResponseLabel();
    };
    previousPresetButton.onClick = [this] { processor.getPresetManager().loadPrevious(); refreshPresetBox(); refreshImpulseResponseLabel(); };
    nextPresetButton.onClick = [this] { processor.getPresetManager().loadNext(); refreshPresetBox(); refreshImpulseResponseLabel(); };
    savePresetButton.onClick = [this] { showSavePresetDialog(); };

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &previousPresetButton, &nextPresetButton, &savePresetButton })
        addAndMakeVisible (c);
    refreshPresetBox();

    // Tuner
    tunerButton.setClickingTogglesState (true);
    tunerButton.setColour (juce::TextButton::buttonOnColourId, palette::kelp);
    tunerButton.onClick = [this] { setTunerVisible (tunerButton.getToggleState()); };
    addAndMakeVisible (tunerButton);
    addChildComponent (tunerDisplay);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    setResizable (true, true);
    setResizeLimits (900, 560, 1600, 1000);
    getConstrainer()->setFixedAspectRatio (1040.0 / 640.0);
    setSize (1040, 640);
    startTimerHz (30);
}

PomanEditor::~PomanEditor()
{
    processor.setTunerActive (false);
    setLookAndFeel (nullptr);
}

void PomanEditor::refreshPresetBox()
{
    auto& presets = processor.getPresetManager();
    presets.refreshUserPresets();
    const auto names = presets.getPresetNames();

    presetBox.clear (juce::dontSendNotification);
    presetBox.addSectionHeading ("FACTORY");
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == presets.getNumFactoryPresets())
            presetBox.addSectionHeading ("USER");
        presetBox.addItem (names[i], i + 1);
    }

    shownPresetName = presets.getCurrentName();
    const int current = presets.getCurrentIndex();
    if (current >= 0)
        presetBox.setSelectedId (current + 1, juce::dontSendNotification);
    else
        presetBox.setText (shownPresetName, juce::dontSendNotification);
}

void PomanEditor::showSavePresetDialog()
{
    saveDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name for the new preset:", juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", processor.getPresetManager().getCurrentName());
    saveDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveDialog != nullptr)
        {
            const auto name = saveDialog->getTextEditorContents ("name");
            if (! processor.getPresetManager().saveUserPreset (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "Save preset",
                                                        "Could not save to " + processor.getPresetManager().getUserFolder().getFullPathName());
            refreshPresetBox();
        }
        saveDialog = nullptr;
    }), false);
}

void PomanEditor::setTunerVisible (bool visible)
{
    processor.setTunerActive (visible);
    tunerDisplay.setVisible (visible);
    if (visible)
        tunerDisplay.toFront (false);
}

void PomanEditor::timerCallback()
{
    inputMeter.pushPeak (processor.takeInputPeak());
    outputMeter.pushPeak (processor.takeOutputPeak());

    if (tunerDisplay.isVisible())
        tunerDisplay.setReading (processor.analyseTuner());

    // Session restores and host preset changes can swap the state underneath the editor.
    if (processor.getPresetManager().getCurrentName() != shownPresetName)
    {
        refreshPresetBox();
        refreshImpulseResponseLabel();
    }
}

//==============================================================================
void LevelMeter::pushPeak (float peak)
{
    const float db = juce::Decibels::gainToDecibels (peak, -100.0f);
    levelDb = db > levelDb ? db : juce::jmax (db, levelDb - 1.5f);
    if (peak >= 0.99f)
        clipHoldFrames = 45;
    else if (clipHoldFrames > 0)
        --clipHoldFrames;
    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    const auto led = area.removeFromTop (area.getWidth());
    area.removeFromTop (2.0f);

    g.setColour (clipHoldFrames > 0 ? juce::Colour (0xffff3b30) : palette::abyss);
    g.fillRoundedRectangle (led, 2.0f);

    g.setColour (palette::abyss);
    g.fillRoundedRectangle (area, 2.0f);

    const float proportion = juce::jlimit (0.0f, 1.0f, (levelDb + 60.0f) / 60.0f);
    const auto fill = area.withTop (area.getBottom() - area.getHeight() * proportion);
    const auto colour = levelDb > -3.0f ? palette::ember : (levelDb > -12.0f ? juce::Colour (0xffffc94a) : palette::kelp);
    g.setColour (colour);
    g.fillRoundedRectangle (fill, 2.0f);
}

void TunerDisplay::setReading (const poman::TunerReading& reading)
{
    if (reading.frequency > 0.0f)
    {
        if (reading.midiNote != current.midiNote)
            smoothedCents = reading.cents;
        current = reading;
        smoothedCents += 0.35f * (reading.cents - smoothedCents);
        silentFrames = 0;
    }
    else
    {
        ++silentFrames;
    }
    repaint();
}

void TunerDisplay::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (palette::abyss.withAlpha (0.96f));
    g.fillRoundedRectangle (bounds, 8.0f);
    g.setColour (palette::kelp);
    g.drawRoundedRectangle (bounds.reduced (0.5f), 8.0f, 1.5f);

    const bool hasPitch = silentFrames < 20 && current.frequency > 0.0f;
    const bool inTune = hasPitch && std::abs (smoothedCents) < 3.0f;
    const float h = bounds.getHeight();

    g.setFont (juce::FontOptions (h * 0.32f, juce::Font::bold));
    g.setColour (hasPitch ? (inTune ? palette::kelp : palette::text) : palette::dimText);
    g.drawText (hasPitch ? current.noteName() : juce::String ("--"), bounds.withTrimmedBottom (h * 0.35f), juce::Justification::centred);

    g.setFont (juce::FontOptions (h * 0.08f));
    g.setColour (palette::dimText);
    const auto info = hasPitch ? juce::String (current.frequency, 1) + " Hz   " + (smoothedCents >= 0.0f ? "+" : "") + juce::String (smoothedCents, 1) + " cents"
                               : juce::String ("Play a string - output muted");
    g.drawText (info, bounds.withTop (bounds.getBottom() - h * 0.42f).withHeight (h * 0.12f), juce::Justification::centred);

    // Cents scale -50..+50
    const auto scale = bounds.reduced (bounds.getWidth() * 0.12f, 0.0f).withTop (bounds.getBottom() - h * 0.26f).withHeight (h * 0.1f);
    for (int c = -50; c <= 50; c += 10)
    {
        const float x = scale.getX() + scale.getWidth() * (float) (c + 50) / 100.0f;
        g.setColour (c == 0 ? palette::kelp : palette::panelEdge.brighter (0.4f));
        g.fillRect (x - 1.0f, scale.getY(), 2.0f, scale.getHeight() * (c == 0 ? 1.0f : 0.6f));
    }

    if (hasPitch)
    {
        const float x = scale.getX() + scale.getWidth() * (juce::jlimit (-50.0f, 50.0f, smoothedCents) + 50.0f) / 100.0f;
        g.setColour (inTune ? palette::kelp : palette::ember);
        g.fillRoundedRectangle (x - 4.0f, scale.getY() - 6.0f, 8.0f, scale.getHeight() + 12.0f, 3.0f);
    }
}


PomanEditor::Knob& PomanEditor::addKnob (const juce::String& paramId, const juce::String& name, bool large)
{
    auto knob = std::make_unique<Knob>();
    knob->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
    knob->slider.setRotaryParameters (juce::degreesToRadians (225.0f), juce::degreesToRadians (495.0f), true);
    knob->slider.setPopupDisplayEnabled (false, false, this);
    knob->slider.getProperties().set ("large", large);
    knob->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, paramId, knob->slider);
    knob->slider.setNumDecimalPlacesToDisplay (1);

    knob->label.setText (name, juce::dontSendNotification);
    knob->label.setJustificationType (juce::Justification::centred);
    knob->label.setFont (juce::FontOptions (12.0f, juce::Font::bold));

    addAndMakeVisible (knob->slider);
    addAndMakeVisible (knob->label);
    knobs.push_back (std::move (knob));
    return *knobs.back();
}

PomanEditor::Section& PomanEditor::addSection (const juce::String& title, juce::Colour accent,
                                                   const juce::String& powerParamId, std::initializer_list<Knob*> sectionKnobs)
{
    auto section = std::make_unique<Section>();
    section->title = title;
    section->accent = accent;
    section->knobs = sectionKnobs;

    for (auto* k : section->knobs)
        k->slider.setColour (juce::Slider::rotarySliderFillColourId, accent);

    if (powerParamId.isNotEmpty())
    {
        section->power = std::make_unique<juce::ToggleButton>();
        section->power->setColour (juce::ToggleButton::tickColourId, accent);
        section->powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.apvts, powerParamId, *section->power);
        addAndMakeVisible (*section->power);
    }

    sections.push_back (std::move (section));
    return *sections.back();
}

void PomanEditor::refreshImpulseResponseLabel()
{
    const auto name = processor.getImpulseResponseName();
    irLabel.setText (name.isEmpty() ? "Built-in 4x12 model" : name, juce::dontSendNotification);
    clearIrButton.setToggleState (name.isEmpty(), juce::dontSendNotification);
}

void PomanEditor::layoutKnobs (Section& section, juce::Rectangle<int> area)
{
    if (section.knobs.empty())
        return;

    const int cellWidth = area.getWidth() / (int) section.knobs.size();
    for (auto* k : section.knobs)
    {
        auto cell = area.removeFromLeft (cellWidth);
        const bool large = (bool) k->slider.getProperties()["large"];
        const int size = juce::jmin (cell.getWidth() - 8, cell.getHeight() - 22, large ? 110 : 82);
        auto knobArea = cell.withSizeKeepingCentre (size, size + 18).translated (0, 6);
        k->label.setBounds (knobArea.getX() - 10, knobArea.getY() - 18, knobArea.getWidth() + 20, 16);
        k->slider.setBounds (knobArea);
    }
}

//==============================================================================
void PomanEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    juce::ColourGradient depth (palette::trench, bounds.getCentreX(), 0.0f, palette::abyss, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill (depth);
    g.fillAll();

    // Faint "bioluminescent" particles; seeded so they stay put between repaints.
    juce::Random rng (0xab155a1);
    for (int i = 0; i < 70; ++i)
    {
        const float px = rng.nextFloat() * bounds.getWidth();
        const float py = rng.nextFloat() * bounds.getHeight();
        const float r = 0.6f + rng.nextFloat() * 1.6f;
        g.setColour (palette::biolume.withAlpha (0.04f + rng.nextFloat() * 0.12f));
        g.fillEllipse (px, py, r, r);
    }

    const float scale = (float) getWidth() / 1040.0f;
    auto header = getLocalBounds().removeFromTop ((int) (64 * scale)).reduced ((int) (20 * scale), 0);
    g.setColour (palette::text);
    g.setFont (juce::FontOptions (34.0f * scale, juce::Font::bold));
    g.drawText ("POMAN", header, juce::Justification::centredLeft);
    g.setColour (palette::biolume);
    g.setFont (juce::FontOptions (11.0f * scale));
    g.drawText ("GUITAR AMP SUITE", header.withTrimmedLeft ((int) (128 * scale)), juce::Justification::centredLeft);

    for (auto& s : sections)
    {
        const auto area = s->bounds.toFloat();
        g.setColour (palette::panel.withAlpha (0.92f));
        g.fillRoundedRectangle (area, 8.0f);
        g.setColour (palette::panelEdge);
        g.drawRoundedRectangle (area, 8.0f, 1.0f);

        g.setColour (s->accent);
        g.fillRoundedRectangle (area.withHeight (3.0f).reduced (12.0f, 0.0f), 1.5f);

        g.setColour (palette::text);
        g.setFont (juce::FontOptions (13.0f * scale, juce::Font::bold));
        g.drawText (s->title, s->bounds.reduced ((int) (14 * scale), 0).withHeight ((int) (34 * scale)).withTrimmedLeft (s->power ? (int) (24 * scale) : 0),
                    juce::Justification::centredLeft);
    }
}

void PomanEditor::resized()
{
    const float scale = (float) getWidth() / 1040.0f;
    const auto sc = [scale] (int v) { return juce::roundToInt ((float) v * scale); };

    auto area = getLocalBounds().reduced (sc (16), 0);
    auto header = area.removeFromTop (sc (64));
    area.removeFromBottom (sc (14));

    auto io = header.removeFromRight (sc (220)).reduced (0, sc (6));
    for (auto [k, meter] : { std::pair { inputKnob, &inputMeter }, std::pair { outputKnob, &outputMeter } })
    {
        auto cell = io.removeFromLeft (sc (110));
        k->label.setBounds (cell.removeFromLeft (sc (50)).withSizeKeepingCentre (sc (50), sc (16)));
        meter->setBounds (cell.removeFromRight (sc (10)).reduced (0, sc (2)));
        k->slider.setBounds (cell.withSizeKeepingCentre (sc (44), sc (52)));
        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    }

    header.removeFromLeft (sc (270));
    auto presetRow = header.withSizeKeepingCentre (header.getWidth(), sc (30));
    tunerButton.setBounds (presetRow.removeFromRight (sc (80)).reduced (sc (4), 0));
    presetRow.removeFromRight (sc (10));
    savePresetButton.setBounds (presetRow.removeFromRight (sc (60)).reduced (sc (2), 0));
    previousPresetButton.setBounds (presetRow.removeFromLeft (sc (30)).reduced (sc (2), 0));
    nextPresetButton.setBounds (presetRow.removeFromRight (sc (30)).reduced (sc (2), 0));
    presetBox.setBounds (presetRow.reduced (sc (2), 0));

    const int gap = sc (10);
    const int rowHeight = (area.getHeight() - 2 * gap) / 3;
    auto pedals = area.removeFromTop (rowHeight);
    area.removeFromTop (gap);
    auto ampRow = area.removeFromTop (rowHeight);
    area.removeFromTop (gap);
    auto fxRow = area;

    const auto placeRow = [&] (juce::Rectangle<int> row, std::initializer_list<Section*> rowSections, std::initializer_list<int> weights)
    {
        const int totalWeight = std::accumulate (weights.begin(), weights.end(), 0);
        const int available = row.getWidth() - gap * ((int) rowSections.size() - 1);
        auto weight = weights.begin();
        for (auto* s : rowSections)
        {
            s->bounds = row.removeFromLeft (available * *weight++ / totalWeight);
            row.removeFromLeft (gap);
        }
    };

    placeRow (pedals, { sections[0].get(), sections[1].get(), sections[2].get() }, { 1, 3, 3 });
    placeRow (ampRow, { ampSection }, { 1 });
    placeRow (fxRow, { cabSection, sections[5].get(), sections[6].get() }, { 3, 3, 2 });

    for (auto& s : sections)
    {
        auto inner = s->bounds.reduced (sc (10), sc (8));
        auto titleBar = inner.removeFromTop (sc (26));
        if (s->power != nullptr)
            s->power->setBounds (titleBar.removeFromLeft (sc (20)).withSizeKeepingCentre (sc (16), sc (16)).translated (0, sc (1)));

        if (s.get() == ampSection)
            ampModelBox.setBounds (titleBar.removeFromRight (sc (240)).reduced (sc (2), sc (1)));

        if (s.get() == driveSection)
            driveTypeBox.setBounds (titleBar.removeFromRight (sc (150)).reduced (sc (2), sc (1)));

        if (s.get() == cabSection)
        {
            auto irArea = inner.removeFromRight (inner.getWidth() / 2).reduced (sc (6), sc (10));
            auto buttons = irArea.removeFromTop (sc (30));
            loadIrButton.setBounds (buttons.removeFromLeft (buttons.getWidth() / 2).reduced (sc (2), 0));
            clearIrButton.setBounds (buttons.reduced (sc (2), 0));
            irLabel.setBounds (irArea.removeFromTop (sc (36)));
        }

        layoutKnobs (*s, inner.withTrimmedTop (sc (14)));
    }

    tunerDisplay.setBounds (ampSection->bounds);
}
