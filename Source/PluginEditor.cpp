#include "PluginEditor.h"
#include "Localisation.h"

using poman::i18n::tr;
#include "Palette.h"

#include <numeric>


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

juce::Font PomanLookAndFeel::getTextButtonFont (juce::TextButton& button, int buttonHeight)
{
    if (const auto* size = button.getProperties().getVarPointer ("fontSize"))
        return juce::FontOptions ((float) *size, juce::Font::bold);
    return LookAndFeel_V4::getTextButtonFont (button, buttonHeight);
}

void PomanLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height, float sliderPos,
                                         float minPos, float maxPos, juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical)
    {
        LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minPos, maxPos, style, slider);
        return;
    }

    // EQ fader: thin track, fill from the 0 dB centre to the thumb.
    const auto accent = slider.findColour (juce::Slider::trackColourId);
    const float cx = (float) x + (float) width * 0.5f;
    g.setColour (palette::abyss);
    g.fillRoundedRectangle (cx - 2.5f, (float) y, 5.0f, (float) height, 2.5f);

    const float zeroY = (float) slider.getPositionOfValue (0.0);
    g.setColour (accent);
    g.fillRoundedRectangle (cx - 2.5f, juce::jmin (zeroY, sliderPos), 5.0f, std::abs (zeroY - sliderPos), 2.5f);

    g.setColour (palette::panelEdge.brighter (0.5f));
    g.fillRect (cx - 7.0f, zeroY, 14.0f, 1.0f);

    const float thumbWidth = juce::jmin (22.0f, (float) width * 0.8f);
    const auto thumb = juce::Rectangle<float> (thumbWidth, 9.0f).withCentre ({ cx, sliderPos });
    g.setColour (palette::text);
    g.fillRoundedRectangle (thumb, 3.0f);
    g.setColour (palette::abyss);
    g.fillRect (thumb.withSizeKeepingCentre (thumbWidth - 6.0f, 1.0f));
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
// Starts file choosers in the portable folder's IRs/ or Models/ when it exists.
juce::File defaultFolder (const char* name)
{
    const auto bundled = poman::portableRoot().getChildFile (name);
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

    gateSection = &addSection ("GATE", palette::kelp, "gateOn", { &addKnob ("gateThreshold", "THRESHOLD") });
    compSection = &addSection ("COMP", palette::kelp, "compOn",
                               { &addKnob ("compSustain", "SUSTAIN"), &addKnob ("compAttack", "ATTACK"),
                                 &addKnob ("compLevel", "LEVEL"), &addKnob ("compBlend", "BLEND") });
    compGrLabel.setJustificationType (juce::Justification::centredRight);
    compGrLabel.setFont (juce::FontOptions (11.0f, juce::Font::bold));
    compGrLabel.setColour (juce::Label::textColourId, palette::kelp);
    addAndMakeVisible (compGrLabel);

    octaveSection = &addSection ("OCTAVE", palette::pressure, "octOn",
                { &addKnob ("octSub", "SUB"), &addKnob ("octUp", "UP"), &addKnob ("octDry", "DRY") });
    wahSection = &addSection ("WAH", palette::sunlight, "wahOn",
                              { &addKnob ("wahPos", "POSITION"), &addKnob ("wahReso", "RESO"), &addKnob ("volPedal", "VOLUME") });
    wahModeBox.addItemList (poman::Wah::modeNames(), 1);
    wahModeBox.setColour (juce::ComboBox::outlineColourId, palette::sunlight.withAlpha (0.6f));
    wahModeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "wahMode", wahModeBox);
    addAndMakeVisible (wahModeBox);
    wahAutoButton.setClickingTogglesState (true);
    wahAutoButton.setColour (juce::TextButton::buttonOnColourId, palette::sunlight.withAlpha (0.7f));
    wahAutoButton.setTooltip ("Right-click POSITION to MIDI-learn an expression pedal. With PEDAL ON, moving the pedal "
                              "switches the wah on, and resting at the heel for half a second switches it off");
    wahAutoAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (processor.apvts, "wahAuto", wahAutoButton);
    addAndMakeVisible (wahAutoButton);

    driveSection = &addSection ("DRIVE", palette::ember, "odOn",
                { &addKnob ("odDrive", "DRIVE"), &addKnob ("odTone", "TONE"), &addKnob ("odLevel", "LEVEL") });

    eqSection = &addSection ("GRAPHIC EQ", palette::sunlight, "eqOn",
                             { &addKnob ("eqLowCut", "LOW CUT"), &addKnob ("eqHighCut", "HIGH CUT"), &addKnob ("eqLevel", "LEVEL") });
    {
        const char* ids[] { "eq31", "eq63", "eq125", "eq250", "eq500", "eq1k", "eq2k", "eq4k", "eq8k", "eq16k" };
        const auto labels = poman::GraphicEQ::bandLabels();
        for (int b = 0; b < poman::GraphicEQ::numBands; ++b)
        {
            auto& s = eqSliders[(size_t) b];
            s.setSliderStyle (juce::Slider::LinearVertical);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setColour (juce::Slider::trackColourId, palette::sunlight);
            s.setPopupDisplayEnabled (true, true, this);
            eqAttachments[(size_t) b] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (processor.apvts, ids[b], s);
            s.setDoubleClickReturnValue (true, 0.0); // after the attachment, which resets it
            addAndMakeVisible (s);

            auto& l = eqLabels[(size_t) b];
            l.setText (labels[b], juce::dontSendNotification);
            l.setJustificationType (juce::Justification::centred);
            l.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            addAndMakeVisible (l);
        }
        addAndMakeVisible (eqCurve);
    }

    ampSection = &addSection ("AMPLIFIER", palette::biolume, {},
                              { &addKnob ("ampGain", "GAIN", true), &addKnob ("ampBass", "BASS"), &addKnob ("ampMid", "MID"),
                                &addKnob ("ampTreble", "TREBLE"), &addKnob ("ampPresence", "PRESENCE"),
                                &addKnob ("ampDepth", "DEPTH"), &addKnob ("ampMaster", "MASTER", true) });

    cabSection = &addSection ("CABINET", palette::biolume, "cabOn",
                              { &addKnob ("cabMic", "MIC POS"), &addKnob ("cabMix", "A/B MIX"),
                                &addKnob ("cabLowCut", "LOW CUT"), &addKnob ("cabHighCut", "HIGH CUT") });
    delayTimeKnob = &addKnob ("dlyTime", "TIME");
    delaySection = &addSection ("DELAY", palette::pressure, "dlyOn",
                { delayTimeKnob, &addKnob ("dlyFeedback", "FEEDBACK"), &addKnob ("dlyMix", "MIX") });
    delaySyncBox.addItemList (poman::delayDivisionNames(), 1);
    delaySyncBox.setColour (juce::ComboBox::outlineColourId, palette::pressure.withAlpha (0.6f));
    delaySyncBox.setTooltip ("Sync the delay to the tempo (DAW tempo in a plugin, studio/drums tempo standalone)");
    delaySyncAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "dlySync", delaySyncBox);
    addAndMakeVisible (delaySyncBox);

    tapButton.setTooltip ("Tap the tempo (shared with STUDIO and DRUMS)");
    tapButton.onClick = [this]
    {
        if (const double bpm = tapTempo.tap (juce::Time::getMillisecondCounterHiRes() * 0.001); bpm > 0.0)
        {
            processor.getRecorder().setBpm (bpm);
            tapButton.setButtonText (juce::String (juce::roundToInt (bpm)));
            tapShownUntil = juce::Time::getMillisecondCounterHiRes() * 0.001 + 2.0;
        }
    };
    addAndMakeVisible (tapButton);

    calButton.setTooltip ("Input calibration: press, then play hard for 5 seconds");
    calButton.setColour (juce::TextButton::buttonOnColourId, palette::kelp);
    calButton.onClick = [this] { startCalibration(); };
    addAndMakeVisible (calButton);
    inputMeter.setTargetZone (-12.0f, -6.0f);
    reverbSection = &addSection ("REVERB", palette::pressure, "revOn",
                { &addKnob ("revSize", "SIZE"), &addKnob ("revTone", "TONE"), &addKnob ("revMix", "MIX") });

    harmIntervalKnobs = { &addKnob ("harmV1Steps", "VOICE 1"), &addKnob ("harmV2Steps", "VOICE 2") };
    harmonizerSection = &addSection ("HARMONIZER", palette::aurora, "harmOn",
                                     { harmIntervalKnobs[0], &addKnob ("harmV1Level", "V1 LEVEL"),
                                       harmIntervalKnobs[1], &addKnob ("harmV2Level", "V2 LEVEL"), &addKnob ("harmDry", "DRY") });

    modVariableKnobs = { &addKnob ("modMix", "MIX"), &addKnob ("modFeedback", "FDBK") };
    modSection = &addSection ("MOD", palette::coral, "modOn",
                              { &addKnob ("modRate", "RATE"), &addKnob ("modDepth", "DEPTH"), modVariableKnobs[0], modVariableKnobs[1] });
    modTypeBox.addItemList (poman::Modulation::typeNames(), 1);
    modTypeBox.setColour (juce::ComboBox::outlineColourId, palette::coral.withAlpha (0.6f));
    modTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "modType", modTypeBox);
    addAndMakeVisible (modTypeBox);

    revTypeBox.addItemList (poman::Ambience::typeNames(), 1);
    revTypeBox.setColour (juce::ComboBox::outlineColourId, palette::pressure.withAlpha (0.6f));
    revTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "revType", revTypeBox);
    addAndMakeVisible (revTypeBox);

    // Interval knobs read as musical intervals; their meaning follows the mode (degrees vs semitones).
    for (auto* k : harmIntervalKnobs)
    {
        k->slider.textFromValueFunction = [this] (double v) { return intervalName (juce::roundToInt (v)); };
        k->slider.updateText();
    }

    const auto setupBox = [this] (juce::ComboBox& box, const juce::StringArray& items, const char* paramId,
                                  std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>& attachment)
    {
        box.addItemList (items, 1);
        box.setColour (juce::ComboBox::outlineColourId, palette::aurora.withAlpha (0.6f));
        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, paramId, box);
        addAndMakeVisible (box);
    };
    setupBox (harmModeBox, poman::Harmonizer::modeNames(), "harmMode", harmModeAttachment);
    setupBox (harmKeyBox, poman::Harmonizer::keyNames(), "harmKey", harmKeyAttachment);
    setupBox (harmScaleBox, poman::Harmonizer::scaleNames(), "harmScale", harmScaleAttachment);

    harmInfoLabel.setJustificationType (juce::Justification::centred);
    harmInfoLabel.setFont (juce::FontOptions (15.0f, juce::Font::bold));
    harmInfoLabel.setColour (juce::Label::textColourId, palette::text);
    harmInfoLabel.setColour (juce::Label::backgroundColourId, palette::abyss.withAlpha (0.6f));
    harmInfoLabel.setColour (juce::Label::outlineColourId, palette::panelEdge);
    addAndMakeVisible (harmInfoLabel);

    // Item ids must follow the choice order: ComboBoxAttachment maps id - 1 to the parameter index.
    ampModelBox.addItemList (PomanProcessor::ampModelChoices(), 1);
    ampModelBox.setColour (juce::ComboBox::outlineColourId, palette::biolume.withAlpha (0.6f));
    ampModelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "ampChannel", ampModelBox);
    addAndMakeVisible (ampModelBox);

    driveTypeBox.addItemList (poman::Overdrive::typeNames(), 1);
    driveTypeBox.setColour (juce::ComboBox::outlineColourId, palette::ember.withAlpha (0.6f));
    driveTypeAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (processor.apvts, "odType", driveTypeBox);
    addAndMakeVisible (driveTypeBox);

    for (auto* b : { &irPrevButton, &irNextButton, &irAButton, &irBButton, &builtInCabButton, &loadNamButton })
    {
        b->setColour (juce::TextButton::buttonOnColourId, palette::biolume);
        addAndMakeVisible (*b);
    }
    irAButton.setTooltip ("Load an impulse response into slot A");
    irBButton.setTooltip ("Load a second IR into slot B (blended with A/B MIX, phase-aligned automatically)");
    builtInCabButton.setTooltip ("Remove both IRs and use the built-in 4x12 model");
    loadNamButton.setTooltip ("Load a Neural Amp Modeler capture (.nam) and select NAM Capture");
    irPrevButton.setTooltip ("Previous IR in the folder (slot A)");
    irNextButton.setTooltip ("Next IR in the folder (slot A)");
    irPrevButton.onClick = [this] { stepIr (-1); };
    irNextButton.onClick = [this] { stepIr (1); };
    irAButton.onClick = [this] { chooseFile (PomanProcessor::FileSlot::irA); };
    irBButton.onClick = [this] { chooseFile (PomanProcessor::FileSlot::irB); };
    loadNamButton.onClick = [this] { chooseFile (PomanProcessor::FileSlot::namModel); };
    builtInCabButton.onClick = [this]
    {
        processor.clearFile (PomanProcessor::FileSlot::irA);
        processor.clearFile (PomanProcessor::FileSlot::irB);
        refreshFileLabels();
    };

    for (auto* l : { &irLabel, &namLabel })
    {
        l->setFont (juce::FontOptions (12.0f));
        l->setMinimumHorizontalScale (0.7f);
        addAndMakeVisible (*l);
    }
    irLabel.setJustificationType (juce::Justification::centred);
    namLabel.setJustificationType (juce::Justification::centredRight);
    refreshFileLabels();

    // Presets
    presetBox.setTextWhenNothingSelected ("Preset");
    presetBox.onChange = [this]
    {
        const int index = presetBox.getSelectedId() - 1;
        if (index >= 0 && index != processor.getPresetManager().getCurrentIndex())
            processor.getPresetManager().loadPreset (index);
        refreshPresetBox();
        refreshFileLabels();
    };
    previousPresetButton.onClick = [this] { processor.getPresetManager().loadPrevious(); refreshPresetBox(); refreshFileLabels(); };
    nextPresetButton.onClick = [this] { processor.getPresetManager().loadNext(); refreshPresetBox(); refreshFileLabels(); };
    savePresetButton.onClick = [this] { showSavePresetDialog(); };

    for (auto* c : std::initializer_list<juce::Component*> { &presetBox, &previousPresetButton, &nextPresetButton, &savePresetButton })
        addAndMakeVisible (c);
    refreshPresetBox();

    // Tuner
    tunerButton.setClickingTogglesState (true);
    tunerButton.setColour (juce::TextButton::buttonOnColourId, palette::kelp);
    tunerButton.onClick = [this] { setTunerVisible (tunerButton.getToggleState()); };
    tunerButton.onRightClick = [this] { showMidiMenu ("action:tuner", tunerButton); };
    previousPresetButton.onRightClick = [this] { showMidiMenu ("action:presetPrev", previousPresetButton); };
    nextPresetButton.onRightClick = [this] { showMidiMenu ("action:presetNext", nextPresetButton); };
    addAndMakeVisible (tunerButton);
    addChildComponent (tunerDisplay);

    addAndMakeVisible (inputMeter);
    addAndMakeVisible (outputMeter);

    if (processor.wrapperType == juce::AudioProcessor::wrapperType_Standalone)
    {
        studioView = std::make_unique<StudioView> (processor.getRecorder());
        studioView->makeExportLayer = [this] { return processor.makeDrumExportLayer(); };
        addChildComponent (*studioView);
        drumView = std::make_unique<DrumView> (processor.getDrums(), processor.getRecorder());
        addChildComponent (*drumView);
        loopView = std::make_unique<LoopView> (processor.getLooper(),
                                               [this] (const juce::String& target, juce::Component& anchor) { showMidiMenu (target, anchor); },
                                               [this] (const juce::String& target) { return processor.getMidiRouter().getCcFor (target); });
        addChildComponent (*loopView);
        liveView = std::make_unique<LiveView> (processor,
                                               [this] (const juce::String& target, juce::Component& anchor) { showMidiMenu (target, anchor); },
                                               [this] { return lastTunerReading; });
        addChildComponent (*liveView);

        for (auto* b : { &rigPageButton, &studioPageButton, &drumsPageButton, &loopPageButton, &livePageButton })
        {
            b->setRadioGroupId (1001);
            b->setClickingTogglesState (true);
            b->setColour (juce::TextButton::buttonOnColourId, palette::biolume);
            addAndMakeVisible (*b);
        }
        rigPageButton.setToggleState (true, juce::dontSendNotification);
        rigPageButton.onClick = [this] { showPage (Page::rig); };
        studioPageButton.onClick = [this] { showPage (Page::studio); };
        drumsPageButton.onClick = [this] { showPage (Page::drums); };
        loopPageButton.onClick = [this] { showPage (Page::loop); };
        livePageButton.onClick = [this] { showPage (Page::live); };
    }

    languageButton.setTooltip ("Language / Idioma");
    languageButton.onClick = [this]
    {
        using poman::i18n::Language;
        poman::i18n::setLanguage (poman::i18n::language() == Language::spanish ? Language::english : Language::spanish);
        applyLanguage();
    };
    addAndMakeVisible (languageButton);
    applyLanguage();

    setResizable (true, true);
    setResizeLimits (760, 687, 1600, 1446);
    getConstrainer()->setFixedAspectRatio (1040.0 / 940.0);
    setSize (1040, 940);
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
    presetBox.addSectionHeading (tr ("FACTORY"));
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == presets.getNumFactoryPresets())
            presetBox.addSectionHeading (tr ("USER"));
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
    saveDialog = std::make_unique<juce::AlertWindow> (tr ("Save preset"), tr ("Name for the new preset:"), juce::MessageBoxIconType::NoIcon, this);
    saveDialog->addTextEditor ("name", processor.getPresetManager().getCurrentName());
    saveDialog->addButton (tr ("Save"), 1, juce::KeyPress (juce::KeyPress::returnKey));
    saveDialog->addButton (tr ("Cancel"), 0, juce::KeyPress (juce::KeyPress::escapeKey));
    saveDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && saveDialog != nullptr)
        {
            const auto name = saveDialog->getTextEditorContents ("name");
            if (! processor.getPresetManager().saveUserPreset (name))
                juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Save preset"),
                                                        tr ("Could not save to ") + processor.getPresetManager().getUserFolder().getFullPathName());
            refreshPresetBox();
        }
        saveDialog = nullptr;
    }), false);
}

