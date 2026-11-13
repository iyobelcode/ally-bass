#include "KeyboardPanel.h"
#include "../PluginProcessor.h"

namespace AllyGUI
{

EngineeredDepthKeyboard::EngineeredDepthKeyboard(juce::MidiKeyboardState& state)
    : juce::MidiKeyboardComponent(state, juce::MidiKeyboardComponent::horizontalKeyboard)
    , keyboardStateRef(state)
{
    clearKeyMappings();
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setOctaveForMiddleC(4); // C2 = MIDI 36, C4 = MIDI 60
    setScrollButtonsVisible(false);
    setAvailableRange(36, 72); // C2..C5 default (22 white keys)
    setBlackNoteWidthProportion(0.60f);
    setBlackNoteLengthProportion(0.62f);

    setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xff08151d));
    setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff5f7887));
    setColour(juce::MidiKeyboardComponent::keySeparatorLineColourId, EngineeredDepthTheme::Colors::black);
    setColour(juce::MidiKeyboardComponent::textLabelColourId, EngineeredDepthTheme::Colors::textSecondary);
    setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, EngineeredDepthTheme::Colors::cyan.withAlpha(0.14f));
    setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, EngineeredDepthTheme::Colors::cyan);
    setColour(juce::MidiKeyboardComponent::shadowColourId, EngineeredDepthTheme::Colors::black);
}

void EngineeredDepthKeyboard::resized()
{
    if (getWidth() > 22)
        setKeyWidth(static_cast<float>(getWidth()) / 22.0f);
    cachedW = 0;
    cachedH = 0;
    juce::MidiKeyboardComponent::resized();
}

void EngineeredDepthKeyboard::updateHoverNote(juce::Point<float> pos) noexcept
{
    currentHoverNote = isMouseOver(false) ? getNoteAndVelocityAtPosition(pos).note : -1;
}

void EngineeredDepthKeyboard::mouseDown(const juce::MouseEvent& e)
{
    updateHoverNote(e.position);
    juce::MidiKeyboardComponent::mouseDown(e);
}

void EngineeredDepthKeyboard::mouseMove(const juce::MouseEvent& e)
{
    updateHoverNote(e.position);
    juce::MidiKeyboardComponent::mouseMove(e);
}

void EngineeredDepthKeyboard::mouseDrag(const juce::MouseEvent& e)
{
    updateHoverNote(e.position);
    juce::MidiKeyboardComponent::mouseDrag(e);
}

void EngineeredDepthKeyboard::mouseEnter(const juce::MouseEvent& e)
{
    updateHoverNote(e.position);
    juce::MidiKeyboardComponent::mouseEnter(e);
}

void EngineeredDepthKeyboard::mouseExit(const juce::MouseEvent& e)
{
    currentHoverNote = -1;
    juce::MidiKeyboardComponent::mouseExit(e);
}

void EngineeredDepthKeyboard::paint(juce::Graphics& g)
{
    const int w = getWidth();
    const int h = getHeight();
    if (w <= 0 || h <= 0)
        return;

    const int rStart = getRangeStart();
    const int rEnd   = getRangeEnd();
    const int span   = rEnd - rStart + 1;
    if (span <= 0 || span > 64)
    {
        juce::MidiKeyboardComponent::paint(g);
        return;
    }

    uint64_t downMask = 0;
    for (int note = rStart; note <= rEnd; ++note)
    {
        if (keyboardStateRef.isNoteOnForChannels(0xffff, note))
            downMask |= (uint64_t { 1 } << static_cast<unsigned>(note - rStart));
    }

    const int hoverNote = isMouseOver(false) ? currentHoverNote : -1;
    const float scale   = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());

    if (!cachedKeyboardImage.isValid()
        || cachedW != w
        || cachedH != h
        || std::abs(cachedScale - scale) > 0.01f
        || cachedRangeStart != rStart
        || cachedRangeEnd != rEnd
        || cachedDownMask != downMask
        || cachedHoverNote != hoverNote)
    {
        const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(w) * scale));
        const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(h) * scale));
        cachedKeyboardImage = juce::Image(juce::Image::ARGB, rw, rh, true);
        juce::Graphics cacheG(cachedKeyboardImage);
        cacheG.addTransform(juce::AffineTransform::scale(scale));
        juce::MidiKeyboardComponent::paint(cacheG);

        cachedW          = w;
        cachedH          = h;
        cachedScale      = scale;
        cachedRangeStart = rStart;
        cachedRangeEnd   = rEnd;
        cachedDownMask   = downMask;
        cachedHoverNote  = hoverNote;
    }

    g.drawImage(cachedKeyboardImage,
                0, 0, w, h,
                0, 0, cachedKeyboardImage.getWidth(), cachedKeyboardImage.getHeight());
}

