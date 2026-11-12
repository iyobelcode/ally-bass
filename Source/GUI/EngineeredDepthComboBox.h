#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthIcons.h"
#include "EngineeredDepthTheme.h"

namespace AllyGUI
{

struct ComboBoxConfig
{
    juce::String title;
    juce::String paramId;
    juce::String componentName;
    juce::StringArray items;
    EngineeredDepthIcons::IconId icon = EngineeredDepthIcons::IconId::None;
    EngineeredDepthTheme::AccentTheme theme = EngineeredDepthTheme::AccentTheme::Cyan;
};

/**
 * Reusable Engineered Depth labelled selector (vector icon + title + sleek capsule ComboBox).
 * Supports Normal, Hover, Active/Open, Focus, and Disabled states with dark popup menu styling.
 */
class EngineeredDepthComboBox : public juce::Component
{
public:
    EngineeredDepthComboBox(const ComboBoxConfig& cfg, juce::AudioProcessorValueTreeState& apvts);
    ~EngineeredDepthComboBox() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void enablementChanged() override;

    juce::ComboBox& getComboBox() noexcept { return comboBox; }
    const juce::ComboBox& getComboBox() const noexcept { return comboBox; }

    juce::Rectangle<float> getIconBounds() const noexcept { return iconBounds; }
    juce::Rectangle<float> getLabelBounds() const noexcept { return labelBounds; }
    juce::Rectangle<int> getComboBounds() const noexcept { return comboBox.getBounds(); }

private:
    class ComboLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        explicit ComboLookAndFeel(EngineeredDepthComboBox& ownerComponent) : owner(ownerComponent) {}

        juce::Font getComboBoxFont(juce::ComboBox& box) override;
        void positionComboBoxText(juce::ComboBox& box, juce::Label& label) override;
        void drawComboBox(juce::Graphics& g,
                          int width,
                          int height,
                          bool isButtonDown,
                          int buttonX,
                          int buttonY,
                          int buttonW,
                          int buttonH,
                          juce::ComboBox& box) override;
        void drawLabel(juce::Graphics& g, juce::Label& label) override;
        void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
        void drawPopupMenuItem(juce::Graphics& g,
                               const juce::Rectangle<int>& area,
                               bool isSeparator,
                               bool isActive,
                               bool isHighlighted,
                               bool isTicked,
                               bool hasSubMenu,
                               const juce::String& text,
                               const juce::String& shortcutKeyText,
                               const juce::Drawable* icon,
                               const juce::Colour* textColour) override;

    private:
        EngineeredDepthComboBox& owner;
    };

    ComboBoxConfig config;
    ComboLookAndFeel lookAndFeel;
    juce::ComboBox comboBox;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    juce::Rectangle<float> iconBounds;
    juce::Rectangle<float> labelBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineeredDepthComboBox)
};

} // namespace AllyGUI
