#include "HeaderPanel.h"
#include <cmath>
#include <vector>

namespace AllyGUI
{

namespace
{

struct MeasuredGlyph
{
    juce::String character;
    float x = 0.0f;
    float width = 0.0f;
};

float drawSpacedOpticalWord(juce::Graphics& g,
                            const juce::Font& font,
                            const juce::String& text,
                            float startX,
                            float baselineY,
                            float targetTotalWidth,
                            juce::Colour textColour,
                            float glowAlpha = 0.0f)
{
    if (text.isEmpty())
        return 0.0f;

    std::vector<MeasuredGlyph> glyphs;
    glyphs.reserve(static_cast<size_t>(text.length()));
    float sumGlyphInkWidths = 0.0f;

    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text.substring(i, i + 1);
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, ch, 0.0f, 0.0f);
        const auto bb = ga.getBoundingBox(0, -1, false);
        const float w = (ch == " ") ? font.getHeight() * 0.28f : juce::jmax(1.5f, bb.getWidth());
        glyphs.push_back({ ch, bb.getX(), w });
        sumGlyphInkWidths += w;
    }

    const int n = static_cast<int>(glyphs.size());
    const float gap = (n > 1)
        ? juce::jmax(0.6f, (targetTotalWidth - sumGlyphInkWidths) / static_cast<float>(n - 1))
        : 0.0f;

    auto renderAt = [&](juce::Colour col, float radiusOffsetX, float radiusOffsetY)
    {
        g.setFont(font);
        g.setColour(col);
        float cursorX = startX;
        for (int i = 0; i < n; ++i)
        {
            const auto& mg = glyphs[static_cast<size_t>(i)];
            if (mg.character != " ")
            {
                juce::GlyphArrangement ga;
                ga.addLineOfText(font, mg.character, cursorX - mg.x + radiusOffsetX, baselineY + radiusOffsetY);
                ga.draw(g);
            }
            cursorX += mg.width + gap;
        }
    };

    if (glowAlpha > 0.01f)
    {
        const auto glowCol = textColour.withAlpha(glowAlpha * 0.28f);
        renderAt(glowCol, -0.7f, 0.0f);
        renderAt(glowCol, 0.7f, 0.0f);
        renderAt(glowCol, 0.0f, -0.6f);
        renderAt(glowCol, 0.0f, 0.6f);
    }

    renderAt(textColour, 0.0f, 0.0f);
    return sumGlyphInkWidths + gap * static_cast<float>(juce::jmax(0, n - 1));
}

float measureNaturalInkSpan(const juce::Font& font, const juce::String& text, float trackingEm)
{
    if (text.isEmpty())
        return 0.0f;

    const float extraPx = font.getHeight() * trackingEm;
    float total = 0.0f;
    for (int i = 0; i < text.length(); ++i)
    {
        const auto ch = text.substring(i, i + 1);
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, ch, 0.0f, 0.0f);
        const auto bb = ga.getBoundingBox(0, -1, false);
        total += (ch == " ") ? font.getHeight() * 0.28f : juce::jmax(1.5f, bb.getWidth());
        if (i + 1 < text.length())
            total += extraPx;
    }
    return total;
}

} // namespace

// =============================================================================
// PresetSelectorButton
// =============================================================================