void EngineeredDepthKeyboard::drawWhiteNote(int midiNoteNumber,
                                            juce::Graphics& g,
                                            juce::Rectangle<float> area,
                                            bool isDown,
                                            bool isOver,
                                            juce::Colour,
                                            juce::Colour)
{
    if (area.getWidth() < 2.0f || area.getHeight() < 8.0f)
        return;

    // 1. Crisp black separator slot between natural keys
    const float gapRight = juce::jlimit(1.2f, 2.0f, area.getWidth() * 0.048f);
    const float gapLeft  = juce::jlimit(0.35f, 0.8f, area.getWidth() * 0.018f);

    g.setColour(juce::Colour(0xff020508));
    g.fillRect(area);

    const auto keyRect = area.withTrimmedLeft(gapLeft).withTrimmedRight(gapRight);
    if (keyRect.getWidth() < 2.0f)
        return;

    const float frontH = juce::jlimit(3.5f, 6.0f, keyRect.getHeight() * 0.105f);
    const auto faceRect = keyRect.withTrimmedBottom(frontH);
    const auto frontRect = keyRect.withTop(keyRect.getBottom() - frontH);

    // 2. Sculpted cool steel-grey / pale slate key face matching Engineered Depth master
    juce::Colour topCol = juce::Colour(0xff7d9bb0);
    juce::Colour midCol = juce::Colour(0xff98b5c7);
    juce::Colour botCol = juce::Colour(0xffb5cddc);

    if (isDown)
    {
        topCol = juce::Colour(0xff0a5063);
        midCol = juce::Colour(0xff0e88a0);
        botCol = juce::Colour(0xff1ae0f7);
    }
    else if (isOver)
    {
        topCol = topCol.interpolatedWith(EngineeredDepthTheme::Colors::cyanBright, 0.12f);
        midCol = midCol.interpolatedWith(EngineeredDepthTheme::Colors::cyanBright, 0.15f);
        botCol = botCol.interpolatedWith(EngineeredDepthTheme::Colors::cyanBright, 0.18f);
    }

    juce::ColourGradient faceGrad(topCol, faceRect.getCentreX(), faceRect.getY(),
                                  botCol, faceRect.getCentreX(), faceRect.getBottom(), false);
    faceGrad.addColour(0.30, midCol);
    faceGrad.addColour(0.86, isDown ? juce::Colour(0xff3eeeff) : juce::Colour(0xffc2d8e4));
    g.setGradientFill(faceGrad);
    g.fillRect(faceRect);

    // Subtle dark overhang shadow directly under top chassis bezel
    const float pocketShadowH = juce::jlimit(2.5f, 5.2f, faceRect.getHeight() * 0.088f);
    juce::ColourGradient topPocketGrad(juce::Colour(0xd002060a), faceRect.getCentreX(), faceRect.getY(),
                                       juce::Colours::transparentBlack, faceRect.getCentreX(), faceRect.getY() + pocketShadowH,
                                       false);
    g.setGradientFill(topPocketGrad);
    g.fillRect(faceRect.withHeight(pocketShadowH));

    // Crisp tight left & right dark key side walls (architectural seam, no wide dark smear)
    const float sideBevelW = juce::jlimit(0.9f, 1.35f, keyRect.getWidth() * 0.042f);
    g.setColour(juce::Colour(isDown ? 0x9004222c : 0xc0081620));
    g.fillRect(faceRect.withWidth(sideBevelW));
    g.setColour(juce::Colour(isDown ? 0xa0041c24 : 0xd8050e15));
    g.fillRect(faceRect.withLeft(faceRect.getRight() - sideBevelW * 1.15f));

    // Fine bright bevel edge catch right inside the left side groove + horizontal key ridge
    g.setColour((isDown ? EngineeredDepthTheme::Colors::cyanBright : juce::Colour(0xffd5e7f0))
                    .withAlpha(isDown ? 0.70f : 0.40f));
    g.drawLine(faceRect.getX() + 0.9f, faceRect.getY() + 3.0f,
               faceRect.getX() + 0.9f, faceRect.getBottom() - 1.0f, 0.85f);

    g.setColour((isDown ? EngineeredDepthTheme::Colors::cyanBright : juce::Colour(0xffe2eef5))
                    .withAlpha(isDown ? 0.90f : 0.65f));
    g.fillRect(faceRect.getX(), faceRect.getBottom() - 1.15f, faceRect.getWidth(), 1.15f);

    // 3. Dark 3D front drop face at bottom of white key
    {
        juce::ColourGradient frontGrad(
            isDown ? juce::Colour(0xff0095a8) : juce::Colour(0xff223a47),
            frontRect.getCentreX(), frontRect.getY(),
            isDown ? juce::Colour(0xff004452) : juce::Colour(0xff08131a),
            frontRect.getCentreX(), frontRect.getBottom(),
            false);
        g.setGradientFill(frontGrad);
        g.fillRect(frontRect);

        // Side shadows down the front drop block
        g.setColour(juce::Colour(0x90010406));
        g.fillRect(frontRect.withWidth(sideBevelW));
        g.fillRect(frontRect.withLeft(frontRect.getRight() - sideBevelW));
    }

    // 4. Active / hover luminous cyan feedback
    if (isDown)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.24f));
        g.fillRect(keyRect.reduced(0.5f));
        g.setColour(EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.92f));
        g.drawRect(keyRect.reduced(0.4f), 1.2f);
    }
    else if (isOver)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.32f));
        g.drawRect(keyRect.reduced(0.4f), 0.9f);
    }

    // 5. Crisp octave markers on every C (C2, C3, C4, C5) with soft shadow for legibility
    if (midiNoteNumber % 12 == 0)
    {
        const auto noteText = juce::MidiMessage::getMidiNoteName(midiNoteNumber, true, true, 4);
        const float fontSize = juce::jlimit(8.0f, 11.0f, keyRect.getWidth() * 0.42f);
        g.setFont(EngineeredDepthTheme::makeDataFont(fontSize, true));

        const auto labelArea = faceRect.reduced(2.2f, 0.0f)
                                       .withTop(faceRect.getBottom() - 14.0f)
                                       .withBottom(faceRect.getBottom() - 1.5f);

        // Crisp dark slate embossed lettering
        g.setColour(isDown ? juce::Colour(0xff02141c) : juce::Colour(0xff122633).withAlpha(0.90f));
        g.drawText(noteText, labelArea.translated(0.0f, 0.2f), juce::Justification::centredLeft, false);
    }
}

