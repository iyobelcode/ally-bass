#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DepthRingLookAndFeel.h"

namespace AllyGUI
{

struct DepthRingConfig
{
    juce::String title;
    juce::String paramId;
    juce::String unitSuffix;
    EngineeredDepthTheme::AccentTheme theme = EngineeredDepthTheme::AccentTheme::Cyan;
    EngineeredDepthIcons::IconId icon = EngineeredDepthIcons::IconId::None;
    int valueDecimals = 2;
    std::function<juce::String(double)> valueFormatter;
};

/**
 * Reusable Engineered Depth rotary Depth Ring component.
 *
 * Renders the top uppercase label, glowing status pill, scalable vector Depth Ring,
 * bottom bracketed readout ([ 0.50 NAP ]), and focus outline while keeping the
 * underlying JUCE Slider + APVTS SliderAttachment intact for automation & testing.
 */
class DepthRingKnob : public juce::Component
{
public:
    DepthRingKnob(const DepthRingConfig& cfg, juce::AudioProcessorValueTreeState& apvts);
    ~DepthRingKnob() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void enablementChanged() override;

    juce::Slider& getSlider() noexcept { return slider; }
    const juce::Slider& getSlider() const noexcept { return slider; }

    const juce::String& getFormattedValueText() const noexcept { return formattedValue; }
    EngineeredDepthTheme::ControlState getCurrentVisualState() const;

    juce::Rectangle<float> getTitleBounds() const noexcept { return titleBounds; }
    juce::Rectangle<float> getPillBounds() const noexcept { return pillBounds; }
    juce::Rectangle<float> getDialBounds() const noexcept { return dialBounds; }
    juce::Rectangle<float> getReadoutBounds() const noexcept { return readoutBounds; }

private:
    class DialSlider final : public juce::Slider
    {
    public:
        explicit DialSlider(DepthRingKnob& ownerComponent) : owner(ownerComponent) {}

        void mouseEnter(const juce::MouseEvent& e) override
        {
            juce::Slider::mouseEnter(e);
            owner.repaint();
        }

        void mouseExit(const juce::MouseEvent& e) override
        {
            juce::Slider::mouseExit(e);
            owner.repaint();
        }

        void mouseDown(const juce::MouseEvent& e) override
        {
            juce::Slider::mouseDown(e);
            owner.repaint();
        }

        void mouseUp(const juce::MouseEvent& e) override
        {
            juce::Slider::mouseUp(e);
            owner.repaint();
        }

        void focusGained(FocusChangeType cause) override
        {
            juce::Slider::focusGained(cause);
            owner.repaint();
        }

        void focusLost(FocusChangeType cause) override
        {
            juce::Slider::focusLost(cause);
            owner.repaint();
        }

        void enablementChanged() override
        {
            juce::Slider::enablementChanged();
            owner.repaint();
        }

    private:
        DepthRingKnob& owner;
    };

    void updateFormattedValue();
    float getNormalizedSliderValue() const;

    DepthRingConfig config;
    DepthRingLookAndFeel lookAndFeel;
    DialSlider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;

    juce::String formattedValue;
    juce::Rectangle<float> titleBounds;
    juce::Rectangle<float> pillBounds;
    juce::Rectangle<float> dialBounds;
    juce::Rectangle<float> readoutBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(DepthRingKnob)
};

} // namespace AllyGUI