void PomanEditor::applyLanguage()
{
    languageButton.setButtonText (poman::i18n::language() == poman::i18n::Language::spanish ? "ES" : "EN");
    poman::i18n::retranslate (*this);
    refreshPresetBox();
    refreshMidiLabels();
    repaint();
    for (auto* child : getChildren())
        child->repaint();
}

void PomanEditor::showPage (Page page)
{
    if (studioView == nullptr)
        return;

    studioView->setVisible (page == Page::studio);
    drumView->setVisible (page == Page::drums);
    loopView->setVisible (page == Page::loop);
    liveView->setVisible (page == Page::live);
    if (page == Page::studio)
        studioView->toFront (false);
    else if (page == Page::drums)
        drumView->toFront (false);
    else if (page == Page::loop)
        loopView->toFront (false);
    else if (page == Page::live)
        liveView->toFront (false);
    else if (tunerDisplay.isVisible())
        tunerDisplay.toFront (false);
}

void PomanEditor::showMidiMenu (const juce::String& target, juce::Component& anchor)
{
    auto& router = processor.getMidiRouter();
    const int cc = router.getCcFor (target);
    const bool learningThis = router.getLearnTarget() == target;

    juce::PopupMenu menu;
    menu.addSectionHeader ("MIDI");
    menu.addItem (1, tr (learningThis ? "Cancel MIDI learn" : "MIDI learn: move a knob, pedal or footswitch"));
    if (cc >= 0)
        menu.addItem (2, tr ("Forget CC ") + juce::String (cc));
    menu.addItem (3, tr ("Forget all MIDI mappings"), router.hasMappings());
    menu.addSeparator();
    menu.addItem (4, tr ("Program Change n loads preset n (always on)"), false);

    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&anchor),
                        [this, target, learningThis] (int result)
                        {
                            auto& r = processor.getMidiRouter();
                            if (result == 1)
                                learningThis ? r.cancelLearn() : r.learn (target);
                            else if (result == 2)
                                r.forget (target);
                            else if (result == 3)
                                r.forgetAll();
                            refreshMidiLabels();
                        });
}