void EngineeredDepthKeyboard::drawBlackNote(int,
                                            juce::Graphics& g,
                                            juce::Rectangle<float> area,
                                            bool isDown,
                                            bool isOver,
                                            juce::Colour)
{
    if (area.getWidth() < 2.0f || area.getHeight() < 6.0f)
        return;

    // 1. Deep black recessed well & drop shadow underneath raised titanium black keycap
    const float insetX = juce::jlimit(1.0f, 1.85f, area.getWidth() * 0.10f);
    const auto wellRect = area.expanded(insetX * 0.45f, 0.0f);
    g.setColour(juce::Colour(0xf0010407));
    g.fillRoundedRectangle(wellRect, 2.0f);

    // Keycap geometry shifts down slightly when pressed
    const float pressShift = isDown ? 1.2f : 0.0f;
    const float botDropShadow = juce::jlimit(2.0f, 3.8f, area.getHeight() * 0.085f);
    const auto capOuter = area.reduced(insetX, 0.0f)
                              .withTrimmedTop(pressShift)
                              .withTrimmedBottom(botDropShadow - pressShift * 0.45f);
    const float capRadius = juce::jlimit(1.2f, 2.4f, capOuter.getWidth() * 0.13f);

    const float frontH = juce::jlimit(3.0f, 5.2f, capOuter.getHeight() * 0.145f);
    const auto topPlane = capOuter.withTrimmedBottom(frontH * 0.72f);

    // 2. Sculpted tactile matte obsidian sharp/flat keycap
    juce::Colour topCol = juce::Colour(0xff12232c);
    juce::Colour midCol = juce::Colour(0xff182e3a);
    juce::Colour botCol = juce::Colour(0xff223d4c);

    if (isDown)
    {
        topCol = juce::Colour(0xff004c59);
        midCol = EngineeredDepthTheme::Colors::cyan.darker(0.25f);
        botCol = juce::Colour(0xff009db3);
    }
    else if (isOver)
    {
        topCol = topCol.interpolatedWith(EngineeredDepthTheme::Colors::cyanBright, 0.16f);
        midCol = midCol.interpolatedWith(EngineeredDepthTheme::Colors::cyanBright, 0.20f);
        botCol = botCol.interpolatedWith(EngineeredDepthTheme::Colors::cyanBright, 0.22f);
    }

    // Deep dark side/front keycap block base
    g.setColour(isDown ? juce::Colour(0xff00343e) : juce::Colour(0xff061016));
    g.fillRoundedRectangle(capOuter, capRadius);

    juce::ColourGradient capGrad(topCol, topPlane.getCentreX(), topPlane.getY(),
                                 botCol, topPlane.getCentreX(), topPlane.getBottom(), false);
    capGrad.addColour(0.55, midCol);
    g.setGradientFill(capGrad);
    g.fillRoundedRectangle(topPlane, capRadius);

    // Subtle dark vertical side walls along the matte keycap
    const float wallW = juce::jlimit(1.0f, 1.8f, topPlane.getWidth() * 0.10f);
    g.setColour(juce::Colour(isDown ? 0x6500343e : 0xb503080c));
    g.fillRect(topPlane.getX(), topPlane.getY() + 1.0f, wallW, topPlane.getHeight() - 2.0f);
    g.fillRect(topPlane.getRight() - wallW, topPlane.getY() + 1.0f, wallW, topPlane.getHeight() - 2.0f);

    // Crisp horizontal top specular rim + sharp lower bevel ridge separating top plane from front block
    g.setColour((isDown ? EngineeredDepthTheme::Colors::cyanBright : juce::Colour(0xff5a869e))
                    .withAlpha(isDown ? 0.90f : 0.45f));
    g.drawLine(topPlane.getX() + 1.4f, topPlane.getBottom() - 0.8f,
               topPlane.getRight() - 1.4f, topPlane.getBottom() - 0.8f, 0.95f);

    g.setColour((isDown ? EngineeredDepthTheme::Colors::cyanBright : juce::Colour(0xff86b5ce))
                    .withAlpha(isDown ? 0.85f : 0.38f));
    g.drawLine(topPlane.getX() + 1.4f, topPlane.getY() + 0.8f,
               topPlane.getRight() - 1.4f, topPlane.getY() + 0.8f, 0.85f);

    if (isDown)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.95f));
        g.drawRoundedRectangle(capOuter, capRadius, 1.25f);
    }
    else
    {
        g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.75f));
        g.drawRoundedRectangle(capOuter, capRadius, 0.9f);
    }
}

