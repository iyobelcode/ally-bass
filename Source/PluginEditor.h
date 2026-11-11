#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "GUI/BrutalistLookAndFeel.h"
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

    // The 5 Big Primary Knobs
    std::unique_ptr<AllyGUI::HeavyKnob> knobDiepte;
    std::unique_ptr<AllyGUI::HeavyKnob> knobPolderPunch;
    std::unique_ptr<AllyGUI::HeavyKnob> knobHavenDirt;
    std::unique_ptr<AllyGUI::HeavyKnob> knobWeerwaterGlide;
    std::unique_ptr<AllyGUI::HeavyKnob> knobMasterVol;

    // The 2 Compact Knobs (Attack & Release)
    std::unique_ptr<AllyGUI::HeavyKnob> knobAttack;
    std::unique_ptr<AllyGUI::HeavyKnob> knobRelease;

    // Stadsdeel Mode Selector
    juce::Label stadsdeelLabel;
    juce::ComboBox stadsdeelBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> stadsdeelAttachment;

    // 2 Master Switches
    std::unique_ptr<AllyGUI::BrutalistToggle> toggleOverdrive036;
    std::unique_ptr<AllyGUI::BrutalistToggle> toggleMonoLock1300;

    // Preset Selector
    juce::Label presetLabel;
    juce::ComboBox presetBox;

    void setupPresets();
    void updateKeyboardRange();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllyBassAudioProcessorEditor)
};
