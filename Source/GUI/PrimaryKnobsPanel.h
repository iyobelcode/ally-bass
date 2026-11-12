#pragma once

#include <array>
#include <memory>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "DepthRingKnob.h"

namespace AllyGUI
{

/**
 * Shared Engineered Depth primary console chassis hosting all 5 primary Depth Rings
 * (DIEPTE, POLDER PUNCH, HAVEN DIRT, WATERWIJK GLIDE, OUTPUT) in uniform alignment.
 */
class PrimaryKnobsPanel final : public juce::Component,
                                private juce::AudioProcessorValueTreeState::Listener,
                                private juce::AsyncUpdater
{
public:
    static constexpr int knobCount = 5;

    explicit PrimaryKnobsPanel(juce::AudioProcessorValueTreeState& apvts);
    ~PrimaryKnobsPanel() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

    DepthRingKnob* getKnob(int index) const noexcept;
    juce::Rectangle<float> getChassisBounds() const noexcept { return chassisBounds; }

private:
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void handleAsyncUpdate() override;

    juce::AudioProcessorValueTreeState& apvtsRef;
    std::array<std::unique_ptr<DepthRingKnob>, knobCount> knobs;
    std::array<juce::Rectangle<int>, knobCount> slotBounds;
    juce::Rectangle<float> chassisBounds;
    std::atomic<float> havenDirtNormalized { 0.15f };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PrimaryKnobsPanel)
};

} // namespace AllyGUI
