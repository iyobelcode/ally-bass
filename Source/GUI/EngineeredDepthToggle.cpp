#include "EngineeredDepthToggle.h"

namespace AllyGUI
{

EngineeredDepthToggle::EngineeredDepthToggle(const ToggleConfig& cfg,
                                             juce::AudioProcessorValueTreeState& apvts)
    : button(cfg)
{
    setInterceptsMouseClicks(false, true);

    button.setName(cfg.componentName.isNotEmpty() ? cfg.componentName : cfg.paramId);
    button.setTitle(cfg.title);
    button.setButtonText(cfg.title);
    button.setWantsKeyboardFocus(true);
    button.setMouseCursor(juce::MouseCursor::PointingHandCursor);
    addAndMakeVisible(button);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(
        apvts, cfg.paramId, button);
}

void EngineeredDepthToggle::enablementChanged()
{
    button.setEnabled(isEnabled());
    repaint();
}

void EngineeredDepthToggle::resized()
{
    const auto inner = getLocalBounds().toFloat().reduced(1.0f);
    if (inner.getWidth() < 48.0f || inner.getHeight() < 22.0f)
    {
        button.setBounds(0, 0, 0, 0);
        return;
    }

    const float pillH = juce::jlimit(24.0f, 38.0f, inner.getHeight() * 0.60f);
    const auto buttonRect = juce::Rectangle<float>(inner.getX(),
                                                   inner.getCentreY() - pillH * 0.5f,
                                                   inner.getWidth(),
                                                   pillH);
    button.setBounds(buttonRect.toNearestInt());
}

juce::Rectangle<float> EngineeredDepthToggle::getCapsuleBounds() const noexcept
{
    return button.computeCapsuleBounds().translated(static_cast<float>(button.getX()),
                                                   static_cast<float>(button.getY()));
}

juce::Rectangle<float> EngineeredDepthToggle::getLedBounds() const noexcept
{
    const auto capsule = button.computeCapsuleBounds();
    return button.computeLedBounds(capsule).translated(static_cast<float>(button.getX()),
                                                       static_cast<float>(button.getY()));
}

size_t EngineeredDepthToggle::getEstimatedCacheBytes() const noexcept
{
    return button.getEstimatedCacheBytes();
}

size_t EngineeredDepthToggle::HardwareButton::getEstimatedCacheBytes() const noexcept
{
    return cachedButtonImage.isValid()
        ? static_cast<size_t>(cachedButtonImage.getWidth()) * static_cast<size_t>(cachedButtonImage.getHeight()) * 4u
        : 0u;
}

juce::Rectangle<float> EngineeredDepthToggle::HardwareButton::computeCapsuleBounds() const noexcept
{
    return getLocalBounds().toFloat().reduced(1.5f);
}

juce::Rectangle<float> EngineeredDepthToggle::HardwareButton::computeLedBounds(juce::Rectangle<float> capsule) const noexcept
{
    if (capsule.isEmpty())
        return {};
    const float ledDiam = juce::jlimit(10.0f, 18.0f, capsule.getHeight() * 0.54f);
    const float ledCentreX = capsule.getX() + capsule.getHeight() * 0.52f;
    return juce::Rectangle<float>(ledDiam, ledDiam).withCentre({ ledCentreX, capsule.getCentreY() });
}

void EngineeredDepthToggle::HardwareButton::paintButton(juce::Graphics& g,
                                                        bool shouldDrawButtonAsHighlighted,
                                                        bool shouldDrawButtonAsDown)
{
    const auto capsule = computeCapsuleBounds();
    if (capsule.isEmpty() || getWidth() <= 0 || getHeight() <= 0)
        return;

    const bool enabled = isEnabled();
    const bool isOn    = getToggleState();
    const bool hovered = enabled && shouldDrawButtonAsHighlighted;
    const bool down    = enabled && shouldDrawButtonAsDown;
    const bool focused = enabled && hasKeyboardFocus(true);

    const uint8_t stateMask = static_cast<uint8_t>(
        (enabled ? 1u : 0u)
        | (isOn ? 2u : 0u)
        | (hovered ? 4u : 0u)
        | (down ? 8u : 0u)
        | (focused ? 16u : 0u));

    const float scale = juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int w = getWidth();
    const int h = getHeight();

    if (!cachedButtonImage.isValid()
        || cachedW != w
        || cachedH != h
        || std::abs(scale - cachedScale) > 0.01f
        || cachedStateMask != stateMask)
    {
        const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(w) * scale));
        const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(h) * scale));
        cachedButtonImage = juce::Image(juce::Image::ARGB, rw, rh, true);

        juce::Graphics cacheG(cachedButtonImage);
        cacheG.addTransform(juce::AffineTransform::scale(scale));
        renderButtonToCache(cacheG, hovered, down);

        cachedW = w;
        cachedH = h;
        cachedScale = scale;
        cachedStateMask = stateMask;
    }

    g.drawImage(cachedButtonImage, 0, 0, w, h, 0, 0, cachedButtonImage.getWidth(), cachedButtonImage.getHeight());
}

