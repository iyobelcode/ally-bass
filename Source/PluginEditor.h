#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GUI/BrutalistLookAndFeel.h"
#include "GUI/PrimaryKnobsPanel.h"
#include "GUI/SecondaryDspPanel.h"
#include "GUI/HeaderPanel.h"
#include "GUI/SettingsOverlay.h"
#include "GUI/SavePresetOverlay.h"
#include "GUI/NapWaveVisualizer.h"
#include "GUI/HeavyKnob.h"
#include "GUI/BrutalistToggle.h"

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

    // Header, Preset & Modal Dialog Accessors (Stap C.4A)
    AllyGUI::HeaderPanel* getHeaderPanel() const noexcept { return headerPanel.get(); }
    AllyGUI::SettingsOverlay* getSettingsOverlay() const noexcept { return settingsOverlay.get(); }
    AllyGUI::SavePresetOverlay* getSavePresetOverlay() const noexcept { return savePresetOverlay.get(); }
    bool isModalOverlayVisible() const noexcept;
    void openSettingsModal();
    void openSavePresetModal();
    void openUserPresetAsync();
    void closeModalOverlays();

private:
    AllyBassAudioProcessor& audioProcessor;
    AllyGUI::BrutalistLookAndFeel brutalistLookAndFeel;

    // Visualizer
    AllyGUI::NapWaveVisualizer visualizer;

    // Interactive On-Screen & Computer Keyboard
    juce::MidiKeyboardComponent keyboardComponent;
    int currentOctave = 2; // C2 = 36 (optimal 808 root)
    std::map<int, int> activeNotesByKey;

    // Octaaf knoppen
    juce::TextButton btnOctaveDown { "-" };
    juce::TextButton btnOctaveUp { "+" };
    juce::Label lblOctave;

    // The 5 Big Primary Knobs in the shared Engineered Depth chassis (Stap C.2)
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
