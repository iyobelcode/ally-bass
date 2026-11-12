#include "KeyboardPanel.h"

namespace AllyGUI
{

EngineeredDepthKeyboard::EngineeredDepthKeyboard(juce::MidiKeyboardState& state)
    : juce::MidiKeyboardComponent(state, juce::MidiKeyboardComponent::horizontalKeyboard)
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
    juce::MidiKeyboardComponent::resized();
}

void EngineeredDepthKeyboard::mouseDown(const juce::MouseEvent& e)
{
    juce::MidiKeyboardComponent::mouseDown(e);
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

    // Leave crisp 1.2px recessed gap between natural keys
    const auto keyRect = area.withTrimmedRight(1.2f);

    // Base dark marine slate key surface matching Engineered Depth master (clear key-to-key separation)
    juce::Colour topCol = juce::Colour(0xff112836);
    juce::Colour midCol = juce::Colour(0xff0b1c26);
    juce::Colour botCol = juce::Colour(0xff06121a);

    if (isDown)
    {
        topCol = juce::Colour(0xff0b3d4a);
        midCol = juce::Colour(0xff074c5b);
        botCol = juce::Colour(0xff008899);
    }
    else if (isOver)
    {
        topCol = topCol.interpolatedWith(EngineeredDepthTheme::Colors::surface2, 0.45f);
        midCol = midCol.interpolatedWith(EngineeredDepthTheme::Colors::cyan, 0.08f);
    }

    juce::ColourGradient grad(topCol, keyRect.getCentreX(), keyRect.getY(),
                              botCol, keyRect.getCentreX(), keyRect.getBottom(), false);
    grad.addColour(0.55, midCol);
    g.setGradientFill(grad);
    g.fillRect(keyRect);

    // Subtle top shadow inside keybed pocket + vertical key sheen
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(isDown ? 0.30f : 0.45f));
    g.fillRect(keyRect.withHeight(juce::jmin(4.0f, keyRect.getHeight() * 0.08f)));

    // Lower front keycap bevel edge
    const float lipH = juce::jlimit(2.0f, 4.0f, keyRect.getHeight() * 0.065f);
    const auto lipRect = keyRect.withTop(keyRect.getBottom() - lipH);
    if (isDown)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.92f));
        g.fillRect(lipRect);
        g.setColour(EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.75f));
        g.fillRect(lipRect.withHeight(1.0f));
    }
    else
    {
        g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.45f));
        g.fillRect(lipRect);
    }

    // Active-note crisp subtle cyan illumination outline & bloom
    if (isDown)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.22f));
        g.fillRect(keyRect.reduced(0.5f));
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.88f));
        g.drawRect(keyRect.reduced(0.5f), 1.15f);
    }
    else if (isOver)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.32f));
        g.drawRect(keyRect.reduced(0.5f), 0.9f);
    }

    // Right dark channel slot line & faint left edge bevel catch for clear natural-key separation
    g.setColour(EngineeredDepthTheme::Colors::black);
    g.fillRect(area.getRight() - 1.25f, area.getY(), 1.25f, area.getHeight());
    g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(isDown ? 0.35f : 0.22f));
    g.drawVerticalLine(juce::roundToInt(keyRect.getX()), keyRect.getY() + 1.0f, keyRect.getBottom() - lipH);

    // Clear octave markers on every C (C2, C3, C4, C5) matching master layout
    if (midiNoteNumber % 12 == 0)
    {
        const auto noteText = juce::MidiMessage::getMidiNoteName(midiNoteNumber, true, true, 4);
        const float fontSize = juce::jlimit(8.0f, 11.0f, keyRect.getWidth() * 0.42f);
        g.setFont(EngineeredDepthTheme::makeDataFont(fontSize, isDown));
        g.setColour(isDown ? EngineeredDepthTheme::Colors::cyanBright
                           : EngineeredDepthTheme::Colors::textSecondary.withAlpha(0.82f));

        const auto labelArea = keyRect.reduced(2.0f, 0.0f)
                                      .withTop(keyRect.getBottom() - lipH - 14.0f)
                                      .withBottom(keyRect.getBottom() - lipH - 1.0f);
        g.drawText(noteText, labelArea, juce::Justification::centredLeft, false);
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

    // Dark recessed well behind raised brushed-titanium sharp/flat keycap
    const float inset = juce::jlimit(0.8f, 1.8f, area.getWidth() * 0.09f);
    const auto wellRect = area.expanded(inset * 0.6f, 0.0f).withTrimmedBottom(0.5f);
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.95f));
    g.fillRoundedRectangle(wellRect, 2.0f);

    // Keycap geometry shifts down 1px cleanly when pressed
    const float pressShift = isDown ? 1.0f : 0.0f;
    const auto capRect = area.reduced(inset, 0.0f)
                             .withTrimmedTop(pressShift)
                             .withTrimmedBottom(juce::jlimit(1.5f, 3.0f, area.getHeight() * 0.06f) - pressShift * 0.5f);
    const float capRadius = juce::jlimit(1.4f, 2.6f, capRect.getWidth() * 0.14f);

    juce::Colour topCol = juce::Colour(0xff8ca4b3);
    juce::Colour midCol = juce::Colour(0xff5a7485);
    juce::Colour botCol = juce::Colour(0xff2c424f);

    if (isDown)
    {
        topCol = juce::Colour(0xff7ff7ff);
        midCol = EngineeredDepthTheme::Colors::cyan;
        botCol = juce::Colour(0xff007a8a);
    }
    else if (isOver)
    {
        topCol = topCol.interpolatedWith(EngineeredDepthTheme::Colors::cyanBright, 0.25f);
        midCol = midCol.interpolatedWith(EngineeredDepthTheme::Colors::cyan, 0.18f);
    }

    juce::ColourGradient capGrad(topCol, capRect.getCentreX(), capRect.getY(),
                                 botCol, capRect.getCentreX(), capRect.getBottom(), false);
    capGrad.addColour(0.68, midCol);
    g.setGradientFill(capGrad);
    g.fillRoundedRectangle(capRect, capRadius);

    // Front vertical 3D bevel lip at bottom of black keycap
    const float frontH = juce::jlimit(2.5f, 4.5f, capRect.getHeight() * 0.12f);
    const auto frontLip = capRect.withTop(capRect.getBottom() - frontH);
    g.setColour(isDown ? juce::Colour(0xff005866) : juce::Colour(0xff192b36));
    g.fillRoundedRectangle(frontLip, capRadius * 0.7f);

    // Top metallic rim specular catch
    g.setColour(isDown ? EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.85f)
                       : EngineeredDepthTheme::Colors::textPrimary.withAlpha(0.32f));
    g.drawLine(capRect.getX() + 1.2f, capRect.getY() + 0.8f,
               capRect.getRight() - 1.2f, capRect.getY() + 0.8f, 0.9f);

    if (isDown)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.92f));
        g.drawRoundedRectangle(capRect, capRadius, 1.1f);
    }
    else
    {
        g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.55f));
        g.drawRoundedRectangle(capRect, capRadius, 0.85f);
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
KeyboardPanel::KeyboardPanel(juce::MidiKeyboardState& keyboardState)
    : keyboard(keyboardState)
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
}

void KeyboardPanel::paint(juce::Graphics& g)
{
    if (chassisBounds.isEmpty())
        return;

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

    g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.24f));
    g.drawLine(chassisBounds.getX() + chamfer, chassisBounds.getY() + 1.0f,
               chassisBounds.getRight() - chamfer, chassisBounds.getY() + 1.0f, 1.0f);

    // 3. Compact Octave Readout (OCT: C2) centered cleanly between - and + buttons
    if (!octaveReadoutBounds.isEmpty())
    {
        const float octFontSize = juce::jlimit(9.2f, 11.5f, octaveReadoutBounds.getHeight() * 0.41f);
        g.setFont(EngineeredDepthTheme::makeDataFont(octFontSize, true));
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.drawText(getDisplayedOctaveText(), octaveReadoutBounds, juce::Justification::centred, false);
    }

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

} // namespace AllyGUI
