#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <memory>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthTheme.h"

class AllyBassAudioProcessor;

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

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;
    void mouseMove(const juce::MouseEvent& e) override;
    void mouseDrag(const juce::MouseEvent& e) override;
    void mouseEnter(const juce::MouseEvent& e) override;
    void mouseExit(const juce::MouseEvent& e) override;

    size_t getEstimatedCacheBytes() const noexcept
    {
        return cachedKeyboardImage.isValid()
            ? static_cast<size_t>(cachedKeyboardImage.getWidth()) * static_cast<size_t>(cachedKeyboardImage.getHeight()) * 4u
            : 0u;
    }

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
    void updateHoverNote(juce::Point<float> pos) noexcept;

    juce::MidiKeyboardState& keyboardStateRef;
    int currentHoverNote = -1;
    mutable juce::Image cachedKeyboardImage;
    mutable int      cachedW = 0;
    mutable int      cachedH = 0;
    mutable float    cachedScale = 0.0f;
    mutable int      cachedRangeStart = -1;
    mutable int      cachedRangeEnd   = -1;
    mutable uint64_t cachedDownMask   = 0;
    mutable int      cachedHoverNote  = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineeredDepthKeyboard)
};

/**
 * Sleek Engineered Depth chassis containing the compact octave controller (- OCT: C2 +)
 * and the interactive 3-octave keyboard bed matching ALLY1300_APPROVED_MASTER_FULL.png.
 */
class KeyboardPanel final : public juce::Component,
                            private juce::Timer
{
public:
    explicit KeyboardPanel(juce::MidiKeyboardState& keyboardState,
                           AllyBassAudioProcessor* processorPtr = nullptr);
    ~KeyboardPanel() override;

    void setOctaveDisplay(int octaveIndex, int minOctave, int maxOctave);

    void paint(juce::Graphics& g) override;
    void resized() override;
    size_t getEstimatedCacheBytes() const noexcept
    {
        size_t bytes = cachedChassisImage.isValid()
            ? static_cast<size_t>(cachedChassisImage.getWidth()) * static_cast<size_t>(cachedChassisImage.getHeight()) * 4u
            : 0u;
        bytes += keyboard.getEstimatedCacheBytes();
        return bytes;
    }

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

    void timerCallback() override;
    void renderChassisToCache(juce::Graphics& g) const;

    AllyBassAudioProcessor* processor = nullptr;
    uint64_t lastExternalMidiVersion = 0;
    std::array<uint64_t, 2> appliedExternalMidiMask {};

    EngineeredDepthKeyboard keyboard;
    OctaveStepButton btnDown { false };
    OctaveStepButton btnUp   { true };

    int displayedOctave = 2;
    juce::Rectangle<float> chassisBounds;
    juce::Rectangle<int>   octaveBarBounds;
    juce::Rectangle<int>   keyboardBedBounds;
    juce::Rectangle<float> octaveReadoutBounds;
    float dividerX = 0.0f;

    mutable juce::Image cachedChassisImage;
    mutable int   cachedChassisW = 0;
    mutable int   cachedChassisH = 0;
    mutable float cachedChassisScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(KeyboardPanel)
};

} // namespace AllyGUI
