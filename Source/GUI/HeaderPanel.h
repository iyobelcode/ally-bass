#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../PluginProcessor.h"
#include "EngineeredDepthIconButton.h"
#include "EngineeredDepthTheme.h"

namespace AllyGUI
{

/**
 * Engineered Depth Top Header Panel for ALLY - 1300 SUB SYNTH (Stap C.4A).
 *
 * Layout left-to-right:
 *   [ A-emblem & symmetrical water waves ] | [ ALLY / 1300 / SUB SYNTH ] |
 *   [ ALMERE / POLDER / STAD / 036 ] / [ Almere map + Weerwater ] /
 *   [ < ] [ > ] [ PRESET Selector ] [ Open ] [ Save ] [ Settings ]
 *
 * Strictly syncs with processor state via APVTS & ValueTree listeners without
 * continuous timers or modifying parameters on editor creation/close.
 */
class HeaderPanel : public juce::Component,
                    private juce::AudioProcessorValueTreeState::Listener,
                    private juce::ValueTree::Listener,
                    private juce::AsyncUpdater
{
public:
    class PresetSelectorButton final : public juce::Button
    {
    public:
        PresetSelectorButton();
        ~PresetSelectorButton() override = default;

        void setDisplayPreset(const juce::String& presetName, bool isModified);
        const juce::String& getPresetNameText() const noexcept { return presetNameText; }
        bool isPresetModified() const noexcept { return modifiedFlag; }

    protected:
        void paintButton(juce::Graphics& g,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    private:
        juce::String presetNameText { "Init" };
        bool modifiedFlag = false;

        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PresetSelectorButton)
    };

    explicit HeaderPanel(AllyBassAudioProcessor& processor);
    ~HeaderPanel() override;

    std::function<void()> onBeforeInteractiveAction;
    std::function<void()> onOpenPresetRequested;
    std::function<void()> onSavePresetRequested;
    std::function<void()> onSettingsRequested;

    void refreshFromProcessor();
    void showPresetPopupMenu();

    /** Builds the dark Engineered Depth preset popup menu (exposed for testing as well as UI). */
    std::unique_ptr<juce::PopupMenu> buildPresetPopupMenu(std::vector<AllyPresets::UserPresetEntry>& outUserEntries) const;

    /** Pure geometry builder for mirror-symmetrical A-emblem + double water waves. */
    static void buildEmblemPaths(juce::Rectangle<float> emblemBox,
                                 juce::Path& outChevronA,
                                 juce::Path& outUpperWave,
                                 juce::Path& outLowerLeftWave,
                                 juce::Path& outLowerRightWave);

    // Component & layout inspection getters
    EngineeredDepthIconButton& getPrevButton() noexcept     { return prevButton; }
    EngineeredDepthIconButton& getNextButton() noexcept     { return nextButton; }
    PresetSelectorButton&      getPresetButton() noexcept   { return presetButton; }
    EngineeredDepthIconButton& getOpenButton() noexcept     { return openButton; }
    EngineeredDepthIconButton& getSaveButton() noexcept     { return saveButton; }
    EngineeredDepthIconButton& getSettingsButton() noexcept { return settingsButton; }

    juce::String getDisplayedPresetText() const;
    juce::Rectangle<float> getChassisBounds() const noexcept  { return chassisBounds; }
    juce::Rectangle<float> getEmblemBounds() const noexcept   { return emblemBounds; }
    juce::Rectangle<float> getWordmarkBounds() const noexcept { return wordmarkBounds; }
    juce::Rectangle<float> getLocationBounds() const noexcept { return locationBounds; }
    juce::Rectangle<float> getMapBounds() const noexcept      { return mapBounds; }
    juce::Rectangle<float> getToolbarBounds() const noexcept  { return toolbarBounds; }

    void paint(juce::Graphics& g) override;
    void resized() override;
    size_t getEstimatedCacheBytes() const noexcept
    {
        return cachedStaticHeader.isValid()
            ? static_cast<size_t>(cachedStaticHeader.getWidth()) * static_cast<size_t>(cachedStaticHeader.getHeight()) * 4u
            : 0u;
    }

private:
    class PresetMenuLookAndFeel final : public juce::LookAndFeel_V4
    {
    public:
        void drawPopupMenuBackground(juce::Graphics& g, int width, int height) override;
        void drawPopupMenuSectionHeaderWithOptions(juce::Graphics& g,
                                                   const juce::Rectangle<int>& area,
                                                   const juce::String& sectionName,
                                                   const juce::PopupMenu::Options&) override;
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
    };

    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;
    void valueTreeRedirected(juce::ValueTree& treeWhichHasBeenChanged) override;
    void handleAsyncUpdate() override;

    void drawChassis(juce::Graphics& g) const;
    void drawAllyEmblem(juce::Graphics& g) const;
    void drawWordmark(juce::Graphics& g) const;
    void drawLocationStack(juce::Graphics& g) const;
    void drawAlmereMapMotif(juce::Graphics& g) const;
    void renderStaticHeaderToCache(juce::Graphics& g) const;

    AllyBassAudioProcessor& audioProcessor;
    PresetMenuLookAndFeel menuLookAndFeel;

    EngineeredDepthIconButton prevButton     { EngineeredDepthIcons::IconId::ChevronLeft,  "presetPrev", "Vorige preset (rondlopend)" };
    EngineeredDepthIconButton nextButton     { EngineeredDepthIcons::IconId::ChevronRight, "presetNext", "Volgende preset (rondlopend)" };
    PresetSelectorButton      presetButton;
    EngineeredDepthIconButton openButton     { EngineeredDepthIcons::IconId::Folder,       "presetOpen", "Open user preset (.allypreset)" };
    EngineeredDepthIconButton saveButton     { EngineeredDepthIcons::IconId::Save,         "presetSave", "Save user preset (.allypreset)" };
    EngineeredDepthIconButton settingsButton { EngineeredDepthIcons::IconId::Settings,     "settingsBtn", "Settings & Info" };

    juce::Rectangle<float> chassisBounds;
    juce::Rectangle<float> emblemBounds;
    float divider1X = 0.0f;
    juce::Rectangle<float> wordmarkBounds;
    float divider2X = 0.0f;
    juce::Rectangle<float> locationBounds;
    juce::Rectangle<float> mapBounds;
    juce::Rectangle<float> toolbarBounds;

    mutable juce::Image cachedStaticHeader;
    mutable int cachedW = 0;
    mutable int cachedH = 0;
    mutable float cachedScale = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(HeaderPanel)
};

} // namespace AllyGUI
