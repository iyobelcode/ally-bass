#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthTheme.h"
#include "EngineeredDepthIcons.h"

namespace AllyGUI
{

/**
 * Shared LookAndFeel renderer for Engineered Depth rotary Depth Ring controls.
 *
 * Designed generically for all 5 main controls (DIEPTE, POLDER PUNCH, HAVEN DIRT,
 * WATERWIJK GLIDE, OUTPUT) via per-slider theme/icon properties so a single
 * vector renderer drives the entire dial family.
 */
class DepthRingLookAndFeel : public juce::LookAndFeel_V4
{
public:
    DepthRingLookAndFeel() = default;
    ~DepthRingLookAndFeel() override = default;

    static constexpr const char* themePropertyId = "allyDepthTheme";
    static constexpr const char* iconPropertyId  = "allyDepthIcon";

    static void configureSlider(juce::Slider& slider,
                                EngineeredDepthTheme::AccentTheme theme,
                                EngineeredDepthIcons::IconId icon);

    static EngineeredDepthTheme::ControlState inspectSliderState(const juce::Slider& slider,
                                                                   float normalizedValue);

    void drawRotarySlider(juce::Graphics& g,
                          int x,
                          int y,
                          int width,
                          int height,
                          float sliderPosProportional,
                          float rotaryStartAngle,
                          float rotaryEndAngle,
                          juce::Slider& slider) override;
};

} // namespace AllyGUI
