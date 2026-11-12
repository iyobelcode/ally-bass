#include "EngineeredDepthTheme.h"

namespace AllyGUI
{

namespace
{
juce::String resolveInstalledFamily(const juce::StringArray& preferred, const juce::String& fallback)
{
    const auto available = juce::Font::findAllTypefaceNames();
    for (const auto& candidate : preferred)
    {
        for (const auto& installed : available)
        {
            if (installed.equalsIgnoreCase(candidate))
                return installed;
        }
    }
    return fallback;
}
} // namespace

EngineeredDepthTheme::AccentPalette EngineeredDepthTheme::resolveAccent(AccentTheme theme, const ControlState& state)
{
    const bool isRed = (theme == AccentTheme::Red);
    const juce::Colour basePrimary = isRed ? Colors::red : Colors::cyan;
    const juce::Colour baseBright  = isRed ? Colors::redBright : Colors::cyanBright;
    const juce::Colour baseDim     = isRed ? Colors::redDim : Colors::cyanDim;

    if (!state.enabled)
    {
        return {
            Colors::disabled,
            Colors::textMuted,
            juce::Colours::transparentBlack,
            Colors::disabled.withAlpha(0.55f),
            Colors::disabled,
            Colors::trackDark.withAlpha(0.50f),
            Colors::disabled.withAlpha(0.60f),
            Colors::disabled
        };
    }

    const float boost = state.dragging ? 0.16f : (state.hovered ? 0.09f : 0.0f);
    const juce::Colour litPrimary = basePrimary.interpolatedWith(baseBright, boost);
    const float maxGlow = isRed ? Metrics::maxRedGlowAlpha : Metrics::maxCyanGlowAlpha;
    const float glowAlpha = state.dragging
        ? (maxGlow * 1.15f)
        : (state.hovered ? (maxGlow * 0.95f) : (state.active ? maxGlow * 0.78f : maxGlow * 0.35f));

    return {
        litPrimary,
        baseBright,
        basePrimary.withAlpha(glowAlpha),
        baseDim,
        Colors::cyanFocus,
        Colors::trackDark,
        litPrimary.withAlpha(state.dragging ? 0.95f : (state.hovered ? 0.85f : 0.72f)),
        litPrimary.interpolatedWith(Colors::textPrimary, state.dragging ? 0.25f : 0.12f)
    };
}

const juce::String& EngineeredDepthTheme::getUiFontFamily()
{
    static const juce::String family = resolveInstalledFamily(
        { "Inter", "SF Pro Text", "SF Pro Display", "Helvetica Neue", "Segoe UI" },
        juce::Font::getDefaultSansSerifFontName());
    return family;
}

const juce::String& EngineeredDepthTheme::getDataFontFamily()
{
    static const juce::String family = resolveInstalledFamily(
        { "JetBrains Mono", "SF Mono", "Menlo", "Consolas" },
        juce::Font::getDefaultMonospacedFontName());
    return family;
}

juce::Font EngineeredDepthTheme::makeUiFont(float pointSize, bool bold)
{
    auto options = juce::FontOptions(getUiFontFamily(), pointSize, bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font(options);
}

juce::Font EngineeredDepthTheme::makeDataFont(float pointSize, bool bold)
{
    auto options = juce::FontOptions(getDataFontFamily(), pointSize, bold ? juce::Font::bold : juce::Font::plain);
    return juce::Font(options);
}

void EngineeredDepthTheme::drawSurfacePanel(juce::Graphics& g,
                                            juce::Rectangle<float> bounds,
                                            float cornerRadius,
                                            bool raised,
                                            bool highlighted)
{
    if (bounds.isEmpty())
        return;

    const auto topCol = raised ? Colors::surfaceRaised : Colors::surface1;
    const auto botCol = raised ? Colors::surface1 : Colors::bg;

    juce::ColourGradient grad(topCol, bounds.getCentreX(), bounds.getY(),
                              botCol, bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(grad);
    g.fillRoundedRectangle(bounds, cornerRadius);

    g.setColour(highlighted ? Colors::cyanFocus.withAlpha(0.65f) : Colors::strokeSubtle);
    g.drawRoundedRectangle(bounds.reduced(0.5f), cornerRadius, Metrics::panelStroke);
}

void EngineeredDepthTheme::drawContourLine(juce::Graphics& g,
                                           juce::Point<float> start,
                                           juce::Point<float> end,
                                           float thickness,
                                           float alpha)
{
    g.setColour(Colors::stroke.withAlpha(alpha));
    g.drawLine(start.x, start.y, end.x, end.y, thickness);
}

void EngineeredDepthTheme::drawAccentPill(juce::Graphics& g,
                                          juce::Rectangle<float> bounds,
                                          juce::Colour accentColour,
                                          float glowAlpha,
                                          bool enabled)
{
    if (bounds.isEmpty())
        return;

    const float radius = bounds.getHeight() * 0.5f;

    if (enabled && glowAlpha > 0.01f)
    {
        g.setColour(accentColour.withAlpha(glowAlpha * 0.35f));
        g.fillRoundedRectangle(bounds.expanded(bounds.getHeight() * 0.85f, bounds.getHeight() * 0.75f),
                               radius + bounds.getHeight() * 0.75f);

        g.setColour(accentColour.withAlpha(glowAlpha * 0.65f));
        g.fillRoundedRectangle(bounds.expanded(bounds.getHeight() * 0.38f, bounds.getHeight() * 0.32f),
                               radius + bounds.getHeight() * 0.32f);
    }

    g.setColour(enabled ? accentColour : Colors::disabled.withAlpha(0.65f));
    g.fillRoundedRectangle(bounds, radius);
}

void EngineeredDepthTheme::drawBracketReadout(juce::Graphics& g,
                                              juce::Rectangle<float> bounds,
                                              const juce::String& text,
                                              const juce::Font& font,
                                              juce::Colour bracketColour,
                                              juce::Colour textColour,
                                              float strokeThickness,
                                              bool highlighted)
{
    if (bounds.getWidth() < 20.0f || bounds.getHeight() < 10.0f)
        return;

    const auto rect = bounds.reduced(1.0f);
    if (highlighted)
    {
        g.setColour(Colors::surface2.withAlpha(0.32f));
        g.fillRoundedRectangle(rect, 3.0f);
    }

    const float arm = juce::jlimit(4.5f, 10.0f, rect.getHeight() * 0.30f);
    const float corner = juce::jmin(2.0f, strokeThickness * 1.4f);

    juce::Path leftBracket;
    leftBracket.startNewSubPath(rect.getX() + arm, rect.getY());
    leftBracket.lineTo(rect.getX() + corner, rect.getY());
    leftBracket.quadraticTo(rect.getX(), rect.getY(), rect.getX(), rect.getY() + corner);
    leftBracket.lineTo(rect.getX(), rect.getBottom() - corner);
    leftBracket.quadraticTo(rect.getX(), rect.getBottom(), rect.getX() + corner, rect.getBottom());
    leftBracket.lineTo(rect.getX() + arm, rect.getBottom());

    juce::Path rightBracket;
    rightBracket.startNewSubPath(rect.getRight() - arm, rect.getY());
    rightBracket.lineTo(rect.getRight() - corner, rect.getY());
    rightBracket.quadraticTo(rect.getRight(), rect.getY(), rect.getRight(), rect.getY() + corner);
    rightBracket.lineTo(rect.getRight(), rect.getBottom() - corner);
    rightBracket.quadraticTo(rect.getRight(), rect.getBottom(), rect.getRight() - corner, rect.getBottom());
    rightBracket.lineTo(rect.getRight() - arm, rect.getBottom());

    const juce::PathStrokeType stroke(strokeThickness, juce::PathStrokeType::curved, juce::PathStrokeType::rounded);
    g.setColour(bracketColour);
    g.strokePath(leftBracket, stroke);
    g.strokePath(rightBracket, stroke);

    g.setFont(font);
    g.setColour(textColour);
    g.drawFittedText(text, rect.reduced(arm * 0.6f, 0.0f).toNearestInt(), juce::Justification::centred, 1);
}

void EngineeredDepthTheme::drawFocusOutline(juce::Graphics& g,
                                            juce::Rectangle<float> bounds,
                                            float cornerRadius,
                                            juce::Colour focusColour)
{
    g.setColour(focusColour.withAlpha(0.70f));
    g.drawRoundedRectangle(bounds.reduced(0.75f), cornerRadius, 1.25f);
}

void EngineeredDepthTheme::drawTrackedText(juce::Graphics& g,
                                           const juce::String& text,
                                           juce::Rectangle<float> bounds,
                                           const juce::Font& font,
                                           juce::Colour colour,
                                           float extraTrackingEm,
                                           juce::Justification justification)
{
    if (text.isEmpty() || bounds.isEmpty())
        return;

    g.setFont(font.withExtraKerningFactor(extraTrackingEm));
    g.setColour(colour);
    g.drawFittedText(text, bounds.toNearestInt(), justification, 1);
}

} // namespace AllyGUI
