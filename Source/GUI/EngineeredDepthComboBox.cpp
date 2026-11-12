#include "EngineeredDepthComboBox.h"

namespace AllyGUI
{

EngineeredDepthComboBox::EngineeredDepthComboBox(const ComboBoxConfig& cfg,
                                                 juce::AudioProcessorValueTreeState& apvts)
    : config(cfg),
      lookAndFeel(*this)
{
    setInterceptsMouseClicks(false, true);

    for (int i = 0; i < config.items.size(); ++i)
        comboBox.addItem(config.items[i], i + 1);

    comboBox.setName(config.componentName.isNotEmpty() ? config.componentName : config.paramId);
    comboBox.setTitle(config.title);
    comboBox.setWantsKeyboardFocus(true);
    comboBox.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    comboBox.setLookAndFeel(&lookAndFeel);
    addAndMakeVisible(comboBox);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        apvts, config.paramId, comboBox);

    comboBox.onChange = [this]() { repaint(); };
}

EngineeredDepthComboBox::~EngineeredDepthComboBox()
{
    comboBox.setLookAndFeel(nullptr);
}

void EngineeredDepthComboBox::enablementChanged()
{
    comboBox.setEnabled(isEnabled());
    repaint();
}

void EngineeredDepthComboBox::resized()
{
    const auto inner = getLocalBounds().toFloat().reduced(2.0f);
    if (inner.getWidth() < 120.0f || inner.getHeight() < 26.0f)
    {
        iconBounds = {};
        labelBounds = {};
        comboBox.setBounds(0, 0, 0, 0);
        return;
    }

    float cursorX = inner.getX();
    const float h = inner.getHeight();
    const float w = inner.getWidth();
    const float gap = juce::jlimit(4.0f, 10.0f, w * 0.022f);

    if (config.icon != EngineeredDepthIcons::IconId::None)
    {
        const float iconW = juce::jlimit(18.0f, 30.0f, h * 0.44f);
        const float iconH = iconW * 0.88f;
        iconBounds = juce::Rectangle<float>(iconW, iconH)
                         .withCentre({ cursorX + iconW * 0.5f, inner.getCentreY() });
        cursorX += iconW + gap;
    }
    else
    {
        iconBounds = {};
    }

    if (config.title.isNotEmpty())
    {
        const float titleFontPx = juce::jlimit(8.5f, 12.2f, w * 0.036f);
        const auto titleFont = EngineeredDepthTheme::makeUiFont(titleFontPx, false);
        juce::GlyphArrangement ga;
        ga.addLineOfText(titleFont, config.title, 0.0f, 0.0f);
        const float measuredW = ga.getBoundingBox(0, -1, false).getWidth() * 1.09f;
        const float maxTitleW = juce::jmax(58.0f, w * 0.31f);
        const float labelW = juce::jmin(maxTitleW, measuredW + 4.0f);

        labelBounds = { cursorX, inner.getY(), labelW, h };
        cursorX += labelW + gap;
    }
    else
    {
        labelBounds = {};
    }

    const float comboW = juce::jmax(80.0f, inner.getRight() - cursorX);
    const float comboH = juce::jlimit(24.0f, 36.0f, h * 0.58f);
    const auto comboRect = juce::Rectangle<float>(cursorX,
                                                  inner.getCentreY() - comboH * 0.5f,
                                                  comboW,
                                                  comboH);
    comboBox.setBounds(comboRect.toNearestInt());
}

void EngineeredDepthComboBox::paint(juce::Graphics& g)
{
    const bool enabled = isEnabled() && comboBox.isEnabled();
    const auto state = EngineeredDepthTheme::ControlState {
        comboBox.isMouseOverOrDragging() && enabled,
        comboBox.isPopupActive() && enabled,
        true,
        comboBox.hasKeyboardFocus(true) && enabled,
        enabled
    };
    const auto palette = EngineeredDepthTheme::resolveAccent(config.theme, state);

    if (config.icon != EngineeredDepthIcons::IconId::None && !iconBounds.isEmpty())
    {
        const float stroke = juce::jlimit(1.25f, 1.9f, iconBounds.getHeight() * 0.08f);
        const float glow = enabled ? (state.hovered || state.dragging ? 0.34f : 0.20f) : 0.0f;
        EngineeredDepthIcons::drawIcon(g,
                                       config.icon,
                                       iconBounds,
                                       palette.primary.withAlpha(enabled ? 0.94f : 0.50f),
                                       palette.coreHighlight.withAlpha(enabled ? 0.92f : 0.45f),
                                       stroke,
                                       glow);
    }

    if (config.title.isNotEmpty() && !labelBounds.isEmpty())
    {
        const float titleFontPx = juce::jlimit(8.5f, 12.2f, getWidth() * 0.036f);
        const auto titleFont = EngineeredDepthTheme::makeUiFont(titleFontPx, false);
        const auto textCol = enabled
            ? EngineeredDepthTheme::Colors::textPrimary.withAlpha(0.95f)
            : EngineeredDepthTheme::Colors::disabled;

        if (enabled)
        {
            EngineeredDepthTheme::drawTrackedText(g,
                                                  config.title,
                                                  labelBounds.translated(0.0f, 0.8f),
                                                  titleFont,
                                                  EngineeredDepthTheme::Colors::black.withAlpha(0.60f),
                                                  0.055f,
                                                  juce::Justification::centredLeft);
        }

        EngineeredDepthTheme::drawTrackedText(g,
                                              config.title,
                                              labelBounds,
                                              titleFont,
                                              textCol,
                                              0.055f,
                                              juce::Justification::centredLeft);
    }
}