// ==============================================================================
KeyboardPanel::OctaveStepButton::OctaveStepButton(bool isIncrementButton)
    : juce::Button(isIncrementButton ? "octaveUp" : "octaveDown"),
      increment(isIncrementButton)
{
    setWantsKeyboardFocus(false);
    setMouseClickGrabsKeyboardFocus(false);
    setDescription(isIncrementButton ? "Increase Octave" : "Decrease Octave");
}

void KeyboardPanel::OctaveStepButton::paintButton(juce::Graphics& g,
                                                  bool shouldDrawButtonAsHighlighted,
                                                  bool shouldDrawButtonAsDown)
{
    const auto b = getLocalBounds().toFloat().reduced(0.8f);
    if (b.getWidth() < 8.0f || b.getHeight() < 8.0f)
        return;

    const bool enabled = isEnabled();
    const float r = juce::jlimit(4.0f, 6.5f, b.getHeight() * 0.22f);

    const auto topCol = !enabled
        ? EngineeredDepthTheme::Colors::bg
        : (shouldDrawButtonAsDown ? EngineeredDepthTheme::Colors::surface2
                                  : (shouldDrawButtonAsHighlighted ? EngineeredDepthTheme::Colors::surfaceRaised
                                                                   : EngineeredDepthTheme::Colors::surface1));
    const auto botCol = !enabled
        ? EngineeredDepthTheme::Colors::black
        : EngineeredDepthTheme::Colors::bg.darker(0.25f);

    juce::ColourGradient grad(topCol, b.getCentreX(), b.getY(),
                              botCol, b.getCentreX(), b.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(b, r);

    const auto borderCol = !enabled
        ? EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.32f)
        : (shouldDrawButtonAsDown || shouldDrawButtonAsHighlighted
               ? EngineeredDepthTheme::Colors::cyanFocus.withAlpha(0.85f)
               : EngineeredDepthTheme::Colors::stroke.withAlpha(0.75f));
    g.setColour(borderCol);
    g.drawRoundedRectangle(b, r, 1.0f);

    if (enabled)
    {
        g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(shouldDrawButtonAsHighlighted ? 0.34f : 0.22f));
        g.drawLine(b.getX() + r, b.getY() + 0.9f, b.getRight() - r, b.getY() + 0.9f, 0.9f);
    }

    // Symmetrical vector - / + glyph centered cleanly
    const float cx = b.getCentreX();
    const float cy = b.getCentreY();
    const float arm = juce::jlimit(3.2f, 5.2f, juce::jmin(b.getWidth(), b.getHeight()) * 0.19f);
    const float strokeW = 1.45f;

    const auto glyphCol = !enabled
        ? EngineeredDepthTheme::Colors::disabled
        : (shouldDrawButtonAsDown ? EngineeredDepthTheme::Colors::cyanBright
                                  : EngineeredDepthTheme::Colors::textPrimary);
    g.setColour(glyphCol);
    g.drawLine(cx - arm, cy, cx + arm, cy, strokeW);
    if (increment)
        g.drawLine(cx, cy - arm, cx, cy + arm, strokeW);
}