void PomanEditor::refreshMidiLabels()
{
    const auto& router = processor.getMidiRouter();
    const auto learning = router.getLearnTarget();
    const bool blinkOn = (juce::Time::getMillisecondCounter() / 300) % 2 == 0;

    for (auto& k : knobs)
    {
        const int cc = router.getCcFor (k->paramId);
        const auto text = learning == k->paramId ? tr ("LEARN...") : tr (k->baseName) + (cc >= 0 ? " [" + juce::String (cc) + "]" : juce::String());
        if (k->label.getText() != text)
            k->label.setText (text, juce::dontSendNotification);
        k->label.setColour (juce::Label::textColourId, learning == k->paramId ? (blinkOn ? palette::kelp : palette::text)
                                                                              : palette::dimText);
        k->slider.setTooltip (cc >= 0 ? "MIDI CC " + juce::String (cc) + tr (" (right-click for MIDI)") : tr ("Right-click for MIDI learn"));
    }

    const auto markButton = [&] (juce::Button& b, const juce::String& target)
    {
        const int cc = router.getCcFor (target);
        b.setTooltip (learning == target ? tr ("Waiting for a MIDI controller...")
                                         : (cc >= 0 ? "MIDI CC " + juce::String (cc) : tr ("Right-click for MIDI learn")));
        b.setAlpha (learning == target && ! blinkOn ? 0.4f : 1.0f);
    };
    for (auto& s : sections)
        if (s->power != nullptr)
            markButton (*s->power, s->powerParamId);
    markButton (tunerButton, "action:tuner");
    markButton (previousPresetButton, "action:presetPrev");
    markButton (nextPresetButton, "action:presetNext");
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
    // The tuner can also be toggled from a MIDI footswitch.
    if (processor.isTunerActive() != tunerDisplay.isVisible())
    {
        tunerButton.setToggleState (processor.isTunerActive(), juce::dontSendNotification);
        setTunerVisible (processor.isTunerActive());
    }
    refreshMidiLabels();

    inputMeter.pushPeak (processor.takeInputPeak());
    outputMeter.pushPeak (processor.takeOutputPeak());

    if (processor.isTunerActive())
    {
        lastTunerReading = processor.analyseTuner();
        tunerDisplay.setReading (lastTunerReading);
    }

    refreshHarmonizerInfo();
    refreshModLabels();
    refreshFileLabels();
    poman::i18n::retranslate (*this); // texts the code just set (in English) follow the language

    const double now = juce::Time::getMillisecondCounterHiRes() * 0.001;
    const float reduction = processor.getCompressorGainReductionDb();
    compGrLabel.setText (reduction < -0.05f ? "GR " + juce::String (reduction, 1) + " dB" : juce::String(), juce::dontSendNotification);

    // TIME only applies in free mode; synced delays take their time from the tempo.
    delayTimeKnob->slider.setEnabled (delaySyncBox.getSelectedId() <= 1);
    delayTimeKnob->slider.setAlpha (delaySyncBox.getSelectedId() <= 1 ? 1.0f : 0.35f);
    if (tapShownUntil > 0.0 && now > tapShownUntil)
    {
        tapShownUntil = 0.0;
        tapButton.setButtonText ("TAP");
    }
    updateCalibration (now);

    const auto eqSettings = processor.getEqSettings();
    const bool eqEnabled = processor.isEqEnabled();
    if (! eqCurveInitialised || eqSettings != shownEq || eqEnabled != shownEqEnabled)
    {
        eqCurveInitialised = true;
        shownEq = eqSettings;
        shownEqEnabled = eqEnabled;
        eqCurve.setResponse (eqSettings, processor.getSampleRate() > 0.0 ? processor.getSampleRate() : 48000.0, eqEnabled);
    }

    // Session restores and host preset changes can swap the state underneath the editor.
    if (processor.getPresetManager().getCurrentName() != shownPresetName)
    {
        refreshPresetBox();
        refreshFileLabels();
    }
}

