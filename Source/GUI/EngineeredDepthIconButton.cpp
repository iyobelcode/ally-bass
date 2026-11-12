#include "EngineeredDepthIconButton.h"

namespace AllyGUI
{

EngineeredDepthIconButton::EngineeredDepthIconButton(EngineeredDepthIcons::IconId iconId,
                                                     const juce::String& componentName,
                                                     const juce::String& tooltipText,
                                                     EngineeredDepthTheme::AccentTheme accentTheme)
    : juce::Button(componentName),
      icon(iconId),
      theme(accentTheme)
{
    setName(componentName);
    setTitle(componentName);
    if (tooltipText.isNotEmpty())
        setTooltip(tooltipText);

    setWantsKeyboardFocus(true);
    setMouseCursor(juce::MouseCursor::PointingHandCursor);
}

void EngineeredDepthIconButton::setIcon(EngineeredDepthIcons::IconId newIcon)
{
    if (icon != newIcon)
    {
        icon = newIcon;
        repaint();
    }
}

void EngineeredDepthIconButton::setIconScale(float scaleFraction) noexcept
{
    iconFraction = juce::jlimit(0.25f, 0.80f, scaleFraction);
    repaint();
}

void EngineeredDepthIconButton::paintButton(juce::Graphics& g,
                                            bool shouldDrawButtonAsHighlighted,
                                            bool shouldDrawButtonAsDown)
{
    const auto bounds = getLocalBounds().toFloat().reduced(1.5f);
    if (bounds.getWidth() < 8.0f || bounds.getHeight() < 8.0f)
        return;

    const bool enabled = isEnabled();
    const bool hovered = enabled && shouldDrawButtonAsHighlighted;
    const bool down    = enabled && shouldDrawButtonAsDown;
    const bool active  = enabled && (getToggleState() || down);
    const bool focused = enabled && hasKeyboardFocus(true);

    const auto state = EngineeredDepthTheme::ControlState { hovered, down, active, focused, enabled };
    const auto palette = EngineeredDepthTheme::resolveAccent(theme, state);
    const float radius = juce::jlimit(5.5f, 9.0f, bounds.getHeight() * 0.24f);

    if (focused)
    {
        EngineeredDepthTheme::drawFocusOutline(g, bounds.expanded(1.0f), radius + 1.0f, palette.focus);
    }

    // Dark brushed metal/glass surface gradient matching master header buttons
    const auto topFill = !enabled
        ? EngineeredDepthTheme::Colors::bg.withAlpha(0.65f)
        : (down
            ? EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.45f)
            : (hovered
                ? EngineeredDepthTheme::Colors::surface2.interpolatedWith(EngineeredDepthTheme::Colors::surfaceRaised, 0.55f)
                : EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.40f)));
    const auto botFill = !enabled
        ? EngineeredDepthTheme::Colors::bg.withAlpha(0.55f)
        : (down
            ? EngineeredDepthTheme::Colors::surface1
            : (hovered
                ? EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::surface2, 0.55f)
                : EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::surface2, 0.25f)));

    juce::ColourGradient fillGrad(topFill, bounds.getCentreX(), bounds.getY(),
                                  botFill, bounds.getCentreX(), bounds.getBottom(),
                                  false);
    g.setGradientFill(fillGrad);
    g.fillRoundedRectangle(bounds, radius);

    // Subtle specular metallic rim + bottom edge sheen
    if (enabled)
    {
        juce::ColourGradient rimGrad(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(hovered ? 0.55f : 0.34f),
                                     bounds.getX(), bounds.getY(),
                                     EngineeredDepthTheme::Colors::stroke.withAlpha(hovered ? 0.85f : 0.60f),
                                     bounds.getRight(), bounds.getBottom(),
                                     false);
        rimGrad.addColour(0.5, (down || active ? palette.primary : EngineeredDepthTheme::Colors::metalRimMid)
                                   .withAlpha(hovered || active ? 0.65f : 0.44f));
        g.setGradientFill(rimGrad);
        g.drawRoundedRectangle(bounds, radius, (hovered || active) ? 1.25f : 1.0f);

        juce::ColourGradient bottomSheen(juce::Colours::transparentBlack,
                                         bounds.getX() + 3.0f, bounds.getBottom() - 0.6f,
                                         palette.primary.withAlpha(down ? 0.55f : (hovered ? 0.42f : 0.22f)),
                                         bounds.getCentreX(), bounds.getBottom() - 0.6f,
                                         false);
        bottomSheen.addColour(0.95, juce::Colours::transparentBlack);
        g.setGradientFill(bottomSheen);
        g.drawLine(bounds.getX() + radius * 0.5f, bounds.getBottom() - 0.6f,
                   bounds.getRight() - radius * 0.5f, bounds.getBottom() - 0.6f, 1.0f);
    }
    else
    {
        g.setColour(EngineeredDepthTheme::Colors::disabled.withAlpha(0.45f));
        g.drawRoundedRectangle(bounds, radius, 1.0f);
    }

    // Centered crisp vector icon
    const float minSide = juce::jmin(bounds.getWidth(), bounds.getHeight());
    const float iconSize = minSide * iconFraction;
    const float pressOffset = down ? 0.6f : 0.0f;
    const auto iconBounds = juce::Rectangle<float>(iconSize, iconSize)
                                .withCentre({ bounds.getCentreX(), bounds.getCentreY() + pressOffset });

    const auto iconCol = !enabled
        ? EngineeredDepthTheme::Colors::disabled
        : (down || getToggleState()
            ? palette.coreHighlight
            : (hovered ? EngineeredDepthTheme::Colors::cyanBright
                       : EngineeredDepthTheme::Colors::textPrimary));
    const auto secondaryCol = enabled ? palette.primary : EngineeredDepthTheme::Colors::disabled;
    const float strokeW = juce::jlimit(1.35f, 2.0f, iconSize * 0.10f);
    const float glow = enabled ? (down ? 0.34f : (hovered ? 0.24f : 0.08f)) : 0.0f;

    EngineeredDepthIcons::drawIcon(g,
                                   icon,
                                   iconBounds,
                                   iconCol,
                                   secondaryCol,
                                   strokeW,
                                   glow);
}

} // namespace AllyGUI