// ==============================================================================
KeyboardPanel::KeyboardPanel(juce::MidiKeyboardState& keyboardState,
                             AllyBassAudioProcessor* processorPtr)
    : processor(processorPtr),
      keyboard(keyboardState)
{
    setInterceptsMouseClicks(false, true);
    setWantsKeyboardFocus(false);

    btnDown.setName("octaveDown");
    btnUp.setName("octaveUp");
    btnDown.onClick = [this]()
    {
        if (onOctaveStepRequested)
            onOctaveStepRequested(-1);
    };
    btnUp.onClick = [this]()
    {
        if (onOctaveStepRequested)
            onOctaveStepRequested(+1);
    };

    addAndMakeVisible(btnDown);
    addAndMakeVisible(btnUp);
    addAndMakeVisible(keyboard);

    setOctaveDisplay(2, 0, 5);

    if (processor != nullptr)
    {
        processor->syncExternalMidiToKeyboardState(lastExternalMidiVersion, appliedExternalMidiMask);
        startTimerHz(30);
    }
}

KeyboardPanel::~KeyboardPanel()
{
    stopTimer();
}

void KeyboardPanel::timerCallback()
{
    if (processor != nullptr)
        processor->syncExternalMidiToKeyboardState(lastExternalMidiVersion, appliedExternalMidiMask);
}

