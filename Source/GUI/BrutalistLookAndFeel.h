#pragma once
#include <juce_gui_basics/juce_gui_basics.h>

namespace AllyGUI
{

class BrutalistLookAndFeel : public juce::LookAndFeel_V4
{
public:
    BrutalistLookAndFeel();
    ~BrutalistLookAndFeel() override = default;

    // Custom Rotary Slider Drawing (Heavy milled aluminium with neon arc)
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float sliderPosProportional, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider& slider) override;

    // Custom Button Drawing (Brutalist metal switches with LED indicators)
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                          bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    // Custom ComboBox Drawing
    void drawComboBox(juce::Graphics& g, int width, int height, bool isButtonDown,
                      int buttonX, int buttonY, int buttonW, int buttonH,
                      juce::ComboBox& box) override;

    void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
    void drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                           bool isSeparator, bool isActive, bool isHighlighted,
                           bool isTicked, bool hasSubMenu, const juce::String& text,
                           const juce::String& shortcutKeyText,
                           const juce::Drawable* icon, const juce::Colour* textColour) override;

    juce::Font getLabelFont(juce::Label& label) override;

    // Color definitions
    static const juce::Colour concreteDark;       // #121316
    static const juce::Colour concretePanel;      // #1A1C20
    static const juce::Colour concreteBorder;     // #2D3139
    static const juce::Colour aluminiumLight;     // #D8DCE3
    static const juce::Colour aluminiumDark;      // #40444C
    static const juce::Colour weerwaterTeal;      // #00E5FF
    static const juce::Colour allyRed;            // #FF3B30
    static const juce::Colour industrialText;     // #E0E0E0
    static const juce::Colour mutedText;          // #707580
};

} // namespace AllyGUI