void EngineeredDepthToggle::HardwareButton::renderButtonToCache(juce::Graphics& g,
                                                                bool shouldDrawButtonAsHighlighted,
                                                                bool shouldDrawButtonAsDown) const
{
    const auto capsule = computeCapsuleBounds();
    if (capsule.isEmpty())
        return;

    const bool enabled = isEnabled();
    const bool isOn    = getToggleState();
    const bool hovered = enabled && shouldDrawButtonAsHighlighted;
    const bool down    = enabled && shouldDrawButtonAsDown;
    const bool focused = enabled && hasKeyboardFocus(true);

    const auto state = EngineeredDepthTheme::ControlState { hovered, down, isOn, focused, enabled };
    const auto palette = EngineeredDepthTheme::resolveAccent(config.theme, state);
    const float radius = juce::jlimit(5.5f, 9.0f, capsule.getHeight() * 0.24f);

    // 1. Focus ring outside border (clean keyboard state indicator)
    if (focused)
    {
        EngineeredDepthTheme::drawFocusOutline(g, capsule.expanded(1.0f), radius + 1.0f, palette.focus);
    }

    // 2. Hardware dark-glass interior body with localized LED spill glow when ON
    {
        juce::Graphics::ScopedSaveState save(g);
        juce::Path clipPath;
        clipPath.addRoundedRectangle(capsule, radius);
        g.reduceClipRegion(clipPath);

        const auto topBg = !enabled
            ? EngineeredDepthTheme::Colors::bg.withAlpha(0.60f)
            : (hovered
                ? EngineeredDepthTheme::Colors::surface2.interpolatedWith(EngineeredDepthTheme::Colors::surfaceRaised, 0.45f)
                : (isOn
                    ? EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.55f)
                    : EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::bg, 0.55f)));

        const auto botBg = !enabled
            ? EngineeredDepthTheme::Colors::bg.withAlpha(0.50f)
            : (hovered
                ? EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::surface2, 0.25f)
                : (isOn
                    ? EngineeredDepthTheme::Colors::surface1
                    : EngineeredDepthTheme::Colors::surface1.darker(0.45f)));

        juce::ColourGradient bodyGrad(topBg, capsule.getCentreX(), capsule.getY(),
                                      botBg, capsule.getCentreX(), capsule.getBottom(),
                                      false);
        g.setGradientFill(bodyGrad);
        g.fillRect(capsule);

        const auto ledBounds = computeLedBounds(capsule);
        if (enabled && isOn)
        {
            const float auraR = capsule.getHeight() * 1.45f;
            juce::ColourGradient ledAura(palette.primary.withAlpha(hovered ? 0.30f : 0.22f),
                                         ledBounds.getCentreX(), ledBounds.getCentreY(),
                                         juce::Colours::transparentBlack,
                                         ledBounds.getCentreX() + auraR, ledBounds.getCentreY(),
                                         true);
            g.setGradientFill(ledAura);
            g.fillEllipse(ledBounds.getCentreX() - auraR,
                          ledBounds.getCentreY() - auraR,
                          auraR * 2.0f,
                          auraR * 2.0f);
        }

        // Delicate upper metallic bezel catch sheen inside button
        if (enabled)
        {
            juce::ColourGradient topSpec(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(isOn ? 0.16f : 0.08f),
                                         capsule.getCentreX(), capsule.getY(),
                                         juce::Colours::transparentBlack,
                                         capsule.getCentreX(), capsule.getY() + capsule.getHeight() * 0.26f,
                                         false);
            g.setGradientFill(topSpec);
            g.fillRect(capsule);
        }
    }

    // 3. Machined bezel rim with left-side accent contour when ON (matching approved master toggles)
    if (!enabled)
    {
        g.setColour(EngineeredDepthTheme::Colors::disabled.withAlpha(0.48f));
        g.drawRoundedRectangle(capsule, radius, 1.0f);
    }
    else if (isOn)
    {
        // Keep cool machined steel rim on 036 OVERDRIVE (matching master) and cyan left rim on 1300 MONO LOCK
        const auto leftAccentCol = (config.theme == EngineeredDepthTheme::AccentTheme::Red)
            ? EngineeredDepthTheme::Colors::metalRimMid.interpolatedWith(EngineeredDepthTheme::Colors::metalRimLight, 0.42f)
            : palette.primary.interpolatedWith(EngineeredDepthTheme::Colors::metalRimLight, 0.14f);

        juce::ColourGradient borderGrad(leftAccentCol.withAlpha(hovered ? 0.90f : 0.78f),
                                        capsule.getX(), capsule.getCentreY(),
                                        EngineeredDepthTheme::Colors::stroke.withAlpha(hovered ? 0.85f : 0.62f),
                                        capsule.getRight(), capsule.getCentreY(),
                                        false);
        borderGrad.addColour(0.36, EngineeredDepthTheme::Colors::metalRimMid.withAlpha(hovered ? 0.72f : 0.56f));
        g.setGradientFill(borderGrad);
        g.drawRoundedRectangle(capsule, radius, hovered ? 1.35f : 1.15f);
    }
    else
    {
        g.setColour(hovered
                        ? EngineeredDepthTheme::Colors::stroke.withAlpha(0.95f)
                        : EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.90f));
        g.drawRoundedRectangle(capsule, radius, hovered ? 1.25f : 1.0f);
    }

    // 4. Concentric LED emitter on left
    const auto ledRect = computeLedBounds(capsule).translated(down ? 0.3f : 0.0f, down ? 0.4f : 0.0f);
    if (enabled && isOn)
    {
        const float outerGlowR = ledRect.getWidth() * 0.78f;
        juce::ColourGradient outerGlow(palette.primary.withAlpha(hovered ? 0.46f : 0.34f),
                                       ledRect.getCentreX(), ledRect.getCentreY(),
                                       juce::Colours::transparentBlack,
                                       ledRect.getCentreX() + outerGlowR, ledRect.getCentreY(),
                                       true);
        g.setGradientFill(outerGlow);
        g.fillEllipse(ledRect.getCentreX() - outerGlowR,
                      ledRect.getCentreY() - outerGlowR,
                      outerGlowR * 2.0f,
                      outerGlowR * 2.0f);

        // Sculpted dark recessed metal collar + bright halo ring + luminous jewel dome
        g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.95f));
        g.fillEllipse(ledRect.expanded(1.2f));
        g.setColour(EngineeredDepthTheme::Colors::metalRimMid.withAlpha(0.65f));
        g.drawEllipse(ledRect.expanded(1.0f), 0.9f);

        g.setColour(palette.primary.withAlpha(0.94f));
        g.drawEllipse(ledRect.reduced(0.6f), juce::jmax(1.2f, ledRect.getWidth() * 0.095f));

        const auto coreRect = ledRect.reduced(ledRect.getWidth() * 0.18f);
        juce::ColourGradient coreGrad(EngineeredDepthTheme::Colors::textPrimary,
                                      coreRect.getCentreX() - coreRect.getWidth() * 0.12f,
                                      coreRect.getCentreY() - coreRect.getHeight() * 0.12f,
                                      palette.primary,
                                      coreRect.getRight(), coreRect.getCentreY(),
                                      true);
        coreGrad.addColour(0.36, palette.coreHighlight);
        g.setGradientFill(coreGrad);
        g.fillEllipse(coreRect);
    }
    else
    {
        // Unlit matte LED lens clearly distinct from illuminated ON state
        g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(enabled ? 0.90f : 0.50f));
        g.fillEllipse(ledRect);

        g.setColour((enabled ? EngineeredDepthTheme::Colors::strokeSubtle : EngineeredDepthTheme::Colors::disabled)
                        .withAlpha(enabled ? 0.85f : 0.45f));
        g.drawEllipse(ledRect.reduced(0.6f), 1.0f);

        const auto darkLens = ledRect.reduced(ledRect.getWidth() * 0.24f);
        const auto dimTint = (!enabled && isOn)
            ? (config.theme == EngineeredDepthTheme::AccentTheme::Red
                   ? EngineeredDepthTheme::Colors::redDim.withAlpha(0.45f)
                   : EngineeredDepthTheme::Colors::cyanDim.withAlpha(0.42f))
            : EngineeredDepthTheme::Colors::surface1.withAlpha(enabled ? 0.52f : 0.26f);
        g.setColour(dimTint);
        g.fillEllipse(darkLens);

        if (!enabled && isOn)
        {
            g.setColour((config.theme == EngineeredDepthTheme::AccentTheme::Red
                             ? EngineeredDepthTheme::Colors::redDim
                             : EngineeredDepthTheme::Colors::cyanDim).withAlpha(0.42f));
            g.drawEllipse(ledRect.reduced(0.6f), 1.0f);
        }

        if (enabled)
        {
            g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(hovered ? 0.24f : 0.12f));
            g.fillEllipse(darkLens.getCentreX() - darkLens.getWidth() * 0.18f,
                          darkLens.getCentreY() - darkLens.getHeight() * 0.20f,
                          darkLens.getWidth() * 0.30f,
                          darkLens.getHeight() * 0.26f);
        }
    }

    // 5. Crisp data-monospace toggle label to the right of LED
    const float textStartX = ledRect.getRight() + juce::jlimit(6.0f, 12.0f, capsule.getHeight() * 0.22f);
    const auto textRect = juce::Rectangle<float>(textStartX,
                                                 capsule.getY(),
                                                 juce::jmax(10.0f, capsule.getRight() - textStartX - 6.0f),
                                                 capsule.getHeight())
                              .translated(down ? 0.3f : 0.0f, down ? 0.4f : 0.0f);

    float fontPx = juce::jlimit(8.8f, 13.5f, capsule.getHeight() * 0.365f);
    auto font = EngineeredDepthTheme::makeDataFont(fontPx, false);
    const auto text = config.title;
    if (text.isNotEmpty())
    {
        juce::GlyphArrangement ga;
        ga.addLineOfText(font, text, 0.0f, 0.0f);
        const float textW = ga.getBoundingBox(0, -1, false).getWidth() * 1.04f;
        if (textW > textRect.getWidth() && textW > 1.0f)
        {
            fontPx = juce::jmax(8.0f, fontPx * (textRect.getWidth() / textW));
            font = EngineeredDepthTheme::makeDataFont(fontPx, false);
        }
    }

    const auto textCol = !enabled
        ? EngineeredDepthTheme::Colors::disabled
        : (isOn
            ? EngineeredDepthTheme::Colors::textPrimary
            : (hovered ? EngineeredDepthTheme::Colors::textPrimary.withAlpha(0.90f)
                       : EngineeredDepthTheme::Colors::textSecondary.withAlpha(0.88f)));

    if (enabled && isOn)
    {
        EngineeredDepthTheme::drawTrackedText(g,
                                              text,
                                              textRect.translated(0.0f, 0.7f),
                                              font,
                                              EngineeredDepthTheme::Colors::black.withAlpha(0.65f),
                                              0.03f,
                                              juce::Justification::centredLeft);
    }

    EngineeredDepthTheme::drawTrackedText(g,
                                          text,
                                          textRect,
                                          font,
                                          textCol,
                                          0.03f,
                                          juce::Justification::centredLeft);
}

} // namespace AllyGUI
