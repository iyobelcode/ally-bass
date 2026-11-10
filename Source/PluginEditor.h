#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
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

private:
    AllyBassAudioProcessor& audioProcessor;
    AllyGUI::BrutalistLookAndFeel brutalistLookAndFeel;

    // Visualizer
    AllyGUI::NapWaveVisualizer visualizer;

    // The 6 Primary Knobs
    std::unique_ptr<AllyGUI::HeavyKnob> knobDiepte;
    std::unique_ptr<AllyGUI::HeavyKnob> knobPolderPunch;
    std::unique_ptr<AllyGUI::HeavyKnob> knobVerval;
    std::unique_ptr<AllyGUI::HeavyKnob> knobHavenDirt;
    std::unique_ptr<AllyGUI::HeavyKnob> knobWeerwaterGlide;
    std::unique_ptr<AllyGUI::HeavyKnob> knobMasterVol;

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

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllyBassAudioProcessorEditor)
};
