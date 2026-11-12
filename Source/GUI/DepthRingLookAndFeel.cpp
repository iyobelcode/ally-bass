#include "DepthRingLookAndFeel.h"

namespace AllyGUI
{

void DepthRingLookAndFeel::configureSlider(juce::Slider& slider,
                                           EngineeredDepthTheme::AccentTheme theme,
                                           EngineeredDepthIcons::IconId icon,
                                           bool compact)
{
    slider.getProperties().set(themePropertyId, static_cast<int>(theme));
    slider.getProperties().set(iconPropertyId, static_cast<int>(icon));
    slider.getProperties().set(compactPropertyId, compact);
}

EngineeredDepthTheme::ControlState DepthRingLookAndFeel::inspectSliderState(const juce::Slider& slider,
                                                                              float normalizedValue)
{
    EngineeredDepthTheme::ControlState state;
    state.enabled  = slider.isEnabled();
    state.hovered  = state.enabled && slider.isMouseOverOrDragging();
    state.dragging = state.enabled && slider.isMouseButtonDown();
    state.focused  = state.enabled && slider.hasKeyboardFocus(true);
    state.active   = state.enabled && normalizedValue > 0.002f;
    return state;
}

void DepthRingLookAndFeel::drawRotarySlider(juce::Graphics& g,
                                            int x,
                                            int y,
                                            int width,
                                            int height,
                                            float sliderPosProportional,
                                            float rotaryStartAngle,
                                            float rotaryEndAngle,
                                            juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float diameter = juce::jmin(bounds.getWidth(), bounds.getHeight());
    if (diameter < 24.0f)
        return;

    const auto centre = bounds.getCentre();
    const auto themeVal = static_cast<int>(slider.getProperties().getWithDefault(
        themePropertyId, static_cast<int>(EngineeredDepthTheme::AccentTheme::Cyan)));
    const auto iconVal = static_cast<int>(slider.getProperties().getWithDefault(
        iconPropertyId, static_cast<int>(EngineeredDepthIcons::IconId::None)));

    const auto theme = static_cast<EngineeredDepthTheme::AccentTheme>(themeVal);
    const auto icon  = static_cast<EngineeredDepthIcons::IconId>(iconVal);
    const bool isCompact = static_cast<bool>(slider.getProperties().getWithDefault(compactPropertyId, false));
    const float norm = juce::jlimit(0.0f, 1.0f, sliderPosProportional);
    const auto state = inspectSliderState(slider, norm);
    const auto palette = EngineeredDepthTheme::resolveAccent(theme, state);

    const float currentAngle = rotaryStartAngle + norm * (rotaryEndAngle - rotaryStartAngle);

    // Layer radii calibrated to ALLY1300_APPROVED_MASTER_FULL.png proportions
    const float rTickOuterMajor = 0.488f * diameter;
    const float rTickOuterMinor = 0.468f * diameter;
    const float rTickInner      = 0.450f * diameter;
    const float rWell           = (isCompact ? 0.482f : 0.440f) * diameter;
    const float rArc            = (isCompact ? 0.436f : 0.422f) * diameter;
    const float rMetalOuter     = (isCompact ? 0.398f : 0.404f) * diameter;
    const float rBevelInner     = (isCompact ? 0.348f : 0.366f) * diameter;
    const float rDishOuter      = (isCompact ? 0.343f : 0.363f) * diameter;
    const float rCapInner       = (isCompact ? 0.242f : 0.268f) * diameter;
    const float rPointerOuter   = 0.382f * diameter;
    const float rPointerInner   = 0.145f * diameter;

    const float arcStroke = isCompact
        ? juce::jmax(2.2f, 0.088f * diameter)
        : juce::jmax(2.6f, 0.031f * diameter);

    // 1. Dark circular recessed well behind arc and metallic bezel (subtle machined edge, no black void)
    g.setColour(EngineeredDepthTheme::Colors::dialWell);
    g.fillEllipse(centre.x - rWell, centre.y - rWell, rWell * 2.0f, rWell * 2.0f);
    g.setColour(EngineeredDepthTheme::Colors::trackDark.withAlpha(0.45f));
    g.drawEllipse(centre.x - rWell, centre.y - rWell, rWell * 2.0f, rWell * 2.0f,
                  juce::jmax(0.8f, (isCompact ? 0.014f : 0.0045f) * diameter));

    // 2. Outer technical scale ticks: 24 intervals on full rings; omitted on Mini Depth Rings for clarity
    if (!isCompact)
    {
        constexpr int numSteps = 24;
        const float majorTickWidth = juce::jmax(1.1f, 0.0085f * diameter);
        const float minorTickWidth = juce::jmax(0.8f, 0.0055f * diameter);

        for (int i = 1; i < numSteps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(numSteps);
            const float angle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const bool isMajor = (i % 2 == 0);
            const bool underActiveArc = state.active && (angle <= currentAngle + 0.015f);

            const float sinA = std::sin(angle);
            const float cosA = -std::cos(angle);
            const float outerR = isMajor ? rTickOuterMajor : rTickOuterMinor;

            const float x1 = centre.x + rTickInner * sinA;
            const float y1 = centre.y + rTickInner * cosA;
            const float x2 = centre.x + outerR * sinA;
            const float y2 = centre.y + outerR * cosA;

            juce::Colour tickCol;
            if (!state.enabled)
            {
                tickCol = EngineeredDepthTheme::Colors::disabled.withAlpha(isMajor ? 0.50f : 0.28f);
            }
            else if (isMajor)
            {
                const float hoverLift = (state.hovered || state.dragging) ? 0.12f : 0.0f;
                const bool isCardinal9 = (i == 4);
                if (isCardinal9 && underActiveArc)
                    tickCol = palette.primary.withAlpha(0.90f);
                else
                    tickCol = EngineeredDepthTheme::Colors::tickMajor.withAlpha(juce::jmin(1.0f, 0.84f + hoverLift));
            }
            else
            {
                tickCol = underActiveArc
                    ? palette.dim.withAlpha(state.hovered ? 0.78f : 0.62f)
                    : EngineeredDepthTheme::Colors::tickMinorDim.withAlpha(state.hovered ? 0.85f : 0.65f);
            }

            g.setColour(tickCol);
            g.drawLine(x1, y1, x2, y2, isMajor ? majorTickWidth : minorTickWidth);
        }
    }

    // 3. Inactive arc track groove
    {
        juce::Path trackPath;
        trackPath.addCentredArc(centre.x, centre.y, rArc, rArc, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour(palette.trackInactive);
        g.strokePath(trackPath,
                     juce::PathStrokeType(arcStroke * 0.90f,
                                          juce::PathStrokeType::curved,
                                          juce::PathStrokeType::butt));
    }

    // 4. Active parameter arc + soft bloom
    if (state.active && currentAngle > rotaryStartAngle + 0.01f)
    {
        juce::Path activePath;
        activePath.addCentredArc(centre.x, centre.y, rArc, rArc, 0.0f, rotaryStartAngle, currentAngle, true);

        if (palette.glow.getAlpha() > 0)
        {
            g.setColour(palette.glow);
            g.strokePath(activePath,
                         juce::PathStrokeType(arcStroke * 1.95f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::butt));

            g.setColour(palette.primary.withAlpha(0.28f));
            g.strokePath(activePath,
                         juce::PathStrokeType(arcStroke * 1.35f,
                                              juce::PathStrokeType::curved,
                                              juce::PathStrokeType::butt));
        }

        g.setColour(palette.primary);
        g.strokePath(activePath,
                     juce::PathStrokeType(arcStroke,
                                          juce::PathStrokeType::curved,
                                          juce::PathStrokeType::butt));
    }

    // 5. Outer anodized metallic bevel collar
    {
        const auto rimBounds = juce::Rectangle<float>(centre.x - rMetalOuter,
                                                      centre.y - rMetalOuter,
                                                      rMetalOuter * 2.0f,
                                                      rMetalOuter * 2.0f);

        const float rimTopMix = state.hovered ? 0.72f : 0.56f;
        const auto steelTop = EngineeredDepthTheme::Colors::metalRimMid
                                  .interpolatedWith(EngineeredDepthTheme::Colors::metalRimLight, rimTopMix)
                                  .interpolatedWith(palette.primary, state.active ? 0.07f : 0.02f);

        juce::ColourGradient bevelGrad(
            steelTop,
            centre.x, centre.y - rMetalOuter,
            EngineeredDepthTheme::Colors::metalRimDark.interpolatedWith(EngineeredDepthTheme::Colors::metalRimMid, 0.32f),
            centre.x, centre.y + rMetalOuter * 0.90f,
            false);
        bevelGrad.addColour(0.28, EngineeredDepthTheme::Colors::metalRimMid.interpolatedWith(EngineeredDepthTheme::Colors::metalRimLight, 0.18f));
        bevelGrad.addColour(0.60, EngineeredDepthTheme::Colors::bg);
        g.setGradientFill(bevelGrad);
        g.fillEllipse(rimBounds);

        // Soft machined outer contour so bottom bevel integrates with track groove
        const float rimStroke = isCompact
            ? juce::jmax(0.85f, 0.018f * diameter)
            : juce::jmax(1.0f, 0.0070f * diameter);
        g.setColour(EngineeredDepthTheme::Colors::metalRimMid.withAlpha(0.32f));
        g.drawEllipse(rimBounds.reduced(rimStroke * 0.4f), rimStroke * 0.85f);

        // Broadened, brighter top steel-blue specular rim sheen
        juce::Path topSoftSpec;
        topSoftSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.9f, rMetalOuter - rimStroke * 0.9f,
                                  0.0f, -1.24f, 1.24f, true);
        g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(state.hovered ? 0.30f : 0.22f));
        g.strokePath(topSoftSpec, juce::PathStrokeType(rimStroke * 2.1f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        juce::Path topSpec;
        topSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.5f, rMetalOuter - rimStroke * 0.5f,
                              0.0f, -1.12f, 1.12f, true);
        g.setColour(EngineeredDepthTheme::Colors::metalRimLight.interpolatedWith(EngineeredDepthTheme::Colors::textPrimary, 0.20f)
                        .withAlpha(state.hovered ? 0.62f : 0.48f));
        g.strokePath(topSpec, juce::PathStrokeType(rimStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Subtle strengthened lower-right rim catch-light (~3:30-5:15) matching machined metal bevel
        juce::Path lowerSpec;
        lowerSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.5f, rMetalOuter - rimStroke * 0.5f,
                                0.0f, 1.85f, 2.75f, true);
        g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(state.hovered ? 0.48f : 0.38f));
        g.strokePath(lowerSpec, juce::PathStrokeType(rimStroke * 0.95f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // 6. Dark recessed ring between outer bezel and dish + inner dial cap
    {
        const auto bevelShadow = juce::Rectangle<float>(centre.x - rBevelInner,
                                                        centre.y - rBevelInner,
                                                        rBevelInner * 2.0f,
                                                        rBevelInner * 2.0f);
        g.setColour(EngineeredDepthTheme::Colors::black);
        g.fillEllipse(bevelShadow);

        const auto dishBounds = juce::Rectangle<float>(centre.x - rDishOuter,
                                                       centre.y - rDishOuter,
                                                       rDishOuter * 2.0f,
                                                       rDishOuter * 2.0f);

        const auto dishTopCol = EngineeredDepthTheme::Colors::dialFaceTop
                                    .interpolatedWith(EngineeredDepthTheme::Colors::metalRimLight,
                                                      state.hovered ? 0.24f : 0.14f)
                                    .interpolatedWith(palette.primary, state.active ? 0.06f : 0.0f);
        juce::ColourGradient dishGrad(
            dishTopCol,
            centre.x + rDishOuter * 0.02f, centre.y - rDishOuter * 0.42f,
            EngineeredDepthTheme::Colors::dialFaceBot,
            centre.x, centre.y + rDishOuter * 0.88f,
            true);
        dishGrad.addColour(0.46, EngineeredDepthTheme::Colors::dialFaceMid);
        g.setGradientFill(dishGrad);
        g.fillEllipse(dishBounds);

        // Delicate upper bevel highlight on dish edge (softened on Mini Depth Rings for sleek dark center)
        const float dishStrokeAlpha = isCompact ? (state.hovered ? 0.28f : 0.16f) : (state.hovered ? 0.44f : 0.28f);
        g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(dishStrokeAlpha));
        g.drawEllipse(dishBounds.reduced(0.5f), juce::jmax(0.8f, 0.0055f * diameter));

        // Inner circular step cap
        const auto capBounds = juce::Rectangle<float>(centre.x - rCapInner,
                                                      centre.y - rCapInner,
                                                      rCapInner * 2.0f,
                                                      rCapInner * 2.0f);
        juce::ColourGradient capGrad(EngineeredDepthTheme::Colors::capFillTop,
                                     centre.x, centre.y - rCapInner,
                                     EngineeredDepthTheme::Colors::capFillBot,
                                     centre.x, centre.y + rCapInner,
                                     false);
        g.setGradientFill(capGrad);
        g.fillEllipse(capBounds);

        const float capStrokeAlpha = isCompact ? (state.hovered ? 0.36f : 0.22f) : (state.hovered ? 0.84f : 0.62f);
        g.setColour(EngineeredDepthTheme::Colors::capStroke.withAlpha(capStrokeAlpha));
        g.drawEllipse(capBounds, juce::jmax(0.8f, 0.005f * diameter));
    }

    // 7. Central vector icon (36-40% cap span, raised 1-2px vs C.1 while preserving needle clearance)
    if (icon != EngineeredDepthIcons::IconId::None)
    {
        const auto iconBounds = juce::Rectangle<float>(0.228f * diameter, 0.180f * diameter)
                                    .withCentre({ centre.x, centre.y + 0.010f * diameter });
        const float iconStroke = juce::jmax(1.0f, 0.0072f * diameter);
        const float iconGlow   = state.enabled ? (state.dragging ? 0.30f : (state.hovered ? 0.24f : 0.15f)) : 0.0f;

        // Per approved master (ALLY1300_APPROVED_MASTER_FULL.png): Haven Dirt harbor icon stays cyan/ice-cyan while active
        const bool keepCoolHarborCyan = (icon == EngineeredDepthIcons::IconId::HavenDirt && state.enabled);
        const auto iconPrimary = keepCoolHarborCyan
            ? EngineeredDepthTheme::Colors::cyan
            : palette.primary.withAlpha(state.enabled ? 0.94f : 0.55f);
        const auto iconSecondary = keepCoolHarborCyan
            ? EngineeredDepthTheme::Colors::cyanBright.interpolatedWith(EngineeredDepthTheme::Colors::cyan, 0.28f)
            : palette.coreHighlight.withAlpha(state.enabled ? 0.90f : 0.45f);

        EngineeredDepthIcons::drawIcon(g,
                                       icon,
                                       iconBounds,
                                       iconPrimary,
                                       iconSecondary,
                                       iconStroke,
                                       iconGlow);
    }

    // 8. Luminous indicator needle bar (outer rim inward toward center)
    {
        const float pointerW = isCompact
            ? juce::jmax(1.6f, 0.056f * diameter)
            : juce::jmax(2.4f, 0.024f * diameter);
        const float pointerLen = rPointerOuter - rPointerInner;
        const float pointerRadius = pointerW * 0.45f;
        const auto transform = juce::AffineTransform::rotation(currentAngle, 0.0f, 0.0f)
                                   .translated(centre.x, centre.y);

        if (state.enabled && palette.glow.getAlpha() > 0)
        {
            juce::Path outerGlowBar;
            outerGlowBar.addRoundedRectangle(-pointerW * 1.35f,
                                             -rPointerOuter - pointerW * 0.45f,
                                             pointerW * 2.70f,
                                             pointerLen + pointerW * 0.90f,
                                             pointerW * 1.20f);
            g.setColour(palette.glow.withAlpha(state.dragging ? 0.16f : 0.11f));
            g.fillPath(outerGlowBar, transform);

            juce::Path innerGlowBar;
            innerGlowBar.addRoundedRectangle(-pointerW * 0.82f,
                                             -rPointerOuter - pointerW * 0.18f,
                                             pointerW * 1.64f,
                                             pointerLen + pointerW * 0.36f,
                                             pointerW * 0.72f);
            g.setColour(palette.glow.withAlpha(state.dragging ? 0.28f : 0.19f));
            g.fillPath(innerGlowBar, transform);
        }

        juce::Path pointerBar;
        pointerBar.addRoundedRectangle(-pointerW * 0.5f, -rPointerOuter, pointerW, pointerLen, pointerRadius);
        g.setColour(palette.primary);
        g.fillPath(pointerBar, transform);

        // Crisp luminous inner core for machined optical depth
        if (state.enabled)
        {
            const float coreW = pointerW * 0.42f;
            juce::Path coreBar;
            coreBar.addRoundedRectangle(-coreW * 0.5f,
                                        -rPointerOuter + 0.5f,
                                        coreW,
                                        juce::jmax(1.0f, pointerLen - 1.0f),
                                        coreW * 0.45f);
            g.setColour(palette.coreHighlight.withAlpha(state.dragging ? 0.92f : 0.78f));
            g.fillPath(coreBar, transform);
        }
    }
}

} // namespace AllyGUI