void KeyboardPanel::setOctaveDisplay(int octaveIndex, int minOctave, int maxOctave)
{
    displayedOctave = juce::jlimit(minOctave, maxOctave, octaveIndex);
    const int lowNote = (displayedOctave + 1) * 12;
    const int highNote = juce::jmin(120, lowNote + 36);
    keyboard.setAvailableRange(lowNote, highNote);
    if (keyboard.getWidth() > 22)
        keyboard.setKeyWidth(static_cast<float>(keyboard.getWidth()) / 22.0f);

    btnDown.setEnabled(displayedOctave > minOctave);
    btnUp.setEnabled(displayedOctave < maxOctave);
    repaint();
}

juce::Button& KeyboardPanel::getOctaveDownButton() noexcept { return btnDown; }
juce::Button& KeyboardPanel::getOctaveUpButton() noexcept { return btnUp; }
juce::String KeyboardPanel::getDisplayedOctaveText() const { return "OCT: C" + juce::String(displayedOctave); }

void KeyboardPanel::resized()
{
    const auto local = getLocalBounds().toFloat();
    if (local.getWidth() < 200.0f || local.getHeight() < 34.0f)
    {
        chassisBounds = {};
        octaveBarBounds = {};
        keyboardBedBounds = {};
        octaveReadoutBounds = {};
        return;
    }

    chassisBounds = local.reduced(1.0f, 1.5f);

    const float sidePad = juce::jlimit(8.0f, 14.0f, chassisBounds.getWidth() * 0.015f);
    const float vPad    = juce::jlimit(4.0f, 7.0f, chassisBounds.getHeight() * 0.095f);

    const float octBarW = juce::jlimit(112.0f, 146.0f, chassisBounds.getWidth() * 0.158f);
    const float btnSize = juce::jlimit(22.0f, 30.0f, chassisBounds.getHeight() * 0.44f);
    const int barX = juce::roundToInt(chassisBounds.getX() + sidePad);
    const int barH = juce::roundToInt(btnSize);
    const int barY = juce::roundToInt(chassisBounds.getCentreY() - btnSize * 0.5f);
    octaveBarBounds = juce::Rectangle<int>(barX, barY, juce::roundToInt(octBarW), barH);

    const int bSizeInt = juce::roundToInt(btnSize);
    btnDown.setBounds(octaveBarBounds.getX(), octaveBarBounds.getY(), bSizeInt, barH);
    btnUp.setBounds(octaveBarBounds.getRight() - bSizeInt, octaveBarBounds.getY(), bSizeInt, barH);
    octaveReadoutBounds = juce::Rectangle<float>(static_cast<float>(btnDown.getRight() + 2),
                                                 static_cast<float>(octaveBarBounds.getY()),
                                                 static_cast<float>(btnUp.getX() - btnDown.getRight() - 4),
                                                 static_cast<float>(barH));

    dividerX = static_cast<float>(octaveBarBounds.getRight()) + juce::jlimit(8.0f, 14.0f, chassisBounds.getWidth() * 0.013f);

    const int kbX = juce::roundToInt(dividerX + 8.0f);
    const int kbY = juce::roundToInt(chassisBounds.getY() + vPad);
    const int kbW = juce::jmax(100, juce::roundToInt(chassisBounds.getRight() - sidePad) - kbX);
    const int kbH = juce::jmax(28, juce::roundToInt(chassisBounds.getHeight() - vPad * 2.0f));
    keyboardBedBounds = juce::Rectangle<int>(kbX, kbY, kbW, kbH);

    keyboard.setBounds(keyboardBedBounds);
    if (keyboardBedBounds.getWidth() > 22)
        keyboard.setKeyWidth(static_cast<float>(keyboardBedBounds.getWidth()) / 22.0f);
    cachedChassisW = 0;
    cachedChassisH = 0;
}

