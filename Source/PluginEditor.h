#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GUI/EngineeredDepthTheme.h"
#include "GUI/PrimaryKnobsPanel.h"
#include "GUI/SecondaryDspPanel.h"
#include "GUI/HeaderPanel.h"
#include "GUI/SettingsOverlay.h"
#include "GUI/SavePresetOverlay.h"
#include "GUI/NapWaveVisualizer.h"
#include "GUI/KeyboardPanel.h"

class AllyBassAudioProcessorEditor : public juce::AudioProcessorEditor
{
public:
    explicit AllyBassAudioProcessorEditor(AllyBassAudioProcessor&);
    ~AllyBassAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

    bool keyPressed(const juce::KeyPress& key) override;
    bool keyStateChanged(bool isKeyDown) override;
    void modifierKeysChanged(const juce::ModifierKeys& modifiers) override;
    void focusGained(juce::Component::FocusChangeType) override;
    void focusLost(juce::Component::FocusChangeType) override;
    void releaseQwertyNotesOnly();
    void releaseAllPressedKeys();

    // Header, Keyboard, Panel & Modal Dialog Accessors (Stap C.4A - C.4C)
    AllyGUI::HeaderPanel* getHeaderPanel() const noexcept { return headerPanel.get(); }
    AllyGUI::PrimaryKnobsPanel* getPrimaryKnobsPanel() const noexcept { return primaryKnobsPanel.get(); }
    AllyGUI::SecondaryDspPanel* getSecondaryDspPanel() const noexcept { return secondaryDspPanel.get(); }
    AllyGUI::KeyboardPanel* getKeyboardPanel() const noexcept { return keyboardPanel.get(); }
    AllyGUI::NapWaveVisualizer& getVisualizer() noexcept { return visualizer; }
    AllyGUI::SettingsOverlay* getSettingsOverlay() const noexcept { return settingsOverlay.get(); }
    AllyGUI::SavePresetOverlay* getSavePresetOverlay() const noexcept { return savePresetOverlay.get(); }
    int getCurrentOctave() const noexcept { return currentOctave; }
    void setCurrentOctaveSafe(int targetOctave);
    juce::String getFooterStatusText() const;
    juce::String getFooterCreditsText() const;
    juce::Rectangle<int> getFooterBounds() const noexcept;
    bool isModalOverlayVisible() const noexcept;
    void openSettingsModal();
    void openSavePresetModal();
    void openUserPresetAsync();
    void closeModalOverlays();

private:
    static constexpr int kMinOctave = 0;
    static constexpr int kMaxOctave = 5;

    struct LayoutMetrics
    {
        int sideMargin = 14;
        int headerHeight = 70;
        int scopeHeight = 140;
        int keyboardHeight = 66;
        int footerHeight = 28;
    };

    LayoutMetrics computeLayoutMetrics() const noexcept;
    void repaintFooter();

    AllyBassAudioProcessor& audioProcessor;

    // Visualizer (Weerwater Vector Scope)
    AllyGUI::NapWaveVisualizer visualizer;

    // Engineered Depth Interactive Keyboard & Octave Controller (Stap C.4C)
    std::unique_ptr<AllyGUI::KeyboardPanel> keyboardPanel;
    int currentOctave = 2; // C2 = 36 (optimal 808 root)
    std::map<int, int> activeNotesByKey;

    // The 5 Big Primary Knobs + Output Level Meter in Engineered Depth chassis (Stap C.2 + C.4B)
    std::unique_ptr<AllyGUI::PrimaryKnobsPanel> primaryKnobsPanel;

    // Secondary DSP console bar: Stadsdeel DSP, Attack & Release Mini Rings, 036 Overdrive, 1300 Mono Lock (Stap C.3)
    std::unique_ptr<AllyGUI::SecondaryDspPanel> secondaryDspPanel;

    // Top Engineered Depth Header, Branding & Preset Toolbar (Stap C.4A)
    std::unique_ptr<AllyGUI::HeaderPanel> headerPanel;
    std::unique_ptr<AllyGUI::SettingsOverlay> settingsOverlay;
    std::unique_ptr<AllyGUI::SavePresetOverlay> savePresetOverlay;
    std::unique_ptr<juce::FileChooser> openPresetChooser;

    void setupHeaderAndModals();
    void updateKeyboardRange();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllyBassAudioProcessorEditor)
};