//==============================================================================
void EqCurve::setResponse (const poman::GraphicEQ::Settings& s, double sr, bool on)
{
    settings = s;
    sampleRate = sr;
    enabled = on;
    repaint();
}

void EqCurve::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    g.setColour (palette::abyss);
    g.fillRoundedRectangle (bounds, 6.0f);

    constexpr float rangeDb = 15.0f;
    const auto xForHz = [&] (double hz) { return bounds.getX() + bounds.getWidth() * (float) (std::log10 (hz / 20.0) / 3.0); };
    const auto yForDb = [&] (float db) { return bounds.getCentreY() - bounds.getHeight() * 0.5f * juce::jlimit (-1.0f, 1.0f, db / rangeDb); };

    g.setFont (juce::FontOptions (10.0f));
    for (double hz : { 50.0, 100.0, 200.0, 500.0, 1000.0, 2000.0, 5000.0, 10000.0 })
    {
        const float x = xForHz (hz);
        g.setColour (palette::panelEdge.withAlpha (0.7f));
        g.fillRect (x, bounds.getY(), 1.0f, bounds.getHeight());
        g.setColour (palette::dimText.withAlpha (0.8f));
        g.drawText (hz >= 1000.0 ? juce::String ((int) (hz / 1000.0)) + "k" : juce::String ((int) hz),
                    juce::Rectangle<float> (x + 2.0f, bounds.getBottom() - 13.0f, 30.0f, 12.0f), juce::Justification::centredLeft);
    }
    for (float db : { -12.0f, -6.0f, 6.0f, 12.0f })
    {
        g.setColour (palette::panelEdge.withAlpha (0.5f));
        g.fillRect (bounds.getX(), yForDb (db), bounds.getWidth(), 1.0f);
    }
    g.setColour (palette::panelEdge.brighter (0.4f));
    g.fillRect (bounds.getX(), yForDb (0.0f), bounds.getWidth(), 1.0f);

    const auto design = poman::GraphicEQ::designFilters (settings, sampleRate);
    juce::Path curve;
    for (int x = 0; x <= getWidth(); ++x)
    {
        const double hz = 20.0 * std::pow (1000.0, (double) x / juce::jmax (1, getWidth()));
        const float y = yForDb (poman::GraphicEQ::responseDb (design, settings, sampleRate, juce::jmin (hz, sampleRate * 0.49)));
        if (x == 0)
            curve.startNewSubPath (bounds.getX(), y);
        else
            curve.lineTo (bounds.getX() + (float) x, y);
    }

    const auto colour = enabled ? palette::sunlight : palette::dimText;
    juce::Path fill (curve);
    fill.lineTo (bounds.getRight(), yForDb (0.0f));
    fill.lineTo (bounds.getX(), yForDb (0.0f));
    fill.closeSubPath();
    g.setColour (colour.withAlpha (0.15f));
    g.fillPath (fill);
    g.setColour (colour);
    g.strokePath (curve, juce::PathStrokeType (2.0f));

    if (! enabled)
    {
        g.setColour (palette::dimText);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (tr ("BYPASSED"), bounds.reduced (8.0f, 4.0f), juce::Justification::topRight);
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

    if (targetHigh <= 0.0f)
    {
        // Calibration target band, drawn behind the level.
        const auto yFor = [&area] (float db) { return area.getBottom() - area.getHeight() * juce::jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f); };
        g.setColour (palette::kelp.withAlpha (0.25f));
        g.fillRect (area.withTop (yFor (targetHigh)).withBottom (yFor (targetLow)));
    }
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
                               : tr ("Play a string - output muted");
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
    knob->paramId = paramId;
    knob->baseName = name;
    knob->slider.onRightClick = [this, id = paramId, s = &knob->slider] { showMidiMenu (id, *s); };
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
        section->power = std::make_unique<MidiToggle>();
        section->powerParamId = powerParamId;
        section->power->onRightClick = [this, id = powerParamId, b = section->power.get()] { showMidiMenu (id, *b); };
        section->power->setColour (juce::ToggleButton::tickColourId, accent);
        section->powerAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            processor.apvts, powerParamId, *section->power);
        addAndMakeVisible (*section->power);
    }

    sections.push_back (std::move (section));
    return *sections.back();
}

