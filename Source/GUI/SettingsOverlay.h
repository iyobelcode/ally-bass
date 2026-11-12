#pragma once

#include "EngineeredDepthModal.h"

namespace AllyGUI
{

/**
 * Engineered Depth Settings & Info dialog for ALLY - 1300 SUB SYNTH.
 * Shows factual version, MIDI/QWERTY usage, live octave state, preset path,
 * typography license notes, and master credits without unsupported fake controls.
 */
class SettingsOverlay : public EngineeredDepthModalOverlay
{
public:
    SettingsOverlay();
    ~SettingsOverlay() override = default;

    void setCurrentOctave(int octaveNumber);
    int getCurrentOctave() const noexcept { return currentOctave; }
    void setActivePresetSummary(const juce::String& summaryText);

    void paint(juce::Graphics& g) override;

protected:
    juce::Point<int> computePreferredPanelSize(int hostWidth, int hostHeight) const override;
    void layoutModalContent(juce::Rectangle<int> contentArea) override;

private:
    int currentOctave = 2;
    juce::String activePresetSummary { "Init" };
    juce::Rectangle<int> infoCardsArea;

    EngineeredDepthActionButton doneButton { "SLUITEN", EngineeredDepthActionButton::Style::Primary, "settingsDone" };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SettingsOverlay)
};

} // namespace AllyGUI