HeaderPanel::PresetSelectorButton::PresetSelectorButton()
    : juce::Button("presetSelector")
{
    setName("presetSelector");
    setTitle("PRESET");
    setTooltip("Selecteer factory of user preset");
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void HeaderPanel::PresetSelectorButton::setDisplayPreset(const juce::String& presetName, bool isModified)
{
    const auto cleanName = presetName.isNotEmpty() ? presetName : juce::String("Init");
    if (presetNameText != cleanName || modifiedFlag != isModified)
    {
        presetNameText = cleanName;
        modifiedFlag = isModified;
        repaint();
    }
}

void HeaderPanel::PresetSelectorButton::paintButton(juce::Graphics& g,
                                                    bool shouldDrawButtonAsHighlighted,
                                                    bool shouldDrawButtonAsDown)
{
    const auto outer = getLocalBounds().toFloat().reduced(1.5f);
    if (outer.getWidth() < 40.0f || outer.getHeight() < 16.0f)
        return;

    const float radius = juce::jlimit(5.5f, 9.0f, outer.getHeight() * 0.24f);
    const bool enabled = isEnabled();
    const bool hovered = enabled && shouldDrawButtonAsHighlighted;
    const bool down    = enabled && shouldDrawButtonAsDown;
    const bool focused = enabled && hasKeyboardFocus(true);

    const auto state = EngineeredDepthTheme::ControlState { hovered, down, down, focused, enabled };
    const auto palette = EngineeredDepthTheme::resolveAccent(EngineeredDepthTheme::AccentTheme::Cyan, state);

    if (focused)
    {
        EngineeredDepthTheme::drawFocusOutline(g, outer.expanded(1.0f), radius + 1.0f, palette.focus);
    }

    // Dark obsidian capsule fill matching EngineeredDepthComboBox
    const auto topFill = !enabled
        ? EngineeredDepthTheme::Colors::bg.withAlpha(0.65f)
        : (hovered || down
            ? EngineeredDepthTheme::Colors::surface2.interpolatedWith(EngineeredDepthTheme::Colors::surfaceRaised, 0.55f)
            : EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.42f));
    const auto botFill = !enabled
        ? EngineeredDepthTheme::Colors::bg.withAlpha(0.55f)
        : (hovered || down
            ? EngineeredDepthTheme::Colors::surface1
            : EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::bg, 0.35f));

    juce::ColourGradient fillGrad(topFill, outer.getCentreX(), outer.getY(),
                                  botFill, outer.getCentreX(), outer.getBottom(),
                                  false);
    g.setGradientFill(fillGrad);
    g.fillRoundedRectangle(outer, radius);

    if (enabled)
    {
        juce::ColourGradient rimGrad(EngineeredDepthTheme::Colors::stroke.withAlpha(hovered ? 0.85f : 0.62f),
                                     outer.getX(), outer.getY(),
                                     (down ? palette.primary : EngineeredDepthTheme::Colors::metalRimLight)
                                         .withAlpha(down ? 0.72f : (hovered ? 0.52f : 0.34f)),
                                     outer.getRight(), outer.getBottom(),
                                     false);
        rimGrad.addColour(0.45, EngineeredDepthTheme::Colors::metalRimMid.withAlpha(hovered ? 0.54f : 0.38f));
        g.setGradientFill(rimGrad);
        g.drawRoundedRectangle(outer, radius, (hovered || down) ? 1.25f : 1.05f);

        juce::ColourGradient bottomSheen(juce::Colours::transparentBlack,
                                         outer.getX() + outer.getWidth() * 0.28f, outer.getBottom() - 0.6f,
                                         palette.primary.withAlpha(down ? 0.46f : (hovered ? 0.32f : 0.21f)),
                                         outer.getRight() - radius - 14.0f, outer.getBottom() - 0.6f,
                                         false);
        bottomSheen.addColour(0.95, juce::Colours::transparentBlack);
        g.setGradientFill(bottomSheen);
        g.drawLine(outer.getX() + radius + 3.0f, outer.getBottom() - 0.6f,
                   outer.getRight() - radius - 3.0f, outer.getBottom() - 0.6f, 1.1f);
    }
    else
    {
        g.setColour(EngineeredDepthTheme::Colors::disabled.withAlpha(0.48f));
        g.drawRoundedRectangle(outer, radius, 1.0f);
    }

    // ChevronDown on right
    const float chevronZoneW = juce::jlimit(20.0f, 30.0f, outer.getHeight() * 0.80f);
    const float iconSize = juce::jlimit(10.0f, 15.0f, outer.getHeight() * 0.40f);
    const auto chevronRect = juce::Rectangle<float>(iconSize, iconSize)
                                 .withCentre({ outer.getRight() - chevronZoneW * 0.52f, outer.getCentreY() });
    const auto chevronCol = enabled
        ? (hovered || down ? palette.coreHighlight : palette.primary)
        : EngineeredDepthTheme::Colors::disabled;
    const float chevronStroke = juce::jlimit(1.55f, 2.2f, iconSize * 0.16f);

    EngineeredDepthIcons::drawIcon(g,
                                   EngineeredDepthIcons::IconId::ChevronDown,
                                   chevronRect,
                                   chevronCol,
                                   palette.coreHighlight,
                                   chevronStroke,
                                   enabled ? (hovered || down ? 0.35f : 0.16f) : 0.0f);

    // Inner text row: optional muted "PRESET" tag + active preset name (+ "*")
    auto textArea = outer.reduced(7.5f, 2.0f);
    textArea.setRight(outer.getRight() - chevronZoneW);
    if (textArea.getWidth() < 20.0f)
        return;

    const float tagFontPx = juce::jlimit(7.8f, 10.2f, outer.getHeight() * 0.28f);
    const float nameFontPx = juce::jlimit(8.8f, 11.8f, outer.getHeight() * 0.34f);

    if (textArea.getWidth() > 96.0f)
    {
        const float tagW = juce::jlimit(32.0f, 42.0f, textArea.getWidth() * 0.24f);
        auto tagRect = textArea.removeFromLeft(tagW);
        g.setFont(EngineeredDepthTheme::makeUiFont(tagFontPx, false));
        g.setColour(enabled ? EngineeredDepthTheme::Colors::textMuted : EngineeredDepthTheme::Colors::disabled);
        g.drawFittedText("PRESET", tagRect.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
        textArea.removeFromLeft(4.0f);
    }

    const juce::String displayStr = presetNameText + (modifiedFlag ? " *" : "");
    g.setFont(EngineeredDepthTheme::makeDataFont(nameFontPx, false));
    g.setColour(enabled
        ? (modifiedFlag ? EngineeredDepthTheme::Colors::cyanBright : EngineeredDepthTheme::Colors::textPrimary)
        : EngineeredDepthTheme::Colors::disabled);
    g.drawFittedText(displayStr,
                     textArea.toNearestInt(),
                     juce::Justification::centredLeft,
                     1,
                     0.78f);
}

// =============================================================================
// PresetMenuLookAndFeel
// =============================================================================

void HeaderPanel::PresetMenuLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(EngineeredDepthTheme::Colors::bg.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.28f));
    g.fillRect(area);
    g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(0.85f));
    g.drawRect(area.reduced(0.5f), 1.0f);
}

void HeaderPanel::PresetMenuLookAndFeel::drawPopupMenuSectionHeaderWithOptions(
    juce::Graphics& g,
    const juce::Rectangle<int>& area,
    const juce::String& sectionName,
    const juce::PopupMenu::Options&)
{
    g.setFont(EngineeredDepthTheme::makeUiFont(9.5f, true));
    g.setColour(EngineeredDepthTheme::Colors::cyanDim);
    g.drawFittedText(sectionName, area.reduced(12, 0), juce::Justification::centredLeft, 1, 0.9f);
}

void HeaderPanel::PresetMenuLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
                                                           const juce::Rectangle<int>& area,
                                                           bool isSeparator,
                                                           bool isActive,
                                                           bool isHighlighted,
                                                           bool isTicked,
                                                           bool,
                                                           const juce::String& text,
                                                           const juce::String&,
                                                           const juce::Drawable*,
                                                           const juce::Colour*)
{
    if (isSeparator)
    {
        const auto r = area.reduced(10, 0).toFloat();
        g.setColour(EngineeredDepthTheme::Colors::strokeSubtle);
        g.drawHorizontalLine(area.getCentreY(), r.getX(), r.getRight());
        return;
    }

    const auto itemBounds = area.reduced(3, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour(EngineeredDepthTheme::Colors::surfaceRaised);
        g.fillRoundedRectangle(itemBounds, 4.0f);
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.fillRect(itemBounds.getX() + 1.0f, itemBounds.getY() + 3.0f, 2.5f, itemBounds.getHeight() - 6.0f);
    }
    else if (isTicked)
    {
        g.setColour(EngineeredDepthTheme::Colors::surface1.withAlpha(0.70f));
        g.fillRoundedRectangle(itemBounds, 4.0f);
    }

    if (isTicked)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.fillEllipse(itemBounds.getX() + 7.5f, itemBounds.getCentreY() - 2.5f, 5.0f, 5.0f);
    }

    const auto textColour = !isActive
        ? EngineeredDepthTheme::Colors::disabled
        : (isTicked ? EngineeredDepthTheme::Colors::cyanBright : EngineeredDepthTheme::Colors::textPrimary);

    g.setColour(textColour);
    g.setFont(EngineeredDepthTheme::makeDataFont(11.5f, false));
    g.drawFittedText(text, area.reduced(20, 0), juce::Justification::centredLeft, 1, 0.85f);
}