juce::Font EngineeredDepthComboBox::ComboLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    const float w = static_cast<float>(juce::jmax(1, box.getWidth()));
    const float h = static_cast<float>(juce::jmax(1, box.getHeight()));
    float fontPx = juce::jlimit(8.8f, 13.0f, h * 0.365f);

    const float chevronW = juce::jlimit(20.0f, 30.0f, h * 0.82f);
    const float availTextW = juce::jmax(40.0f, w - chevronW - 14.0f);

    auto font = EngineeredDepthTheme::makeDataFont(fontPx, false);
    const auto text = box.getText();
    if (text.isNotEmpty())
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, text, 0.0f, 0.0f);
        const float textW = ga.getBoundingBox(0, -1, false).getWidth();
        if (textW > availTextW && textW > 1.0f)
        {
            fontPx = juce::jmax(7.5f, fontPx * (availTextW / textW));
            font = EngineeredDepthTheme::makeDataFont(fontPx, false);
        }
    }
    return font;
}

void EngineeredDepthComboBox::ComboLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    const float h = static_cast<float>(box.getHeight());
    const int chevronW = juce::roundToInt(juce::jlimit(20.0f, 30.0f, h * 0.82f));
    label.setBounds(10, 1, juce::jmax(10, box.getWidth() - chevronW - 8), juce::jmax(10, box.getHeight() - 2));
    label.setFont(getComboBoxFont(box));
    label.setJustificationType(juce::Justification::centredLeft);
}

