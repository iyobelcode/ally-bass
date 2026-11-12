#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthTheme.h"

namespace AllyGUI
{

/**
 * Reusable Engineered Depth text button for dialogs and modal overlays
 * (Primary Cyan, Secondary Neutral, Warning Amber/Red).
 */
class EngineeredDepthActionButton : public juce::Button
{
public:
    enum class Style
    {
        Secondary,
        Primary,
        Warning
    };

    EngineeredDepthActionButton(const juce::String& labelText,
                                Style style = Style::Secondary,
                                const juce::String& componentName = {});
    ~EngineeredDepthActionButton() override = default;

    void setStyle(Style newStyle) noexcept;
    Style getStyle() const noexcept { return buttonStyle; }

protected:
    void paintButton(juce::Graphics& g,
                     bool shouldDrawButtonAsHighlighted,
                     bool shouldDrawButtonAsDown) override;

private:
    Style buttonStyle;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineeredDepthActionButton)
};

/**
 * Shared dark chamfered backdrop + centered card base for in-editor modals
 * (Settings/About and Save User Preset), ensuring consistent ESC/click-outside dismissal.
 */
class EngineeredDepthModalOverlay : public juce::Component
{
public:
    explicit EngineeredDepthModalOverlay(const juce::String& titleText,
                                         const juce::String& subtitleText = {});
    ~EngineeredDepthModalOverlay() override = default;

    std::function<void()> onDismissRequested;

    void setTitleText(const juce::String& titleText, const juce::String& subtitleText = {});
    void openModal();
    void closeModal();

    juce::Rectangle<int> getPanelBounds() const noexcept { return panelBounds; }
    juce::Rectangle<int> getContentBounds() const noexcept { return contentBounds; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseUp(const juce::MouseEvent& event) override;
    bool keyPressed(const juce::KeyPress& key) override;

protected:
    virtual juce::Point<int> computePreferredPanelSize(int hostWidth, int hostHeight) const;
    virtual void layoutModalContent(juce::Rectangle<int> contentArea) = 0;
    virtual void onModalOpened() {}

    EngineeredDepthActionButton closeButton { "X", EngineeredDepthActionButton::Style::Secondary, "modalClose" };

private:
    juce::String title;
    juce::String subtitle;
    juce::Rectangle<int> panelBounds;
    juce::Rectangle<int> headerBounds;
    juce::Rectangle<int> contentBounds;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(EngineeredDepthModalOverlay)
};

} // namespace AllyGUI