// =============================================================================
// HeaderPanel
// =============================================================================

HeaderPanel::HeaderPanel(AllyBassAudioProcessor& processor)
    : audioProcessor(processor)
{
    setName("HeaderPanel");
    setInterceptsMouseClicks(false, true);

    prevButton.onClick = [this]()
    {
        if (onBeforeInteractiveAction)
            onBeforeInteractiveAction();
        audioProcessor.selectRelativeFactoryPreset(-1);
        refreshFromProcessor();
    };
    nextButton.onClick = [this]()
    {
        if (onBeforeInteractiveAction)
            onBeforeInteractiveAction();
        audioProcessor.selectRelativeFactoryPreset(+1);
        refreshFromProcessor();
    };
    presetButton.onClick = [this]()
    {
        if (onBeforeInteractiveAction)
            onBeforeInteractiveAction();
        showPresetPopupMenu();
    };
    openButton.onClick = [this]()
    {
        if (onBeforeInteractiveAction)
            onBeforeInteractiveAction();
        if (onOpenPresetRequested)
            onOpenPresetRequested();
    };
    saveButton.onClick = [this]()
    {
        if (onBeforeInteractiveAction)
            onBeforeInteractiveAction();
        if (onSavePresetRequested)
            onSavePresetRequested();
    };
    settingsButton.onClick = [this]()
    {
        if (onBeforeInteractiveAction)
            onBeforeInteractiveAction();
        if (onSettingsRequested)
            onSettingsRequested();
    };

    settingsButton.setIconScale(0.55f);

    addAndMakeVisible(prevButton);
    addAndMakeVisible(nextButton);
    addAndMakeVisible(presetButton);
    addAndMakeVisible(openButton);
    addAndMakeVisible(saveButton);
    addAndMakeVisible(settingsButton);

    for (const auto& id : AllyPresets::getAllParameterIds())
        audioProcessor.apvts.addParameterListener(id, this);
    audioProcessor.apvts.state.addListener(this);

    refreshFromProcessor();
}

HeaderPanel::~HeaderPanel()
{
    cancelPendingUpdate();
    for (const auto& id : AllyPresets::getAllParameterIds())
        audioProcessor.apvts.removeParameterListener(id, this);
    audioProcessor.apvts.state.removeListener(this);
}

void HeaderPanel::parameterChanged(const juce::String&, float)
{
    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
    {
        if (mm->isThisTheMessageThread())
        {
            refreshFromProcessor();
            return;
        }
    }
    triggerAsyncUpdate();
}

void HeaderPanel::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier&)
{
    if (tree != audioProcessor.apvts.state)
        return;

    if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
    {
        if (mm->isThisTheMessageThread())
        {
            refreshFromProcessor();
            return;
        }
    }
    triggerAsyncUpdate();
}

void HeaderPanel::valueTreeRedirected(juce::ValueTree& tree)
{
    if (tree == audioProcessor.apvts.state)
    {
        if (auto* mm = juce::MessageManager::getInstanceWithoutCreating())
        {
            if (mm->isThisTheMessageThread())
            {
                refreshFromProcessor();
                return;
            }
        }
        triggerAsyncUpdate();
    }
}

void HeaderPanel::handleAsyncUpdate()
{
    refreshFromProcessor();
}

void HeaderPanel::refreshFromProcessor()
{
    const auto info = audioProcessor.getPresetStateInfo();
    presetButton.setDisplayPreset(info.displayName, info.modified);
}

juce::String HeaderPanel::getDisplayedPresetText() const
{
    return presetButton.getPresetNameText() + (presetButton.isPresetModified() ? " *" : "");
}

std::unique_ptr<juce::PopupMenu> HeaderPanel::buildPresetPopupMenu(
    std::vector<AllyPresets::UserPresetEntry>& outUserEntries) const
{
    auto menu = std::make_unique<juce::PopupMenu>();
    menu->setLookAndFeel(const_cast<PresetMenuLookAndFeel*>(&menuLookAndFeel));

    const auto info = audioProcessor.getPresetStateInfo();
    const auto& factoryPresets = AllyPresets::getFactoryPresets();

    menu->addSectionHeader("FACTORY PRESETS");
    for (int i = 0; i < static_cast<int>(factoryPresets.size()); ++i)
    {
        const bool isActive = (info.kind == AllyPresets::PresetKind::Factory
                               && info.factoryIndex == i);
        menu->addItem(i + 1,
                      factoryPresets[static_cast<size_t>(i)].name,
                      true,
                      isActive && !info.modified);
    }

    outUserEntries = AllyPresets::scanUserPresetsDirectory();
    if (!outUserEntries.empty())
    {
        menu->addSeparator();
        menu->addSectionHeader("USER PRESETS");
        for (int j = 0; j < static_cast<int>(outUserEntries.size()); ++j)
        {
            const bool isActiveUser = (info.kind == AllyPresets::PresetKind::User
                                       && info.filePath == outUserEntries[static_cast<size_t>(j)].file.getFullPathName());
            menu->addItem(1000 + j,
                          outUserEntries[static_cast<size_t>(j)].name,
                          true,
                          isActiveUser && !info.modified);
        }
    }

    return menu;
}