void KeyboardPanel::renderChassisToCache(juce::Graphics& g) const
{
    const float chamfer = juce::jlimit(7.0f, 13.0f, chassisBounds.getHeight() * 0.16f);
    const auto outerPath = EngineeredDepthTheme::buildChamferedRectPath(chassisBounds, chamfer, chamfer);

    // 1. Machined dark outer shadow bevel & marine interior fill
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.88f));
    g.fillPath(outerPath);

    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(outerPath);

        juce::ColourGradient panelGrad(
            EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::bg, 0.36f),
            chassisBounds.getCentreX(), chassisBounds.getY(),
            EngineeredDepthTheme::Colors::bg.darker(0.32f),
            chassisBounds.getCentreX(), chassisBounds.getBottom(),
            false);
        g.setGradientFill(panelGrad);
        g.fillRect(chassisBounds);

        // Top subtle brushed metallic catch gradient
        juce::ColourGradient topWash(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.06f),
                                     chassisBounds.getCentreX(), chassisBounds.getY(),
                                     juce::Colours::transparentBlack,
                                     chassisBounds.getCentreX(), chassisBounds.getY() + chassisBounds.getHeight() * 0.35f,
                                     false);
        g.setGradientFill(topWash);
        g.fillRect(chassisBounds);
    }

    // 2. Outer contour rim & subtle top bevel highlight
    g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.86f));
    g.strokePath(outerPath, juce::PathStrokeType(1.15f));

    g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.38f));
    g.drawLine(chassisBounds.getX() + chamfer, chassisBounds.getY() + 1.0f,
               chassisBounds.getRight() - chamfer, chassisBounds.getY() + 1.0f, 1.1f);

    // 4. Vertical bay separator between Octave Control and Piano Keybed
    if (dividerX > chassisBounds.getX())
    {
        EngineeredDepthTheme::drawContourLine(
            g,
            { dividerX, chassisBounds.getY() + 7.0f },
            { dividerX, chassisBounds.getBottom() - 7.0f },
            1.0f,
            0.48f);
    }

    // 5. Recessed keybed surround bevel around piano keys
    if (!keyboardBedBounds.isEmpty())
    {
        const auto bed = keyboardBedBounds.toFloat().expanded(1.2f, 1.0f);
        g.setColour(EngineeredDepthTheme::Colors::black);
        g.drawRoundedRectangle(bed, 3.0f, 1.2f);
        g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.55f));
        g.drawRoundedRectangle(bed.expanded(0.8f), 3.5f, 0.85f);
    }
}

void KeyboardPanel::paint(juce::Graphics& g)
{
    if (chassisBounds.isEmpty())
        return;

    const int w = getWidth();
    const int h = getHeight();
    const float scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (!cachedChassisImage.isValid()
        || cachedChassisW != w
        || cachedChassisH != h
        || std::abs(cachedChassisScale - scale) > 0.01f)
    {
        const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(w) * scale));
        const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(h) * scale));
        cachedChassisImage = juce::Image(juce::Image::ARGB, rw, rh, true);
        juce::Graphics cacheG(cachedChassisImage);
        cacheG.addTransform(juce::AffineTransform::scale(scale));
        renderChassisToCache(cacheG);
        cachedChassisW = w;
        cachedChassisH = h;
        cachedChassisScale = scale;
    }

    g.drawImage(cachedChassisImage,
                0, 0, w, h,
                0, 0, cachedChassisImage.getWidth(), cachedChassisImage.getHeight());

    // 3. Compact Octave Readout (OCT: C2) centered cleanly between - and + buttons
    if (!octaveReadoutBounds.isEmpty())
    {
        const float octFontSize = juce::jlimit(9.2f, 11.5f, octaveReadoutBounds.getHeight() * 0.41f);
        g.setFont(EngineeredDepthTheme::makeDataFont(octFontSize, true));
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.drawText(getDisplayedOctaveText(), octaveReadoutBounds, juce::Justification::centred, false);
    }
}

} // namespace AllyGUI
