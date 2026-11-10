#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "BrutalistLookAndFeel.h"

namespace AllyGUI
{

class BrutalistToggle : public juce::Component
{
public:
    BrutalistToggle(const juce::String& text, const juce::String& paramId,
                    juce::AudioProcessorValueTreeState& apvts, bool isRedTheme = false)
    {
        button.setButtonText(text);
        button.setName(isRedTheme ? "036_overdrive" : "mono_lock");
        addAndMakeVisible(button);

        attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
            apvts, paramId, button);
    }

    void resized() override
    {
        button.setBounds(getLocalBounds());
    }

    juce::ToggleButton& getButton() { return button; }

private:
    juce::ToggleButton button;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
};

} // namespace AllyGUI