void HeaderPanel::showPresetPopupMenu()
{
    auto userEntries = std::make_shared<std::vector<AllyPresets::UserPresetEntry>>();
    auto menu = buildPresetPopupMenu(*userEntries);

    const int minMenuW = juce::jmax(210, presetButton.getWidth());
    auto options = juce::PopupMenu::Options()
                       .withTargetComponent(&presetButton)
                       .withMinimumWidth(minMenuW)
                       .withMaximumNumColumns(1)
                       .withStandardItemHeight(24);

    juce::Component::SafePointer<HeaderPanel> safeThis(this);
    menu->showMenuAsync(options, [safeThis, userEntries](int chosenId)
    {
        if (safeThis == nullptr || chosenId <= 0)
            return;

        if (chosenId >= 1 && chosenId <= static_cast<int>(AllyPresets::getFactoryPresets().size()))
        {
            safeThis->audioProcessor.setCurrentProgram(chosenId - 1);
            safeThis->audioProcessor.updateHostDisplay();
            safeThis->refreshFromProcessor();
        }
        else if (chosenId >= 1000)
        {
            const int idx = chosenId - 1000;
            if (idx >= 0 && idx < static_cast<int>(userEntries->size()))
            {
                juce::String err;
                safeThis->audioProcessor.loadUserPresetFromFile((*userEntries)[static_cast<size_t>(idx)].file, err);
                safeThis->refreshFromProcessor();
            }
        }
    });
}

void HeaderPanel::buildEmblemPaths(juce::Rectangle<float> emblemBox,
                                   juce::Path& outChevronA,
                                   juce::Path& outUpperWave,
                                   juce::Path& outLowerLeftWave,
                                   juce::Path& outLowerRightWave)
{
    outChevronA.clear();
    outUpperWave.clear();
    outLowerLeftWave.clear();
    outLowerRightWave.clear();

    const float cx = emblemBox.getCentreX();
    const float w  = emblemBox.getWidth();
    const float h  = emblemBox.getHeight();
    const float xL = emblemBox.getX();
    const float yT = emblemBox.getY();

    auto pt = [&](float nx, float ny) noexcept -> juce::Point<float>
    {
        return { xL + nx * w, yT + ny * h };
    };

    // 1. Flat-top hollow chevron A (mathematically mirrored left/right around nx = 0.5)
    const float topHalfW     = 0.095f;
    const float outerFootW   = 0.330f;
    const float innerFootW   = 0.175f;
    const float aTopY        = 0.055f;
    const float aFootY       = 0.525f;
    const float innerApexY   = 0.235f;

    outChevronA.startNewSubPath(pt(0.5f - topHalfW, aTopY));
    outChevronA.lineTo(pt(0.5f + topHalfW, aTopY));
    outChevronA.lineTo(pt(0.5f + outerFootW, aFootY));
    outChevronA.lineTo(pt(0.5f + innerFootW, aFootY));
    outChevronA.lineTo(pt(0.5f, innerApexY));
    outChevronA.lineTo(pt(0.5f - innerFootW, aFootY));
    outChevronA.lineTo(pt(0.5f - outerFootW, aFootY));
    outChevronA.closeSubPath();

    // 2. Continuous upper cyan wave (left wing -> deep central U trough -> exact mirror right wing)
    // Leaves crisp breathing gap beneath A feet; right half built via exact X mirror across cx.
    outUpperWave.startNewSubPath(pt(0.05f, 0.675f));
    outUpperWave.cubicTo(pt(0.14f, 0.608f), pt(0.20f, 0.622f), pt(0.265f, 0.655f));
    outUpperWave.cubicTo(pt(0.325f, 0.680f), pt(0.375f, 0.600f), pt(0.415f, 0.660f));
    outUpperWave.cubicTo(pt(0.455f, 0.720f), pt(0.460f, 0.935f), pt(0.500f, 0.935f));

    // Mirror control segments in reverse back from center (0.500, 0.935) to right wing (0.950, 0.675)
    outUpperWave.cubicTo(pt(1.0f - 0.460f, 0.935f), pt(1.0f - 0.455f, 0.720f), pt(1.0f - 0.415f, 0.660f));
    outUpperWave.cubicTo(pt(1.0f - 0.375f, 0.600f), pt(1.0f - 0.325f, 0.680f), pt(1.0f - 0.265f, 0.655f));
    outUpperWave.cubicTo(pt(1.0f - 0.200f, 0.622f), pt(1.0f - 0.140f, 0.608f), pt(1.0f - 0.050f, 0.675f));

    // 3. Lower parallel wing wave (build left wing, generate right wing via strict X reflection around cx)
    outLowerLeftWave.startNewSubPath(pt(0.085f, 0.745f));
    outLowerLeftWave.cubicTo(pt(0.160f, 0.688f), pt(0.215f, 0.700f), pt(0.275f, 0.728f));
    outLowerLeftWave.cubicTo(pt(0.335f, 0.750f), pt(0.375f, 0.684f), pt(0.402f, 0.742f));
    outLowerLeftWave.cubicTo(pt(0.422f, 0.785f), pt(0.432f, 0.832f), pt(0.440f, 0.875f));

    outLowerRightWave = outLowerLeftWave;
    outLowerRightWave.applyTransform(juce::AffineTransform::scale(-1.0f, 1.0f).translated(2.0f * cx, 0.0f));
}

