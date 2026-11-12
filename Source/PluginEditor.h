#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GUI/BrutalistLookAndFeel.h"
#include "GUI/PrimaryKnobsPanel.h"
#include "GUI/SecondaryDspPanel.h"
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

    // Preset Selector
    juce::Label presetLabel;
    juce::ComboBox presetBox;

    void setupPresets();
    void updateKeyboardRange();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllyBassAudioProcessorEditor)
};
