#include "BrutalistLookAndFeel.h"

namespace AllyGUI
{

const juce::Colour BrutalistLookAndFeel::concreteDark    = juce::Colour(0xff121316);
const juce::Colour BrutalistLookAndFeel::concretePanel   = juce::Colour(0xff1a1c20);
const juce::Colour BrutalistLookAndFeel::concreteBorder  = juce::Colour(0xff2d3139);
const juce::Colour BrutalistLookAndFeel::aluminiumLight  = juce::Colour(0xffd8dce3);
const juce::Colour BrutalistLookAndFeel::aluminiumDark   = juce::Colour(0xff383c44);
const juce::Colour BrutalistLookAndFeel::weerwaterTeal   = juce::Colour(0xff00e5ff);
const juce::Colour BrutalistLookAndFeel::allyRed         = juce::Colour(0xffff3b30);
const juce::Colour BrutalistLookAndFeel::industrialText  = juce::Colour(0xffe0e0e0);
const juce::Colour BrutalistLookAndFeel::mutedText       = juce::Colour(0xff7a808c);

BrutalistLookAndFeel::BrutalistLookAndFeel()
{
    setColour(juce::ResizableWindow::backgroundColourId, concreteDark);
    setColour(juce::ComboBox::backgroundColourId, concretePanel);
    setColour(juce::ComboBox::textColourId, industrialText);
    setColour(juce::ComboBox::outlineColourId, concreteBorder);
    setColour(juce::PopupMenu::backgroundColourId, concretePanel);
    setColour(juce::PopupMenu::textColourId, industrialText);
    setColour(juce::PopupMenu::highlightedBackgroundColourId, concreteBorder.brighter(0.2f));
    setColour(juce::PopupMenu::highlightedTextColourId, weerwaterTeal);
}

void BrutalistLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                            float sliderPosProportional, float rotaryStartAngle,
                                            float rotaryEndAngle, juce::Slider& slider)
{
    auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight()) - 8.0f;
    if (diameter < 10.0f)
        return;

    auto centre = bounds.getCentre();
    float radius = diameter * 0.5f;

    // Check if slider is configured for Red/Drive or Teal
    bool isRedTheme = slider.getName().containsIgnoreCase("dirt") || slider.getName().containsIgnoreCase("punch");
    juce::Colour activeGlowColour = isRedTheme ? allyRed : weerwaterTeal;

    // 1. Dark circular well behind the knob
    g.setColour(juce::Colour(0xff0d0e10));
    g.fillEllipse(centre.x - radius, centre.y - radius, diameter, diameter);

    // 2. Track ring groove
    float trackRadius = radius - 3.0f;
    juce::Path trackPath;
    trackPath.addCentredArc(centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(concreteBorder);
    g.strokePath(trackPath, juce::PathStrokeType(3.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // 3. Glowing Active Arc
    float currentAngle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    if (currentAngle > rotaryStartAngle + 0.02f)
    {
        juce::Path activeArc;
        activeArc.addCentredArc(centre.x, centre.y, trackRadius, trackRadius, 0.0f, rotaryStartAngle, currentAngle, true);

        // Soft outer neon glow
        g.setColour(activeGlowColour.withAlpha(0.25f));
        g.strokePath(activeArc, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Core bright neon line
        g.setColour(activeGlowColour);
        g.strokePath(activeArc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 4. Milled Aluminium Knob Body
    float knobRadius = radius - 8.0f;
    auto knobArea = juce::Rectangle<float>(centre.x - knobRadius, centre.y - knobRadius, knobRadius * 2.0f, knobRadius * 2.0f);

    // Drop shadow under knob
    g.setColour(juce::Colours::black.withAlpha(0.6f));
    g.fillEllipse(knobArea.translated(0.0f, 2.5f));

    // Outer knurled rim
    juce::ColourGradient knurledGrad(aluminiumLight.darker(0.3f), centre.x - knobRadius, centre.y - knobRadius,
                                     aluminiumDark.darker(0.5f), centre.x + knobRadius, centre.y + knobRadius, false);
    g.setGradientFill(knurledGrad);
    g.fillEllipse(knobArea);

    // Bevel ridge ring
    float innerRadius = knobRadius - 2.5f;
    auto innerArea = juce::Rectangle<float>(centre.x - innerRadius, centre.y - innerRadius, innerRadius * 2.0f, innerRadius * 2.0f);
    
    // Milled top face with radial/linear brushed metallic sheen
    juce::ColourGradient faceGrad(aluminiumLight, centre.x - innerRadius * 0.5f, centre.y - innerRadius * 0.7f,
                                  aluminiumDark, centre.x + innerRadius * 0.5f, centre.y + innerRadius * 0.7f, false);
    g.setGradientFill(faceGrad);
    g.fillEllipse(innerArea);

    // Concentric machined lathe lines
    g.setColour(juce::Colours::black.withAlpha(0.12f));
    g.drawEllipse(centre.x - innerRadius * 0.7f, centre.y - innerRadius * 0.7f, innerRadius * 1.4f, innerRadius * 1.4f, 1.0f);
    g.drawEllipse(centre.x - innerRadius * 0.4f, centre.y - innerRadius * 0.4f, innerRadius * 0.8f, innerRadius * 0.8f, 1.0f);

    // 5. Machined pointer notch
    juce::Path pointer;
    float pointerLength = innerRadius * 0.75f;
    float pointerThickness = 3.0f;

    pointer.addRoundedRectangle(-pointerThickness * 0.5f, -innerRadius + 2.0f, pointerThickness, pointerLength, 1.5f);
    
    // Rotate pointer to current angle
    juce::AffineTransform transform = juce::AffineTransform::rotation(currentAngle, 0.0f, 0.0f)
                                                           .translated(centre.x, centre.y);

    // Pointer neon highlight
    g.setColour(activeGlowColour);
    g.fillPath(pointer, transform);
}

void BrutalistLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                            bool shouldDrawButtonAsHighlighted, bool /*shouldDrawButtonAsDown*/)
{
    auto bounds = button.getLocalBounds().toFloat().reduced(2.0f);
    bool isOn = button.getToggleState();
    bool isRed = button.getName().containsIgnoreCase("036") || button.getName().containsIgnoreCase("overdrive");

    juce::Colour activeColor = isRed ? allyRed : weerwaterTeal;

    // Background plate (Brutalist industrial metal switch block)
    g.setColour(concretePanel);
    g.fillRoundedRectangle(bounds, 3.0f);

    g.setColour(shouldDrawButtonAsHighlighted ? concreteBorder.brighter(0.4f) : concreteBorder);
    g.drawRoundedRectangle(bounds, 3.0f, 1.5f);

    // Left LED Indicator light
    float ledSize = 10.0f;
    float ledX = bounds.getX() + 10.0f;
    float ledY = bounds.getCentreY() - ledSize * 0.5f;

    if (isOn)
    {
        // Glowing LED halo
        g.setColour(activeColor.withAlpha(0.35f));
        g.fillEllipse(ledX - 3.0f, ledY - 3.0f, ledSize + 6.0f, ledSize + 6.0f);

        // Bright LED core
        g.setColour(activeColor);
        g.fillEllipse(ledX, ledY, ledSize, ledSize);

        // Center highlight
        g.setColour(juce::Colours::white.withAlpha(0.8f));
        g.fillEllipse(ledX + 2.0f, ledY + 2.0f, 3.0f, 3.0f);
    }
    else
    {
        // Dark unlit LED
        g.setColour(juce::Colour(0xff22252a));
        g.fillEllipse(ledX, ledY, ledSize, ledSize);
        g.setColour(concreteBorder);
        g.drawEllipse(ledX, ledY, ledSize, ledSize, 1.0f);
    }

    // Button Text
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold));
    g.setColour(isOn ? industrialText : mutedText);
    
    auto textArea = bounds.withTrimmedLeft(ledX + ledSize + 8.0f - bounds.getX()).withTrimmedRight(6.0f);
    g.drawFittedText(button.getButtonText(), textArea.toNearestInt(), juce::Justification::centredLeft, 1);
}

void BrutalistLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                                        int buttonX, int buttonY, int buttonW, int buttonH,
                                        juce::ComboBox& box)
{
    juce::ignoreUnused(buttonX, buttonY, buttonW, buttonH, isButtonDown);
    auto bounds = juce::Rectangle<int>(0, 0, width, height).toFloat().reduced(1.0f);

    // Brutalist panel
    g.setColour(concretePanel);
    g.fillRoundedRectangle(bounds, 2.0f);

    g.setColour(box.hasKeyboardFocus(true) ? weerwaterTeal : concreteBorder);
    g.drawRoundedRectangle(bounds, 2.0f, 1.2f);

    // Down arrow
    float arrowX = static_cast<float>(width - 16);
    float arrowY = static_cast<float>(height) * 0.5f - 2.0f;
    juce::Path arrow;
    arrow.addTriangle(arrowX, arrowY, arrowX + 8.0f, arrowY, arrowX + 4.0f, arrowY + 5.0f);
    g.setColour(weerwaterTeal);
    g.fillPath(arrow);
}

void BrutalistLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    g.setColour(concretePanel);
    g.fillRect(0, 0, width, height);

    g.setColour(concreteBorder);
    g.drawRect(0, 0, width, height, 1);
}

void BrutalistLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                             bool isSeparator, bool isActive, bool isHighlighted,
                                             bool isTicked, bool hasSubMenu, const juce::String& text,
                                             const juce::String& shortcutKeyText,
                                             const juce::Drawable* icon, const juce::Colour* textColour)
{
    juce::ignoreUnused(isSeparator, isActive, hasSubMenu, shortcutKeyText, icon, textColour);

    if (isHighlighted)
    {
        g.setColour(concreteBorder.brighter(0.2f));
        g.fillRect(area);
        g.setColour(weerwaterTeal);
    }
    else
    {
        g.setColour(industrialText);
    }

    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::plain));
    auto r = area.reduced(8, 0);

    if (isTicked)
    {
        g.setColour(weerwaterTeal);
        g.drawText(juce::CharPointer_UTF8("\xe2\x96\xb6 "), r.removeFromLeft(16), juce::Justification::centredLeft);
    }

    g.drawFittedText(text, r, juce::Justification::centredLeft, 1);
}

juce::Font BrutalistLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
}

} // namespace AllyGUI