void HeaderPanel::resized()
{
    chassisBounds = getLocalBounds().toFloat().reduced(1.0f);
    if (chassisBounds.getWidth() < 260.0f || chassisBounds.getHeight() < 34.0f)
    {
        toolbarBounds = {};
        return;
    }

    const float w = chassisBounds.getWidth();
    const float h = chassisBounds.getHeight();
    const float padX = juce::jlimit(7.0f, 15.0f, w * 0.013f);
    const float padY = juce::jlimit(6.0f, 11.0f, h * 0.14f);

    // 1. Emblem zone on left (generous presence matching master crop)
    const float emblemSize = juce::jlimit(34.0f, 58.0f, h - padY * 1.15f);
    const float emblemW = emblemSize * 1.15f;
    emblemBounds = juce::Rectangle<float>(chassisBounds.getX() + padX * 0.90f,
                                          chassisBounds.getCentreY() - emblemSize * 0.5f,
                                          emblemW,
                                          emblemSize);

    divider1X = emblemBounds.getRight() + juce::jlimit(4.0f, 10.0f, w * 0.008f);

    // 2. Prominent Hero Wordmark ALLY / 1300 / SUB SYNTH (fills header height matching master proportions)
    const float wordW = juce::jlimit(76.0f, 128.0f, w * 0.132f);
    wordmarkBounds = juce::Rectangle<float>(divider1X + juce::jlimit(4.0f, 10.0f, w * 0.008f),
                                            chassisBounds.getY() + padY * 0.30f,
                                            wordW,
                                            h - padY * 0.60f);

    divider2X = wordmarkBounds.getRight() + juce::jlimit(6.0f, 12.0f, w * 0.0095f);

    // 3. Location vertical stack (ALMERE / POLDER / STAD / 036)
    const float locW = juce::jlimit(42.0f, 58.0f, w * 0.068f);
    locationBounds = juce::Rectangle<float>(divider2X + juce::jlimit(5.0f, 10.0f, w * 0.008f),
                                            wordmarkBounds.getY() + padY * 0.15f,
                                            locW,
                                            wordmarkBounds.getHeight() - padY * 0.30f);

    // 4. Right Preset Toolbar: strictly < | > | PRESET Selector | Open | Save | Settings
    // Compact button & capsule budget preserves generous middle Almere/Weerwater map viewport (~20-23%)
    const float btnSize = juce::jlimit(25.0f, 36.0f, h * 0.485f);
    const float gap = juce::jlimit(3.5f, 6.5f, w * 0.0055f);
    const float mapMinW = juce::jlimit(68.0f, 220.0f, w * 0.145f);
    const float toolbarLeftMin = locationBounds.getRight() + mapMinW;
    const float maxToolbarW = juce::jmax(270.0f, chassisBounds.getRight() - padX * 0.85f - toolbarLeftMin);

    const float desiredComboW = juce::jlimit(146.0f, 248.0f, w * 0.218f);
    const float fiveButtonsW = 5.0f * btnSize + 5.0f * gap;
    const float comboW = juce::jmin(desiredComboW, juce::jmax(122.0f, maxToolbarW - fiveButtonsW));
    const float totalToolbarW = fiveButtonsW + comboW;

    toolbarBounds = juce::Rectangle<float>(chassisBounds.getRight() - padX * 0.85f - totalToolbarW,
                                           chassisBounds.getCentreY() - btnSize * 0.5f,
                                           totalToolbarW,
                                           btnSize);

    float tx = toolbarBounds.getX();
    const int btnY = juce::roundToInt(toolbarBounds.getY());
    const int btnS = juce::roundToInt(btnSize);

    prevButton.setBounds(juce::roundToInt(tx), btnY, btnS, btnS);
    tx += btnSize + gap;
    nextButton.setBounds(juce::roundToInt(tx), btnY, btnS, btnS);
    tx += btnSize + gap;
    presetButton.setBounds(juce::roundToInt(tx), btnY, juce::roundToInt(comboW), btnS);
    tx += comboW + gap;
    openButton.setBounds(juce::roundToInt(tx), btnY, btnS, btnS);
    tx += btnSize + gap;
    saveButton.setBounds(juce::roundToInt(tx), btnY, btnS, btnS);
    tx += btnSize + gap;
    settingsButton.setBounds(juce::roundToInt(tx), btnY, btnS, btnS);

    // 5. Middle Slanted Almere Map Zone cleanly filling expanse between Location stack & Toolbar
    const float mapStartX = locationBounds.getRight() + 2.0f;
    const float mapEndX   = toolbarBounds.getX() - 2.0f;
    mapBounds = juce::Rectangle<float>(mapStartX,
                                       chassisBounds.getY() + 1.5f,
                                       juce::jmax(0.0f, mapEndX - mapStartX),
                                       h - 3.0f);

    cachedW = 0;
    cachedH = 0;
}

void HeaderPanel::drawChassis(juce::Graphics& g) const
{
    const float radius = juce::jlimit(8.0f, 13.0f, chassisBounds.getHeight() * 0.19f);

    // Subtle dark vertical metallic background gradient across chassis
    juce::ColourGradient bgGrad(EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.35f),
                                chassisBounds.getCentreX(), chassisBounds.getY(),
                                EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::bg, 0.30f),
                                chassisBounds.getCentreX(), chassisBounds.getBottom(),
                                false);
    bgGrad.addColour(0.55, EngineeredDepthTheme::Colors::bg.interpolatedWith(EngineeredDepthTheme::Colors::surface2, 0.25f));
    g.setGradientFill(bgGrad);
    g.fillRoundedRectangle(chassisBounds, radius);

    // Left corner diagonal metallic bevel reflection matching master header
    {
        g.saveState();
        juce::Path clipRounded;
        clipRounded.addRoundedRectangle(chassisBounds, radius);
        g.reduceClipRegion(clipRounded);

        const float bW = emblemBounds.getWidth() * 1.15f;
        juce::Path leftBevel;
        leftBevel.startNewSubPath(chassisBounds.getX(), chassisBounds.getY());
        leftBevel.lineTo(chassisBounds.getX() + bW, chassisBounds.getY());
        leftBevel.lineTo(chassisBounds.getX() + bW * 0.18f, chassisBounds.getBottom());
        leftBevel.lineTo(chassisBounds.getX(), chassisBounds.getBottom());
        leftBevel.closeSubPath();

        juce::ColourGradient bevelGrad(EngineeredDepthTheme::Colors::surface2.withAlpha(0.28f),
                                       chassisBounds.getX(), chassisBounds.getY(),
                                       EngineeredDepthTheme::Colors::bg.withAlpha(0.05f),
                                       chassisBounds.getX() + bW, chassisBounds.getBottom(),
                                       false);
        g.setGradientFill(bevelGrad);
        g.fillPath(leftBevel);

        // Soft glow behind A-emblem & map center
        juce::ColourGradient mapAmbient(EngineeredDepthTheme::Colors::cyan.withAlpha(0.08f),
                                       mapBounds.getCentreX(), mapBounds.getBottom(),
                                       juce::Colours::transparentBlack,
                                       mapBounds.getCentreX(), mapBounds.getY() - 20.0f,
                                       true);
        g.setGradientFill(mapAmbient);
        g.fillRect(chassisBounds);
        g.restoreState();
    }

    // Outer metallic rim + bright bottom edge reflection matching master UI
    juce::ColourGradient rimGrad(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.45f),
                                 chassisBounds.getX(), chassisBounds.getY(),
                                 EngineeredDepthTheme::Colors::stroke.withAlpha(0.75f),
                                 chassisBounds.getRight(), chassisBounds.getY(),
                                 false);
    g.setGradientFill(rimGrad);
    g.drawRoundedRectangle(chassisBounds, radius, 1.1f);

    juce::ColourGradient bottomHighlight(EngineeredDepthTheme::Colors::metalRimMid.withAlpha(0.35f),
                                         chassisBounds.getX() + radius, chassisBounds.getBottom() - 0.6f,
                                         EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.72f),
                                         chassisBounds.getCentreX(), chassisBounds.getBottom() - 0.6f,
                                         false);
    bottomHighlight.addColour(0.95, EngineeredDepthTheme::Colors::metalRimMid.withAlpha(0.35f));
    g.setGradientFill(bottomHighlight);
    g.drawLine(chassisBounds.getX() + radius, chassisBounds.getBottom() - 0.6f,
               chassisBounds.getRight() - radius, chassisBounds.getBottom() - 0.6f, 1.15f);
}

