#pragma once

#include <functional>
#include <memory>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthTheme.h"

namespace AllyGUI
{

/**
 * Engineered Depth interactive piano keyboard inheriting JUCE's MidiKeyboardComponent
 * for 100% faithful mouse/MIDI/MidiKeyboardState synchronization while rendering dark marine
 * recessed keys, brushed titanium sharp keys, subtle cyan active-note glow, and octave markers.
 */
class EngineeredDepthKeyboard final : public juce::MidiKeyboardComponent
{
public:
    explicit EngineeredDepthKeyboard(juce::MidiKeyboardState& state);
    ~EngineeredDepthKeyboard() override = default;

    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

protected:
    void drawWhiteNote(int midiNoteNumber,
                       juce::Graphics& g,
                       juce::Rectangle<float> area,
                       bool isDown,
                       bool isOver,
                       juce::Colour lineColour,
                       juce::Colour textColour) override;

    void drawBlackNote(int midiNoteNumber,
                       juce::Graphics& g,
                       juce::Rectangle<float> area,
                       bool isDown,
                       bool isOver,
                       juce::Colour noteFillColour) override;

    void drawUpDownButton(juce::Graphics&, int, int, bool, bool, bool) override {}

private:
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineeredDepthKeyboard)
};

/**
 * Sleek Engineered Depth chassis containing the compact octave controller (- OCT: C2 +)
 * and the interactive 3-octave keyboard bed matching ALLY1300_APPROVED_MASTER_FULL.png.
 */
class KeyboardPanel final : public juce::Component
{
public:
    explicit KeyboardPanel(juce::MidiKeyboardState& keyboardState);
    ~KeyboardPanel() override = default;

    void setOctaveDisplay(int octaveIndex, int minOctave, int maxOctave);

    void paint(juce::Graphics& g) override;
    void resized() override;

    std::function<void(int delta)> onOctaveStepRequested;

    // Accessors for editor & regression verification
    EngineeredDepthKeyboard& getKeyboard() noexcept { return keyboard; }
    const EngineeredDepthKeyboard& getKeyboard() const noexcept { return keyboard; }
    juce::Button& getOctaveDownButton() noexcept;
    juce::Button& getOctaveUpButton() noexcept;
    juce::String getDisplayedOctaveText() const;

    juce::Rectangle<float> getChassisBounds() const noexcept { return chassisBounds; }
    juce::Rectangle<int>   getOctaveBarBounds() const noexcept { return octaveBarBounds; }
    juce::Rectangle<int>   getKeyboardBedBounds() const noexcept { return keyboardBedBounds; }
    juce::Rectangle<float> getOctaveReadoutBounds() const noexcept { return octaveReadoutBounds; }

private:
    class OctaveStepButton final : public juce::Button
    {
    public:
        explicit OctaveStepButton(bool isIncrementButton);
        void paintButton(juce::Graphics& g, bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;

    private:
        bool increment = false;
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OctaveStepButton)
    };

    EngineeredDepthKeyboard keyboard;
    OctaveStepButton btnDown { false };
    OctaveStepButton btnUp   { true };

    int displayedOctave = 2;
    juce::Rectangle<float> chassisBounds;
    juce::Rectangle<int>   octaveBarBounds;
    juce::Rectangle<int>   keyboardBedBounds;
    juce::Rectangle<float> octaveReadoutBounds;
    float dividerX = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardPanel)
};

} // namespace AllyGUI
