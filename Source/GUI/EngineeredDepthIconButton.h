#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthIcons.h"
#include "EngineeredDepthTheme.h"

namespace AllyGUI
{

/**
 * Reusable Engineered Depth dark-glass square icon button.
 * Supports Normal, Hover, Pressed, Focused, Active, and Disabled states
 * with optional accent tint (Cyan default or Red).
 */
class EngineeredDepthIconButton : public juce::Button
{
public:
    EngineeredDepthIconButton(EngineeredDepthIcons::IconId iconId,
                              const juce::String& componentName,
                              const juce::String& tooltipText = {},
                              EngineeredDepthTheme::AccentTheme accentTheme = EngineeredDepthTheme::AccentTheme::Cyan);
    ~EngineeredDepthIconButton() override = default;

    void setIcon(EngineeredDepthIcons::IconId newIcon);
    EngineeredDepthIcons::IconId getIcon() const noexcept { return icon; }

    void setIconScale(float scaleFraction) noexcept;

protected:
    void paintButton(juce::Graphics& g,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

private:
    EngineeredDepthIcons::IconId icon;
    EngineeredDepthTheme::AccentTheme theme;
    float iconFraction = 0.50f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineeredDepthIconButton)
};

} // namespace AllyGUI
