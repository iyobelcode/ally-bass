#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "BrutalistLookAndFeel.h"

namespace AllyGUI
{

class HeavyKnob : public juce::Component
{
public:
    HeavyKnob(const juce::String& title, const juce::String& paramId, juce::AudioProcessorValueTreeState& apvts,
              const juce::String& suffix = "", bool isRedTheme = false)
        : titleText(title), unitSuffix(suffix)
    {
        slider.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
        slider.setName(isRedTheme ? "dirt_" + paramId : paramId);
        addAndMakeVisible(slider);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
            apvts, paramId, slider);

        slider.onValueChange = [this]() { updateValueLabel(); };
        updateValueLabel();
    }

    void resized() override
    {
        auto area = getLocalBounds();

        // Title at the top
        titleArea = area.removeFromTop(18);

        // Value text at the bottom
        valueArea = area.removeFromBottom(16);

        // Knob fills middle
        slider.setBounds(area.reduced(2));
    }

    void paint(juce::Graphics& g) override
    {
        // 1. Draw Title
        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.5f, juce::Font::bold));
        g.setColour(BrutalistLookAndFeel::industrialText);
        g.drawFittedText(titleText, titleArea, juce::Justification::centred, 1);

        // 2. Draw Value
        g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::plain));
        g.setColour(BrutalistLookAndFeel::weerwaterTeal);
        g.drawFittedText(currentValueString, valueArea, juce::Justification::centred, 1);
    }

    void updateValueLabel()
    {
        float val = static_cast<float>(slider.getValue());
        if (unitSuffix == "s")
            currentValueString = juce::String(val, 2) + " s";
        else if (unitSuffix == "ms")
            currentValueString = juce::String(val, 1) + " ms";
        else if (unitSuffix == "%")
            currentValueString = juce::String(static_cast<int>(val * 100.0f)) + "%";
        else
            currentValueString = juce::String(val, 2) + " " + unitSuffix;

        repaint(valueArea);
    }

    juce::Slider& getSlider() { return slider; }

private:
    juce::String titleText;
    juce::String unitSuffix;
    juce::String currentValueString;

    juce::Rectangle<int> titleArea;
    juce::Rectangle<int> valueArea;

    juce::Slider slider;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

} // namespace AllyGUI