juce::String PomanEditor::intervalName (int steps) const
{
    const bool diatonic = processor.apvts.getRawParameterValue ("harmMode")->load() > 0.5f;
    const auto sign = juce::String (steps > 0 ? "+" : (steps < 0 ? "-" : ""));

    if (! diatonic)
        return sign + juce::String (std::abs (steps)) + " st";

    const auto& scales = poman::harmonizerScales();
    const int scale = juce::jlimit (0, (int) scales.size() - 1, juce::roundToInt (processor.apvts.getRawParameterValue ("harmScale")->load()));
    if (scales[(size_t) scale].pitchClasses.size() != 7)
        return sign + juce::String (std::abs (steps)) + " deg";

    static const char* names[] { "Unison", "2nd", "3rd", "4th", "5th", "6th", "7th", "Oct",
                                 "9th", "10th", "11th", "12th", "13th", "14th", "2 Oct" };
    return sign + names[juce::jlimit (0, 14, std::abs (steps))];
}

void PomanEditor::startCalibration()
{
    processor.takeRawInputPeak(); // discard anything measured before the press
    calibrationPeak = 0.0f;
    calibrationEnds = juce::Time::getMillisecondCounterHiRes() * 0.001 + 5.0;
    calibrationShownUntil = 0.0;
    calButton.setToggleState (true, juce::dontSendNotification);
}

