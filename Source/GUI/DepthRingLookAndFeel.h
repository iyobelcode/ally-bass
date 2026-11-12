#pragma once

#include <array>
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

    static constexpr const char* themePropertyId   = "allyDepthTheme";
    static constexpr const char* iconPropertyId    = "allyDepthIcon";
    static constexpr const char* compactPropertyId = "allyDepthCompact";

    static void configureSlider(juce::Slider& slider,
                                EngineeredDepthTheme::AccentTheme theme,
                                EngineeredDepthIcons::IconId icon,
                                bool compact = false);

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

    size_t getEstimatedCacheBytes() const noexcept;

private:
    struct TickGeom
    {
        float x1 = 0.0f;
        float y1 = 0.0f;
        float x2 = 0.0f;
        float y2 = 0.0f;
        float angle = 0.0f;
        bool isMajor = false;
        bool isCardinal9 = false;
    };

    juce::Image cachedOuterBack;
    int cachedOuterW = 0;
    int cachedOuterH = 0;
    float cachedOuterScale = 0.0f;
    uint32_t cachedOuterKey = 0u;
    float cachedStartAngle = -1.0f;
    float cachedEndAngle = -1.0f;
    juce::Path cachedTrackPath;
    std::array<TickGeom, 23> cachedTicks {};

    juce::Image cachedInnerBody;
    int cachedInnerW = 0;
    int cachedInnerH = 0;
    float cachedInnerScale = 0.0f;
    uint32_t cachedInnerKey = 0u;
    float cachedInnerNorm = -2.0f;
};

} // namespace AllyGUI
