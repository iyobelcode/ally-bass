#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthTheme.h"

namespace AllyGUI
{

struct ToggleConfig
{
    juce::String title;
    juce::String paramId;
    juce::String componentName;
    EngineeredDepthTheme::AccentTheme theme = EngineeredDepthTheme::AccentTheme::Cyan;
};

/**
 * Reusable Engineered Depth hardware LED toggle switch.
 *
 * Renders the recessed capsule housing, metallic bevel border, glowing LED indicator
 * (crimson Red for 036 OVERDRIVE, cyan Cyan for 1300 MONO LOCK), and monochrome-data
 * label across Normal (On/Off), Hover, Down, Focus, and Disabled states.
 */
class EngineeredDepthToggle : public juce::Component
{
public:
    EngineeredDepthToggle(const ToggleConfig& cfg, juce::AudioProcessorValueTreeState& apvts);
    ~EngineeredDepthToggle() override = default;

    void resized() override;
    void enablementChanged() override;

    juce::ToggleButton& getButton() noexcept { return button; }
    const juce::ToggleButton& getButton() const noexcept { return button; }

    juce::Rectangle<float> getCapsuleBounds() const noexcept;
    juce::Rectangle<float> getLedBounds() const noexcept;

private:
    class HardwareButton final : public juce::ToggleButton
    {
    public:
        explicit HardwareButton(const ToggleConfig& cfg) : config(cfg) {}

        void paintButton(juce::Graphics& g,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

        juce::Rectangle<float> computeCapsuleBounds() const noexcept;
        juce::Rectangle<float> computeLedBounds(juce::Rectangle<float> capsule) const noexcept;

    private:
        ToggleConfig config;
    };

    HardwareButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineeredDepthToggle)
};

} // namespace AllyGUI