void HeaderPanel::drawAllyEmblem(juce::Graphics& g) const
{
    if (emblemBounds.isEmpty())
        return;

    juce::Path chevronA, upperWave, lowerLeft, lowerRight;
    buildEmblemPaths(emblemBounds, chevronA, upperWave, lowerLeft, lowerRight);

    // 1. White flat-top hollow chevron A
    g.setColour(EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.14f));
    g.strokePath(chevronA, juce::PathStrokeType(1.6f));
    g.setColour(EngineeredDepthTheme::Colors::textPrimary);
    g.fillPath(chevronA);

    // 2. Symmetrical double water waves with clean cyan glow + bright core stroke
    const float w = emblemBounds.getWidth();
    const float upperThick = juce::jlimit(1.45f, 2.55f, w * 0.044f);
    const float lowerThick = juce::jlimit(1.20f, 2.15f, w * 0.036f);

    const auto upperStroke = juce::PathStrokeType(upperThick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto lowerStroke = juce::PathStrokeType(lowerThick, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto glowUpper   = juce::PathStrokeType(upperThick * 2.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    const auto glowLower   = juce::PathStrokeType(lowerThick * 2.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);

    g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.24f));
    g.strokePath(upperWave, glowUpper);
    g.strokePath(lowerLeft, glowLower);
    g.strokePath(lowerRight, glowLower);

    g.setColour(EngineeredDepthTheme::Colors::cyan);
    g.strokePath(upperWave, upperStroke);
    g.strokePath(lowerLeft, lowerStroke);
    g.strokePath(lowerRight, lowerStroke);
}

void HeaderPanel::drawWordmark(juce::Graphics& g) const
{
    if (wordmarkBounds.isEmpty())
        return;

    const float h = wordmarkBounds.getHeight();
    const float w = wordmarkBounds.getWidth();

    // Bold prominent branding hierarchy filling header height cleanly matching master crop
    const float allyPt = juce::jlimit(14.5f, 25.5f, h * 0.445f);
    const float numPt  = juce::jlimit(14.0f, 24.8f, h * 0.438f);   // natural 1.0 ratio!
    const float subPt  = juce::jlimit(7.4f, 11.2f, h * 0.198f);

    const auto allyFont = EngineeredDepthTheme::makeUiFont(allyPt, true);
    const auto numFont  = EngineeredDepthTheme::makeUiFont(numPt, true);
    const auto subFont  = EngineeredDepthTheme::makeUiFont(subPt, false);

    // Optical block width matches ALLY cleanly across 1300 and tracked SUB SYNTH
    const float targetSpan = juce::jmin(w - 2.0f,
                                        juce::jmax(measureNaturalInkSpan(allyFont, "ALLY", 0.12f),
                                                   measureNaturalInkSpan(numFont, "1300", 0.19f)));
    const float startX = wordmarkBounds.getCentreX() - targetSpan * 0.5f;

    const float yAllyBase = wordmarkBounds.getY() + h * 0.305f;
    const float yNumBase  = wordmarkBounds.getY() + h * 0.695f;
    const float ySubBase  = wordmarkBounds.getY() + h * 0.935f;

    drawSpacedOpticalWord(g,
                          allyFont,
                          "ALLY",
                          startX,
                          yAllyBase,
                          targetSpan,
                          EngineeredDepthTheme::Colors::textPrimary,
                          0.0f);

    drawSpacedOpticalWord(g,
                          numFont,
                          "1300",
                          startX,
                          yNumBase,
                          targetSpan,
                          EngineeredDepthTheme::Colors::cyanBright.interpolatedWith(EngineeredDepthTheme::Colors::cyan, 0.52f),
                          1.10f);

    // SUB SYNTH centered strictly across optical brand width
    drawSpacedOpticalWord(g,
                          subFont,
                          "SUB SYNTH",
                          startX,
                          ySubBase,
                          targetSpan * 0.98f,
                          EngineeredDepthTheme::Colors::textSecondary.withAlpha(0.94f),
                          0.0f);
}

void HeaderPanel::drawLocationStack(juce::Graphics& g) const
{
    if (locationBounds.isEmpty())
        return;

    const auto drawVerticalDivider = [&](float x, float maxAlpha)
    {
        const float divPad = chassisBounds.getHeight() * 0.16f;
        const float topY = chassisBounds.getY() + divPad;
        const float botY = chassisBounds.getBottom() - divPad;
        juce::ColourGradient dg(juce::Colours::transparentBlack, x, topY,
                                juce::Colours::transparentBlack, x, botY,
                                false);
        dg.addColour(0.25, EngineeredDepthTheme::Colors::textMuted.withAlpha(maxAlpha));
        dg.addColour(0.75, EngineeredDepthTheme::Colors::textMuted.withAlpha(maxAlpha));
        g.setGradientFill(dg);
        g.drawLine(x, topY, x, botY, 1.0f);
    };

    // Divider between brand wordmark and ALMERE stack matching master header
    drawVerticalDivider(divider2X, 0.72f);

    static const char* lines[4] = { "ALMERE", "POLDER", "STAD", "036" };
    const float h = locationBounds.getHeight();
    const float insetY = h * 0.04f;
    const float rowH = (h - insetY * 2.0f) * 0.25f;
    const float fontPx = juce::jlimit(7.8f, 11.2f, h * 0.222f);
    g.setFont(EngineeredDepthTheme::makeDataFont(fontPx, false));

    for (int i = 0; i < 4; ++i)
    {
        const auto rowRect = juce::Rectangle<float>(locationBounds.getX(),
                                                    locationBounds.getY() + insetY + static_cast<float>(i) * rowH,
                                                    locationBounds.getWidth(),
                                                    rowH);
        const float alpha = (i == 3) ? 0.98f : 0.88f;
        g.setColour(EngineeredDepthTheme::Colors::textPrimary.withAlpha(alpha));
        g.drawFittedText(lines[i], rowRect.toNearestInt(), juce::Justification::centredLeft, 1, 0.85f);
    }
}