void EngineeredDepthComboBox::ComboLookAndFeel::drawComboBox(juce::Graphics& g,
                                                             int width,
                                                             int height,
                                                             bool isButtonDown,
                                                             int,
                                                             int,
                                                             int,
                                                             int,
                                                             juce::ComboBox& box)
{
    const auto outer = juce::Rectangle<float>(0.0f, 0.0f,
                                              static_cast<float>(width),
                                              static_cast<float>(height)).reduced(1.5f);
    const float radius = juce::jlimit(5.5f, 9.0f, outer.getHeight() * 0.24f);

    const bool enabled   = box.isEnabled();
    const bool popupOpen = enabled && box.isPopupActive();
    const bool hovered   = enabled && (box.isMouseOverOrDragging() || isButtonDown);
    const bool focused   = enabled && box.hasKeyboardFocus(true);

    const auto state = EngineeredDepthTheme::ControlState { hovered, popupOpen, popupOpen, focused, enabled };
    const auto palette = EngineeredDepthTheme::resolveAccent(owner.config.theme, state);

    // 1. Focus outline without shifting capsule geometry
    if (focused)
    {
        EngineeredDepthTheme::drawFocusOutline(g, outer.expanded(1.0f), radius + 1.0f, palette.focus);
    }

    // 2. Dark glass interior gradient
    const auto topFill = !enabled
        ? EngineeredDepthTheme::Colors::bg.withAlpha(0.65f)
        : (hovered || popupOpen
            ? EngineeredDepthTheme::Colors::surface2.interpolatedWith(EngineeredDepthTheme::Colors::surfaceRaised, 0.55f)
            : EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.45f));
    const auto botFill = !enabled
        ? EngineeredDepthTheme::Colors::bg.withAlpha(0.55f)
        : (hovered || popupOpen
            ? EngineeredDepthTheme::Colors::surface1
            : EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::bg, 0.35f));

    juce::ColourGradient fillGrad(topFill, outer.getCentreX(), outer.getY(),
                                  botFill, outer.getCentreX(), outer.getBottom(),
                                  false);
    g.setGradientFill(fillGrad);
    g.fillRoundedRectangle(outer, radius);

    // Subtle bottom floor specular sheen on right side matching master UI reflection
    if (enabled)
    {
        juce::ColourGradient rimGrad(EngineeredDepthTheme::Colors::stroke.withAlpha(hovered ? 0.85f : 0.62f),
                                     outer.getX(), outer.getY(),
                                     (popupOpen ? palette.primary : EngineeredDepthTheme::Colors::metalRimLight)
                                         .withAlpha(popupOpen ? 0.72f : (hovered ? 0.48f : 0.34f)),
                                     outer.getRight(), outer.getBottom(),
                                     false);
        rimGrad.addColour(0.45, EngineeredDepthTheme::Colors::metalRimMid.withAlpha(hovered ? 0.52f : 0.38f));
        g.setGradientFill(rimGrad);
        g.drawRoundedRectangle(outer, radius, hovered || popupOpen ? 1.3f : 1.05f);

        juce::ColourGradient bottomSheen(juce::Colours::transparentBlack,
                                         outer.getX() + outer.getWidth() * 0.28f, outer.getBottom() - 0.6f,
                                         palette.primary.withAlpha(popupOpen ? 0.44f : (hovered ? 0.30f : 0.19f)),
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

    // 3. Cyan ChevronDown icon on right
    const float chevronZoneW = juce::jlimit(20.0f, 30.0f, outer.getHeight() * 0.82f);
    const float iconSize = juce::jlimit(10.0f, 16.0f, outer.getHeight() * 0.42f);
    const auto chevronRect = juce::Rectangle<float>(iconSize, iconSize)
                                 .withCentre({ outer.getRight() - chevronZoneW * 0.52f, outer.getCentreY() });
    const auto chevronCol = enabled
        ? (hovered || popupOpen ? palette.coreHighlight : palette.primary)
        : EngineeredDepthTheme::Colors::disabled;
    const float chevronStroke = juce::jlimit(1.6f, 2.3f, iconSize * 0.16f);

    EngineeredDepthIcons::drawIcon(g,
                                   EngineeredDepthIcons::IconId::ChevronDown,
                                   chevronRect,
                                   chevronCol,
                                   palette.coreHighlight,
                                   chevronStroke,
                                   enabled ? (hovered || popupOpen ? 0.35f : 0.16f) : 0.0f);
}

void EngineeredDepthComboBox::ComboLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    if (label.isBeingEdited())
        return;

    const auto textCol = label.isEnabled()
        ? EngineeredDepthTheme::Colors::textPrimary
        : EngineeredDepthTheme::Colors::disabled;

    g.setFont(label.getFont());
    g.setColour(textCol);
    g.drawFittedText(label.getText(),
                     label.getLocalBounds(),
                     label.getJustificationType(),
                     1,
                     0.82f);
}

void EngineeredDepthComboBox::ComboLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto area = juce::Rectangle<int>(0, 0, width, height).toFloat();
    g.setColour(EngineeredDepthTheme::Colors::bg.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.25f));
    g.fillRect(area);
    g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(0.85f));
    g.drawRect(area.reduced(0.5f), 1.0f);
}

void EngineeredDepthComboBox::ComboLookAndFeel::drawPopupMenuItem(juce::Graphics& g,
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
        const auto r = area.reduced(8, 0).toFloat();
        g.setColour(EngineeredDepthTheme::Colors::strokeSubtle);
        g.drawHorizontalLine(area.getCentreY(), r.getX(), r.getRight());
        return;
    }

    const auto itemBounds = area.reduced(2, 1).toFloat();
    if (isHighlighted && isActive)
    {
        g.setColour(EngineeredDepthTheme::Colors::surfaceRaised);
        g.fillRoundedRectangle(itemBounds, 4.0f);
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.fillRect(itemBounds.getX() + 1.0f, itemBounds.getY() + 4.0f, 2.5f, itemBounds.getHeight() - 8.0f);
    }
    else if (isTicked)
    {
        g.setColour(EngineeredDepthTheme::Colors::surface1.withAlpha(0.70f));
        g.fillRoundedRectangle(itemBounds, 4.0f);
    }

    if (isTicked)
    {
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.fillEllipse(itemBounds.getX() + 8.0f, itemBounds.getCentreY() - 2.5f, 5.0f, 5.0f);
    }

    const auto textColour = !isActive
        ? EngineeredDepthTheme::Colors::disabled
        : (isTicked ? EngineeredDepthTheme::Colors::cyanBright : EngineeredDepthTheme::Colors::textPrimary);

    g.setColour(textColour);
    g.setFont(EngineeredDepthTheme::makeDataFont(11.5f, false));
    g.drawFittedText(text, area.reduced(20, 0), juce::Justification::centredLeft, 1, 0.85f);
}

} // namespace AllyGUI
