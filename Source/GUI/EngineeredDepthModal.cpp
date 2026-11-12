#include "EngineeredDepthModal.h"

namespace AllyGUI
{

EngineeredDepthActionButton::EngineeredDepthActionButton(const juce::String& labelText,
                                                         Style style,
                                                         const juce::String& componentName)
    : juce::Button(labelText),
      buttonStyle(style)
{
    setButtonText(labelText);
    setName(componentName.isNotEmpty() ? componentName : labelText);
    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void EngineeredDepthActionButton::setStyle(Style newStyle) noexcept
{
    if (buttonStyle != newStyle)
    {
        buttonStyle = newStyle;
        repaint();
    }
}

void EngineeredDepthActionButton::paintButton(juce::Graphics& g,
                                              bool shouldDrawButtonAsHighlighted,
                                              bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.0f);
    if (bounds.getWidth() < 8.0f || bounds.getHeight() < 8.0f)
        return;

    const bool enabled = isEnabled();
    const bool hovered = enabled && shouldDrawButtonAsHighlighted;
    const bool down    = enabled && shouldDrawButtonAsDown;
    const bool focused = enabled && hasKeyboardFocus(true);

    const auto accentTheme = (buttonStyle == Style::Warning)
        ? EngineeredDepthTheme::AccentTheme::Red
        : EngineeredDepthTheme::AccentTheme::Cyan;

    const auto state = EngineeredDepthTheme::ControlState {
        hovered, down, buttonStyle != Style::Secondary, focused, enabled
    };
    const auto palette = EngineeredDepthTheme::resolveAccent(accentTheme, state);
    const float radius = 6.0f;

    if (focused)
    {
        EngineeredDepthTheme::drawFocusOutline(g, bounds.expanded(1.0f), radius + 1.0f, palette.focus);
    }

    juce::Colour topCol = EngineeredDepthTheme::Colors::surface1;
    juce::Colour botCol = EngineeredDepthTheme::Colors::bg;
    juce::Colour rimCol = EngineeredDepthTheme::Colors::stroke.withAlpha(0.75f);
    juce::Colour textCol = EngineeredDepthTheme::Colors::textPrimary;

    if (!enabled)
    {
        topCol = EngineeredDepthTheme::Colors::bg.withAlpha(0.65f);
        botCol = topCol;
        rimCol = EngineeredDepthTheme::Colors::disabled.withAlpha(0.4f);
        textCol = EngineeredDepthTheme::Colors::disabled;
    }
    else if (buttonStyle == Style::Primary)
    {
        topCol = EngineeredDepthTheme::Colors::surface2.overlaidWith(
            EngineeredDepthTheme::Colors::cyan.withAlpha(down ? 0.30f : (hovered ? 0.25f : 0.16f)));
        botCol = EngineeredDepthTheme::Colors::surface1;
        rimCol = hovered || down ? EngineeredDepthTheme::Colors::cyanBright : EngineeredDepthTheme::Colors::cyan;
        textCol = EngineeredDepthTheme::Colors::cyanBright;
    }
    else if (buttonStyle == Style::Warning)
    {
        topCol = EngineeredDepthTheme::Colors::surface2.overlaidWith(
            EngineeredDepthTheme::Colors::red.withAlpha(down ? 0.34f : (hovered ? 0.28f : 0.19f)));
        botCol = EngineeredDepthTheme::Colors::surface1;
        rimCol = hovered || down ? EngineeredDepthTheme::Colors::redBright : EngineeredDepthTheme::Colors::red;
        textCol = EngineeredDepthTheme::Colors::redBright;
    }
    else
    {
        topCol = hovered
            ? EngineeredDepthTheme::Colors::surfaceRaised
            : EngineeredDepthTheme::Colors::surface2;
        botCol = down
            ? EngineeredDepthTheme::Colors::black
            : EngineeredDepthTheme::Colors::surface1;
        rimCol = hovered
            ? EngineeredDepthTheme::Colors::cyanDim
            : EngineeredDepthTheme::Colors::stroke;
        textCol = hovered
            ? EngineeredDepthTheme::Colors::cyanBright
            : EngineeredDepthTheme::Colors::textPrimary;
    }

    juce::ColourGradient grad(topCol, bounds.getCentreX(), bounds.getY(),
                              botCol, bounds.getCentreX(), bounds.getBottom(),
                              false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(rimCol);
    g.drawRoundedRectangle(bounds, radius, (buttonStyle != Style::Secondary || hovered) ? 1.25f : 1.0f);

    const float fontPx = juce::jlimit(9.5f, 12.0f, bounds.getHeight() * 0.44f);
    g.setFont(EngineeredDepthTheme::makeUiFont(fontPx, true));
    g.setColour(textCol);
    g.drawFittedText(getButtonText(),
                     bounds.reduced(8.0f, 2.0f).toNearestInt(),
                     juce::Justification::centred,
                     1,
                     0.85f);
}

// =============================================================================

EngineeredDepthModalOverlay::EngineeredDepthModalOverlay(const juce::String& titleText,
                                                         const juce::String& subtitleText)
    : title(titleText),
      subtitle(subtitleText)
{
    setName("EngineeredDepthModalOverlay");
    setWantsKeyboardFocus(true);
    setInterceptsMouseClicks(true, true);

    closeButton.setTooltip("Sluiten (Esc)");
    closeButton.onClick = [this]() { closeModal(); };
    addAndMakeVisible(closeButton);
}

void EngineeredDepthModalOverlay::setTitleText(const juce::String& titleText, const juce::String& subtitleText)
{
    title = titleText;
    subtitle = subtitleText;
    repaint();
}

void EngineeredDepthModalOverlay::openModal()
{
    setVisible(true);
    toFront(true);
    onModalOpened();
    resized();
    repaint();
}

void EngineeredDepthModalOverlay::closeModal()
{
    if (!isVisible())
        return;

    setVisible(false);
    if (onDismissRequested)
        onDismissRequested();
}

juce::Point<int> EngineeredDepthModalOverlay::computePreferredPanelSize(int hostWidth, int hostHeight) const
{
    return { juce::jlimit(320, hostWidth - 40, 500),
             juce::jlimit(220, hostHeight - 40, 320) };
}

void EngineeredDepthModalOverlay::paint(juce::Graphics& g)
{
    // Dimmed abyssal backdrop
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.76f));
    g.fillRect(getLocalBounds());