void PomanEditor::updateCalibration (double now)
{
    if (calibrationEnds > 0.0)
    {
        calibrationPeak = juce::jmax (calibrationPeak, processor.takeRawInputPeak());
        if (now < calibrationEnds)
        {
            calButton.setButtonText (juce::String ((int) std::ceil (calibrationEnds - now)));
            return;
        }

        calibrationEnds = 0.0;
        calButton.setToggleState (false, juce::dontSendNotification);
        calibrationShownUntil = now + 3.0;
        if (calibrationPeak < 1.0e-4f) // nothing played (or input muted)
        {
            calButton.setButtonText ("NO SIG");
            return;
        }

        const float gainDb = poman::calibrationGainDb (juce::Decibels::gainToDecibels (calibrationPeak));
        if (auto* input = processor.apvts.getParameter ("input"))
            input->setValueNotifyingHost (input->convertTo0to1 (gainDb));
        calButton.setButtonText ((gainDb >= 0.0f ? "+" : "") + juce::String (gainDb, 1));
    }
    else if (calibrationShownUntil > 0.0 && now > calibrationShownUntil)
    {
        calibrationShownUntil = 0.0;
        calButton.setButtonText ("CAL");
    }
}

void PomanEditor::refreshModLabels()
{
    const int type = juce::roundToInt (processor.apvts.getRawParameterValue ("modType")->load());
    if (type == shownModType)
        return;

    shownModType = type;
    const bool tremolo = type == (int) poman::Modulation::Type::tremolo;
    modVariableKnobs[0]->baseName = tremolo ? "SHAPE" : "MIX";
    modVariableKnobs[1]->baseName = tremolo ? "STEREO" : "FDBK";
}

void PomanEditor::refreshHarmonizerInfo()
{
    const bool diatonic = processor.apvts.getRawParameterValue ("harmMode")->load() > 0.5f;
    const int scale = juce::roundToInt (processor.apvts.getRawParameterValue ("harmScale")->load());
    if (diatonic != shownDiatonic || scale != shownScale)
    {
        shownDiatonic = diatonic;
        shownScale = scale;
        for (auto* k : harmIntervalKnobs)
            k->slider.updateText();
    }

    juce::String text;
    const int note = processor.getHarmonizerNote();
    if (! diatonic)
        text = "V1 " + juce::String (processor.getHarmonizerSemitones (0)) + " st   |   V2 " + juce::String (processor.getHarmonizerSemitones (1)) + " st";
    else if (note < 0)
        text = tr ("Play a note...");
    else
        text = poman::midiNoteName (note) + "   >   " + poman::midiNoteName (note + processor.getHarmonizerSemitones (0))
             + "  +  " + poman::midiNoteName (note + processor.getHarmonizerSemitones (1));

    if (harmInfoLabel.getText() != text)
        harmInfoLabel.setText (text, juce::dontSendNotification);
}

void PomanEditor::chooseFile (PomanProcessor::FileSlot slot)
{
    const bool nam = slot == PomanProcessor::FileSlot::namModel;
    fileChooser = std::make_unique<juce::FileChooser> (tr (nam ? "Load a NAM capture" : "Load a cabinet impulse response"),
                                                       defaultFolder (nam ? "Models" : "IRs"), nam ? "*.nam" : "*.wav;*.aif;*.aiff");
    fileChooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this, slot, nam] (const juce::FileChooser& chooser)
                              {
                                  const auto file = chooser.getResult();
                                  if (! file.existsAsFile())
                                      return;
                                  const auto result = processor.loadFile (slot, file);
                                  if (result.failed())
                                      juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon,
                                                                              nam ? "NAM capture" : "Impulse response", result.getErrorMessage());
                                  else if (nam) // loading a capture means "play it"
                                      ampModelBox.setSelectedId (PomanProcessor::namModelIndex() + 1, juce::sendNotificationSync);
                                  refreshFileLabels();
                              });
}