void HeaderPanel::drawAlmereMapMotif(juce::Graphics& g) const
{
    if (mapBounds.getWidth() < 42.0f || mapBounds.getHeight() < 24.0f)
        return;

    const float xL = mapBounds.getX();
    const float xR = mapBounds.getRight();
    const float yT = mapBounds.getY();
    const float yB = mapBounds.getBottom();
    const float h  = mapBounds.getHeight();
    const float slant = juce::jmin(mapBounds.getWidth() * 0.26f, h * 0.48f);

    // Parallellogram map viewport sloping diagonally like the master bezel
    const juce::Point<float> pBL { xL,          yB };
    const juce::Point<float> pTL { xL + slant,  yT };
    const juce::Point<float> pTR { xR,          yT };
    const juce::Point<float> pBR { xR - slant,  yB };

    if (pBR.x <= pBL.x + 12.0f || pTR.x <= pTL.x + 12.0f)
        return;

    juce::Path trapClip;
    trapClip.startNewSubPath(pBL);
    trapClip.lineTo(pTL);
    trapClip.lineTo(pTR);
    trapClip.lineTo(pBR);
    trapClip.closeSubPath();

    const float mw = mapBounds.getWidth();
    auto mPt = [&](float nx, float ny) noexcept -> juce::Point<float>
    {
        return { xL + nx * mw, yT + ny * h };
    };

    g.saveState();
    g.reduceClipRegion(trapClip);

    // 1. Deep nocturnal polder map glass interior + glowing turquoise depth under Weerwater basin
    juce::ColourGradient mapBg(juce::Colour(0xff02080d),
                               mapBounds.getCentreX(), yT,
                               juce::Colour(0xff071a26),
                               mapBounds.getCentreX(), yB,
                               false);
    g.setGradientFill(mapBg);
    g.fillPath(trapClip);

    // Ambient blue-cyan city glow centered over Stad / Weerwater shore
    juce::ColourGradient cityWash(EngineeredDepthTheme::Colors::cyan.withAlpha(0.14f),
                                  mPt(0.46f, 0.56f).x, mPt(0.46f, 0.56f).y,
                                  juce::Colours::transparentBlack,
                                  mPt(0.05f, 0.15f).x, mPt(0.05f, 0.15f).y,
                                  true);
    g.setGradientFill(cityWash);
    g.fillPath(trapClip);

    // 2. Fine Tier-1 Urban City Block Grids (Stad Centrum, Stedenwijk, Filmwijk, Parkwijk)
    juce::Path fineBlocks;
    // West / Stedenwijk dense tilted blocks
    for (int col = 0; col <= 12; ++col)
    {
        const float u = 0.02f + static_cast<float>(col) * 0.036f;
        fineBlocks.startNewSubPath(mPt(u, 0.02f));
        fineBlocks.lineTo(mPt(u - 0.11f, 0.88f));
    }
    for (int row = 0; row <= 8; ++row)
    {
        const float v = 0.06f + static_cast<float>(row) * 0.105f;
        fineBlocks.startNewSubPath(mPt(-0.02f, v));
        fineBlocks.lineTo(mPt(0.48f, v + 0.12f));
    }

    // North-Central / Almere Stad & Centrum tight architectural matrix
    for (int col = 0; col <= 10; ++col)
    {
        const float u = 0.42f + static_cast<float>(col) * 0.044f;
        fineBlocks.startNewSubPath(mPt(u, -0.02f));
        fineBlocks.lineTo(mPt(u - 0.08f, 0.62f));
    }
    for (int row = 0; row <= 7; ++row)
    {
        const float v = 0.04f + static_cast<float>(row) * 0.092f;
        fineBlocks.startNewSubPath(mPt(0.38f, v + 0.05f));
        fineBlocks.lineTo(mPt(0.92f, v - 0.07f));
    }

    // East / Filmwijk curved waterfront blocks
    for (int col = 0; col <= 6; ++col)
    {
        const float u = 0.72f + static_cast<float>(col) * 0.045f;
        fineBlocks.startNewSubPath(mPt(u, 0.10f));
        fineBlocks.lineTo(mPt(u + 0.05f, 0.68f));
    }

    g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(0.46f));
    g.strokePath(fineBlocks, juce::PathStrokeType(0.60f));

    // 3. Tier-2 Distributor Parkways & Streets
    juce::Path avenues;
    avenues.startNewSubPath(mPt(0.06f, 0.24f));
    avenues.quadraticTo(mPt(0.28f, 0.36f), mPt(0.54f, 0.18f));
    avenues.lineTo(mPt(0.86f, 0.02f));

    avenues.startNewSubPath(mPt(0.02f, 0.56f));
    avenues.quadraticTo(mPt(0.24f, 0.66f), mPt(0.46f, 0.44f));

    avenues.startNewSubPath(mPt(0.22f, 0.00f));
    avenues.lineTo(mPt(0.08f, 0.96f));

    avenues.startNewSubPath(mPt(0.64f, -0.02f));
    avenues.quadraticTo(mPt(0.58f, 0.34f), mPt(0.70f, 0.56f));

    g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.28f));
    g.strokePath(avenues, juce::PathStrokeType(0.80f));

    // 4. Tier-3 Primary Glowing Infrastructure Ring (A6 / Hogering / Tussenring)
    juce::Path highways;
    highways.startNewSubPath(mPt(-0.02f, 0.78f));
    highways.quadraticTo(mPt(0.24f, 0.30f), mPt(0.52f, 0.44f));
    highways.quadraticTo(mPt(0.75f, 0.56f), mPt(0.98f, 0.06f));

    highways.startNewSubPath(mPt(0.46f, -0.04f));
    highways.lineTo(mPt(0.29f, 1.04f));

    highways.startNewSubPath(mPt(0.11f, 0.04f));
    highways.lineTo(mPt(0.42f, 0.84f));

    highways.startNewSubPath(mPt(0.78f, -0.02f));
    highways.lineTo(mPt(0.63f, 0.55f));

    g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.18f));
    g.strokePath(highways, juce::PathStrokeType(2.0f));
    g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.76f));
    g.strokePath(highways, juce::PathStrokeType(0.85f));

    // Arterial junction nodes kept strictly on land outside Weerwater coastline
    for (const auto& jPt : { mPt(0.36f, 0.42f), mPt(0.21f, 0.38f), mPt(0.54f, 0.43f) })
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.32f));
        g.fillEllipse(jPt.x - 2.6f, jPt.y - 2.6f, 5.2f, 5.2f);
        g.setColour(EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.86f));
        g.fillEllipse(jPt.x - 1.2f, jPt.y - 1.2f, 2.4f, 2.4f);
    }

    // 5. Sculpted Weerwater Lake Basin (opaque abyssal water masks land roads beneath it cleanly!)
    juce::Path weerwater;
    weerwater.startNewSubPath(mPt(0.38f, 0.78f));
    weerwater.cubicTo(mPt(0.42f, 0.55f), mPt(0.54f, 0.52f), mPt(0.64f, 0.59f));
    weerwater.cubicTo(mPt(0.71f, 0.64f), mPt(0.76f, 0.54f), mPt(0.84f, 0.59f)); // harbor peninsula/cove indent
    weerwater.cubicTo(mPt(0.91f, 0.65f), mPt(0.90f, 0.85f), mPt(0.86f, 1.06f));
    weerwater.lineTo(mPt(0.36f, 1.06f));
    weerwater.cubicTo(mPt(0.33f, 0.95f), mPt(0.35f, 0.86f), mPt(0.38f, 0.78f));
    weerwater.closeSubPath();

    // Dark opaque water base stops road lines cleanly at the coastline
    g.setColour(juce::Colour(0xff02090f));
    g.fillPath(weerwater);

    const auto coastGlowCenter = mPt(0.52f, 0.60f);
    const auto deepBayCorner   = mPt(0.84f, 1.02f);
    juce::ColourGradient waterGlow(EngineeredDepthTheme::Colors::cyan.withAlpha(0.34f),
                                   coastGlowCenter.x, coastGlowCenter.y,
                                   juce::Colour(0xff02090e).withAlpha(0.96f),
                                   deepBayCorner.x, deepBayCorner.y,
                                   false);
    waterGlow.addColour(0.32, EngineeredDepthTheme::Colors::cyan.withAlpha(0.14f));
    waterGlow.addColour(0.70, juce::Colour(0xff04121b).withAlpha(0.90f));
    g.setGradientFill(waterGlow);
    g.fillPath(weerwater);

    // Subtle interior bathymetric depth rings inside Weerwater
    {
        juce::Graphics::ScopedSaveState lakeClip(g);
        g.reduceClipRegion(weerwater);
        const auto lakeCx = mPt(0.62f, 0.84f);
        for (int ring = 1; ring <= 3; ++ring)
        {
            const float s = 1.0f - static_cast<float>(ring) * 0.20f;
            const auto tx = juce::AffineTransform::translation(-lakeCx.x, -lakeCx.y)
                                .scaled(s, s)
                                .translated(lakeCx.x, lakeCx.y);
            g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.20f - static_cast<float>(ring) * 0.045f));
            g.strokePath(weerwater, juce::PathStrokeType(0.75f), tx);
        }
    }

    // Glowing Weerwater shoreline contour
    g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.28f));
    g.strokePath(weerwater, juce::PathStrokeType(2.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    g.setColour(EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.82f));
    g.strokePath(weerwater, juce::PathStrokeType(1.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Soft vignette + glass overhang shadow along left/right slanted seams
    juce::ColourGradient leftFade(EngineeredDepthTheme::Colors::bg.withAlpha(0.72f), xL, yT,
                                  juce::Colours::transparentBlack, xL + mw * 0.14f, yT, false);
    g.setGradientFill(leftFade);
    g.fillPath(trapClip);

    juce::ColourGradient rightFade(juce::Colours::transparentBlack, xR - mw * 0.16f, yT,
                                   EngineeredDepthTheme::Colors::bg.withAlpha(0.86f), xR, yT, false);
    g.setGradientFill(rightFade);
    g.fillPath(trapClip);

    g.restoreState();

    // Crisp parallel diagonal steel-cyan bezel mullions enclosing map zone
    juce::ColourGradient diagGrad(EngineeredDepthTheme::Colors::metalRimMid.withAlpha(0.68f),
                                  pBL.x, pBL.y,
                                  EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.78f),
                                  pTL.x, pTL.y,
                                  false);
    g.setGradientFill(diagGrad);
    g.drawLine(pBL.x, pBL.y, pTL.x, pTL.y, 1.2f);
    g.drawLine(pBR.x, pBR.y, pTR.x, pTR.y, 1.2f);
}

void HeaderPanel::renderStaticHeaderToCache(juce::Graphics& g) const
{
    drawChassis(g);
    drawAllyEmblem(g);
    drawWordmark(g);
    drawLocationStack(g);
    drawAlmereMapMotif(g);
}

void HeaderPanel::paint(juce::Graphics& g)
{
    if (chassisBounds.isEmpty() || getWidth() <= 0 || getHeight() <= 0)
        return;

    const float scale = juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = getWidth();
    const int h = getHeight();

    if (!cachedStaticHeader.isValid()
        || cachedW != w
        || cachedH != h
        || std::abs(scale - cachedScale) > 0.01f)
    {
        const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(w) * scale));
        const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(h) * scale));
        cachedStaticHeader = juce::Image(juce::Image::ARGB, rw, rh, true);

        juce::Graphics cacheG(cachedStaticHeader);
        cacheG.addTransform(juce::AffineTransform::scale(scale));
        renderStaticHeaderToCache(cacheG);

        cachedW = w;
        cachedH = h;
        cachedScale = scale;
    }

    g.drawImage(cachedStaticHeader, 0, 0, w, h, 0, 0, cachedStaticHeader.getWidth(), cachedStaticHeader.getHeight());
}

} // namespace AllyGUI