    if (panelBounds.isEmpty())
        return;

    const auto pf = panelBounds.toFloat();
    const float chamfer = 12.0f;
    const auto chassisPath = EngineeredDepthTheme::buildChamferedRectPath(pf, chamfer, chamfer);

    // Outer ambient shadow
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.65f));
    g.fillPath(EngineeredDepthTheme::buildChamferedRectPath(pf.translated(0.0f, 4.0f), chamfer, chamfer));

    // Metallic dark surface gradient
    juce::ColourGradient bgGrad(EngineeredDepthTheme::Colors::surface2.interpolatedWith(EngineeredDepthTheme::Colors::bg, 0.25f),
                                pf.getCentreX(), pf.getY(),
                                EngineeredDepthTheme::Colors::bg.interpolatedWith(EngineeredDepthTheme::Colors::black, 0.35f),
                                pf.getCentreX(), pf.getBottom(),
                                false);
    g.setGradientFill(bgGrad);
    g.fillPath(chassisPath);

    // Subtle header top tint
    if (!headerBounds.isEmpty())
    {
        g.saveState();
        g.reduceClipRegion(chassisPath);
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.06f));
        g.fillRect(headerBounds);
        g.restoreState();
    }

    // Metallic steel + cyan border
    juce::ColourGradient borderGrad(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.62f),
                                    pf.getX(), pf.getY(),
                                    EngineeredDepthTheme::Colors::cyan.withAlpha(0.45f),
                                    pf.getRight(), pf.getBottom(),
                                    false);
    g.setGradientFill(borderGrad);
    g.strokePath(chassisPath, juce::PathStrokeType(1.35f));

    // Inner divider below header
    const float headerBottomY = static_cast<float>(headerBounds.getBottom());
    EngineeredDepthTheme::drawContourLine(g,
                                          { pf.getX() + 14.0f, headerBottomY },
                                          { pf.getRight() - 14.0f, headerBottomY },
                                          1.0f,
                                          0.65f);

    // Title & subtitle
    auto textRect = headerBounds.reduced(16, 4);
    textRect.setRight(closeButton.getX() - 8);

    const float titlePx = juce::jlimit(12.0f, 15.5f, pf.getWidth() * 0.029f);
    g.setColour(EngineeredDepthTheme::Colors::textPrimary);
    g.setFont(EngineeredDepthTheme::makeUiFont(titlePx, true));
    if (subtitle.isEmpty())
    {
        g.drawFittedText(title, textRect, juce::Justification::centredLeft, 1, 0.85f);
    }
    else
    {
        const int halfH = textRect.getHeight() / 2;
        g.drawFittedText(title, textRect.removeFromTop(halfH + 2), juce::Justification::bottomLeft, 1, 0.85f);
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.setFont(EngineeredDepthTheme::makeDataFont(juce::jmax(9.5f, titlePx * 0.72f), false));
        g.drawFittedText(subtitle, textRect, juce::Justification::topLeft, 1, 0.85f);
    }
}

void EngineeredDepthModalOverlay::resized()
{
    const int w = getWidth();
    const int h = getHeight();
    if (w < 100 || h < 100)
        return;

    const auto pref = computePreferredPanelSize(w, h);
    const int panelW = juce::jmin(w - 28, pref.x);
    const int panelH = juce::jmin(h - 24, pref.y);
    panelBounds = juce::Rectangle<int>(panelW, panelH).withCentre(getLocalBounds().getCentre());

    const int hdrH = juce::jlimit(38, 48, juce::roundToInt(static_cast<float>(panelH) * 0.16f));
    headerBounds = panelBounds.withHeight(hdrH);
    const int btnSize = juce::jlimit(22, 28, hdrH - 14);
    closeButton.setBounds(headerBounds.getRight() - btnSize - 12,
                          headerBounds.getCentreY() - btnSize / 2,
                          btnSize,
                          btnSize);

    contentBounds = panelBounds.withTrimmedTop(hdrH).reduced(18, 12);
    layoutModalContent(contentBounds);
}

void EngineeredDepthModalOverlay::mouseUp(const juce::MouseEvent& event)
{
    if (!panelBounds.contains(event.getPosition()))
        closeModal();
}

bool EngineeredDepthModalOverlay::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        closeModal();
        return true;
    }
    return false;
}

} // namespace AllyGUI