void PomanEditor::stepIr (int direction)
{
    const auto current = processor.getFile (PomanProcessor::FileSlot::irA);
    const auto folder = current.getParentDirectory().isDirectory() && current != juce::File() ? current.getParentDirectory() : defaultFolder ("IRs");
    auto files = folder.findChildFiles (juce::File::findFiles, false, "*.wav;*.aif;*.aiff");
    if (files.isEmpty())
        return;
    struct ByName { static int compareElements (const juce::File& a, const juce::File& b) { return a.getFileName().compareNatural (b.getFileName()); } } order;
    files.sort (order);

    // From the built-in model, > starts at the first file and < at the last.
    int index = files.indexOf (current);
    index = index < 0 ? (direction > 0 ? 0 : files.size() - 1) : (index + direction + files.size()) % files.size();
    const auto result = processor.loadFile (PomanProcessor::FileSlot::irA, files[index]);
    if (result.failed())
        juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, tr ("Impulse response"), result.getErrorMessage());
    refreshFileLabels();
}

void PomanEditor::refreshFileLabels()
{
    using Slot = PomanProcessor::FileSlot;
    const auto describe = [this] (Slot slot)
    {
        const auto name = processor.getFileName (slot);
        if (name.isEmpty())
            return juce::String();
        return processor.isFileSlotLoaded (slot) ? name : name + tr (" (missing)");
    };

    const auto a = describe (Slot::irA), b = describe (Slot::irB);
    juce::String cabText = tr ("Built-in 4x12 model");
    if (a.isNotEmpty() && b.isNotEmpty())
        cabText = "A: " + a + "   B: " + b + tr ("   (aligned ") + juce::String (processor.getCabAlignmentSamples()) + tr (" smp)");
    else if (a.isNotEmpty() || b.isNotEmpty())
        cabText = (a.isNotEmpty() ? "A: " + a : "B: " + b);
    if (irLabel.getText() != cabText)
        irLabel.setText (cabText, juce::dontSendNotification);
    builtInCabButton.setToggleState (a.isEmpty() && b.isEmpty(), juce::dontSendNotification);

    // The capture's name, and the cost of resampling when the interface is not at the model's rate.
    juce::String namText;
    const bool namSelected = ampModelBox.getSelectedId() == PomanProcessor::namModelIndex() + 1;
    const auto capture = describe (Slot::namModel);
    if (capture.isNotEmpty())
    {
        namText = "NAM: " + capture;
        const double hostRate = processor.getSampleRate(), modelRate = processor.getNamModelRate();
        if (processor.isFileSlotLoaded (Slot::namModel) && hostRate > 0.0 && modelRate > 0.0 && std::abs (hostRate - modelRate) > 1.0)
            namText << "   (" << juce::String (modelRate / 1000.0, 1) << tr (" kHz model, +")
                    << juce::String (1000.0 * processor.getNamResamplingLatency() / hostRate, 1) << tr (" ms: set your interface to ")
                    << juce::String (modelRate / 1000.0, 0) << " kHz)";
    }
    else if (namSelected)
    {
        namText = tr ("No capture loaded: press LOAD NAM (Glass Clean plays meanwhile)");
    }
    if (namLabel.getText() != namText)
        namLabel.setText (namText, juce::dontSendNotification);
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
    g.setFont (juce::FontOptions (30.0f * scale, juce::Font::bold));
    g.drawText ("POMAN", header, juce::Justification::centredLeft);
    g.setColour (palette::biolume);
    g.setFont (juce::FontOptions (11.0f * scale));
    if (studioView == nullptr) // the standalone needs the room for its page buttons
        g.drawText (tr ("GUITAR AMP SUITE"), header.withTrimmedLeft ((int) (128 * scale)), juce::Justification::centredLeft);

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
        g.drawText (tr (s->title), s->bounds.reduced ((int) (14 * scale), 0).withHeight ((int) (34 * scale)).withTrimmedLeft (s->power ? (int) (24 * scale) : 0),
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

    auto io = header.removeFromRight (sc (244)).reduced (0, sc (6));
    calButton.setBounds (io.removeFromLeft (sc (44)).withSizeKeepingCentre (sc (40), sc (26)));
    for (auto [k, meter] : { std::pair { inputKnob, &inputMeter }, std::pair { outputKnob, &outputMeter } })
    {
        auto cell = io.removeFromLeft (sc (100));
        k->label.setBounds (cell.removeFromLeft (sc (48)).withSizeKeepingCentre (sc (48), sc (16)));
        meter->setBounds (cell.removeFromRight (sc (10)).reduced (0, sc (2)));
        k->slider.setBounds (cell.withSizeKeepingCentre (sc (44), sc (52)));
        k->slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    }

    header.removeFromLeft (sc (104)); // the logo
    languageButton.setBounds (header.removeFromLeft (sc (32)).withSizeKeepingCentre (sc (30), sc (22)));
    header.removeFromLeft (sc (studioView != nullptr ? 4 : 134));
    if (studioView != nullptr)
    {
        auto pages = header.removeFromLeft (sc (326)).withSizeKeepingCentre (sc (326), sc (30));
        rigPageButton.setBounds (pages.removeFromLeft (sc (48)).reduced (sc (2), 0));
        studioPageButton.setBounds (pages.removeFromLeft (sc (72)).reduced (sc (2), 0));
        drumsPageButton.setBounds (pages.removeFromLeft (sc (74)).reduced (sc (2), 0));
        loopPageButton.setBounds (pages.removeFromLeft (sc (58)).reduced (sc (2), 0));
        livePageButton.setBounds (pages.reduced (sc (2), 0));
        header.removeFromLeft (sc (6));
    }
    auto presetRow = header.withSizeKeepingCentre (header.getWidth(), sc (30));
    tunerButton.setBounds (presetRow.removeFromRight (sc (78)).reduced (sc (3), 0));
    presetRow.removeFromRight (sc (4));
    savePresetButton.setBounds (presetRow.removeFromRight (sc (80)).reduced (sc (2), 0));
    previousPresetButton.setBounds (presetRow.removeFromLeft (sc (28)).reduced (sc (2), 0));
    nextPresetButton.setBounds (presetRow.removeFromRight (sc (28)).reduced (sc (2), 0));
    presetBox.setBounds (presetRow.reduced (sc (2), 0));

    const int gap = sc (10);
    const int rowHeight = (area.getHeight() - 4 * gap) / 5;
    auto pedals = area.removeFromTop (rowHeight);
    area.removeFromTop (gap);
    auto ampRow = area.removeFromTop (rowHeight);
    area.removeFromTop (gap);
    auto eqRow = area.removeFromTop (rowHeight);
    area.removeFromTop (gap);
    auto harmRow = area.removeFromTop (rowHeight);
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

    placeRow (pedals, { gateSection, compSection, wahSection, octaveSection, driveSection }, { 3, 6, 6, 5, 6 });
    placeRow (ampRow, { ampSection }, { 1 });
    placeRow (eqRow, { eqSection }, { 1 });
    placeRow (harmRow, { harmonizerSection, modSection }, { 3, 2 });
    placeRow (fxRow, { cabSection, delaySection, reverbSection }, { 4, 3, 2 });

    for (auto& s : sections)
    {
        auto inner = s->bounds.reduced (sc (10), sc (8));
        auto titleBar = inner.removeFromTop (sc (26));
        if (s->power != nullptr)
            s->power->setBounds (titleBar.removeFromLeft (sc (20)).withSizeKeepingCentre (sc (16), sc (16)).translated (0, sc (1)));

        if (s.get() == ampSection)
        {
            ampModelBox.setBounds (titleBar.removeFromRight (sc (200)).reduced (sc (2), sc (1)));
            loadNamButton.setBounds (titleBar.removeFromRight (sc (92)).reduced (sc (2), sc (1)));
            titleBar.removeFromLeft (sc (110)); // the section title
            namLabel.setBounds (titleBar.reduced (sc (6), 0));
        }

        if (s.get() == driveSection)
            driveTypeBox.setBounds (titleBar.removeFromRight (sc (130)).reduced (sc (2), sc (1)));

        if (s.get() == compSection)
            compGrLabel.setBounds (titleBar.removeFromRight (sc (90)));

        if (s.get() == delaySection)
        {
            tapButton.setBounds (titleBar.removeFromRight (sc (48)).reduced (sc (2), sc (1)));
            delaySyncBox.setBounds (titleBar.removeFromRight (sc (78)).reduced (sc (2), sc (1)));
        }

        if (s.get() == eqSection)
        {
            auto sliders = inner.removeFromLeft (sc (480));
            const int column = sliders.getWidth() / poman::GraphicEQ::numBands;
            for (int b = 0; b < poman::GraphicEQ::numBands; ++b)
            {
                auto cell = sliders.removeFromLeft (column);
                eqLabels[(size_t) b].setBounds (cell.removeFromBottom (sc (16)));
                eqSliders[(size_t) b].setBounds (cell.reduced (sc (4), sc (2)));
            }
            inner.removeFromLeft (sc (12));
            eqCurve.setBounds (inner.removeFromLeft (inner.getWidth() - sc (260)).reduced (0, sc (2)));
        }

        if (s.get() == modSection)
            modTypeBox.setBounds (titleBar.removeFromRight (sc (120)).reduced (sc (2), sc (1)));

        if (s.get() == wahSection)
        {
            wahModeBox.setBounds (titleBar.removeFromRight (sc (78)).reduced (sc (2), sc (1)));
            wahAutoButton.setBounds (titleBar.removeFromRight (sc (66)).reduced (sc (2), sc (1)));
        }

        if (s.get() == reverbSection)
            revTypeBox.setBounds (titleBar.removeFromRight (sc (70)).reduced (sc (2), sc (1)));

        if (s.get() == harmonizerSection)
        {
            harmScaleBox.setBounds (titleBar.removeFromRight (sc (150)).reduced (sc (2), sc (1)));
            harmKeyBox.setBounds (titleBar.removeFromRight (sc (70)).reduced (sc (2), sc (1)));
            harmModeBox.setBounds (titleBar.removeFromRight (sc (110)).reduced (sc (2), sc (1)));
            harmInfoLabel.setBounds (inner.removeFromRight (sc (150)).reduced (sc (6), sc (22)));
        }

        if (s.get() == cabSection)
        {
            builtInCabButton.setBounds (titleBar.removeFromRight (sc (70)).reduced (sc (2), sc (1)));
            irBButton.setBounds (titleBar.removeFromRight (sc (50)).reduced (sc (2), sc (1)));
            irAButton.setBounds (titleBar.removeFromRight (sc (50)).reduced (sc (2), sc (1)));
            irNextButton.setBounds (titleBar.removeFromRight (sc (30)).reduced (sc (2), sc (1)));
            irPrevButton.setBounds (titleBar.removeFromRight (sc (30)).reduced (sc (2), sc (1)));
            irLabel.setBounds (inner.removeFromTop (sc (16)));
        }

        layoutKnobs (*s, inner.withTrimmedTop (sc (14)));
    }

    tunerDisplay.setBounds (ampSection->bounds);

    if (studioView != nullptr)
    {
        studioView->setBounds (getLocalBounds().withTrimmedTop (sc (64)));
        drumView->setBounds (getLocalBounds().withTrimmedTop (sc (64)));
        loopView->setBounds (getLocalBounds().withTrimmedTop (sc (64)));
        liveView->setBounds (getLocalBounds().withTrimmedTop (sc (64)));
    }
}
