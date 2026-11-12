#include "DepthRingLookAndFeel.h"

namespace AllyGUI
{

void DepthRingLookAndFeel::configureSlider(juce::Slider& slider,
                                           EngineeredDepthTheme::AccentTheme theme,
                                           EngineeredDepthIcons::IconId icon)
{
    slider.getProperties().set(themePropertyId, static_cast<int>(theme));
    slider.getProperties().set(iconPropertyId, static_cast<int>(icon));
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
    const float norm = juce::jlimit(0.0f, 1.0f, sliderPosProportional);
    const auto state = inspectSliderState(slider, norm);
    const auto palette = EngineeredDepthTheme::resolveAccent(theme, state);

    const float currentAngle = rotaryStartAngle + norm * (rotaryEndAngle - rotaryStartAngle);

    // Layer radii calibrated to ALLY1300_APPROVED_MASTER_FULL.png proportions
    const float rTickOuterMajor = 0.488f * diameter;
    const float rTickOuterMinor = 0.468f * diameter;
    const float rTickInner      = 0.450f * diameter;
    const float rWell           = 0.442f * diameter;
    const float rArc            = 0.422f * diameter;
    const float rMetalOuter     = 0.394f * diameter;
    const float rBevelInner     = 0.365f * diameter;
    const float rDishOuter      = 0.362f * diameter;
    const float rCapInner       = 0.268f * diameter;
    const float rPointerOuter   = 0.378f * diameter;
    const float rPointerInner   = 0.142f * diameter;

    const float arcStroke = juce::jmax(2.6f, 0.030f * diameter);

    // 1. Dark circular recessed well behind arc and metallic bezel
    g.setColour(EngineeredDepthTheme::Colors::dialWell);
    g.fillEllipse(centre.x - rWell, centre.y - rWell, rWell * 2.0f, rWell * 2.0f);

    // 2. Outer technical scale ticks: 24 intervals (11.25 deg), major ticks every 22.5 deg
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

        juce::ColourGradient bevelGrad(
            EngineeredDepthTheme::Colors::metalRimMid.interpolatedWith(EngineeredDepthTheme::Colors::metalRimLight,
                                                                        state.hovered ? 0.52f : 0.36f),
            centre.x, centre.y - rMetalOuter,
            EngineeredDepthTheme::Colors::metalRimDark.interpolatedWith(EngineeredDepthTheme::Colors::metalRimMid, 0.22f),
            centre.x, centre.y + rMetalOuter * 0.85f,
            false);
        bevelGrad.addColour(0.55, EngineeredDepthTheme::Colors::bg);
        g.setGradientFill(bevelGrad);
        g.fillEllipse(rimBounds);

        // Top specular rim lip
        const float rimStroke = juce::jmax(1.0f, 0.0065f * diameter);
        juce::Path topSpec;
        topSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.5f, rMetalOuter - rimStroke * 0.5f,
                              0.0f, -0.90f, 0.90f, true);
        g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(state.hovered ? 0.46f : 0.30f));
        g.strokePath(topSpec, juce::PathStrokeType(rimStroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        // Subtle lower-right rim catch-light (~4:30) matching machined metal bevel
        juce::Path lowerSpec;
        lowerSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.5f, rMetalOuter - rimStroke * 0.5f,
                                0.0f, 2.00f, 2.75f, true);
        g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.24f));
        g.strokePath(lowerSpec, juce::PathStrokeType(rimStroke * 0.80f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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

        juce::ColourGradient dishGrad(
            EngineeredDepthTheme::Colors::dialFaceTop.withMultipliedBrightness(state.hovered ? 1.12f : 0.94f),
            centre.x + rDishOuter * 0.04f, centre.y - rDishOuter * 0.35f,
            EngineeredDepthTheme::Colors::dialFaceBot,
            centre.x, centre.y + rDishOuter * 0.90f,
            true);
        dishGrad.addColour(0.45, EngineeredDepthTheme::Colors::dialFaceMid);
        g.setGradientFill(dishGrad);
        g.fillEllipse(dishBounds);

        // Delicate upper bevel highlight on dish edge
        g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(state.hovered ? 0.38f : 0.22f));
        g.drawEllipse(dishBounds.reduced(0.5f), juce::jmax(0.8f, 0.0050f * diameter));

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

        g.setColour(EngineeredDepthTheme::Colors::capStroke.withAlpha(state.hovered ? 0.82f : 0.60f));
        g.drawEllipse(capBounds, juce::jmax(0.8f, 0.005f * diameter));
    }

    // 7. Central vector icon (strictly bounded inside inner cap clearance)
    if (icon != EngineeredDepthIcons::IconId::None)
    {
        const auto iconBounds = juce::Rectangle<float>(0.215f * diameter, 0.150f * diameter)
                                    .withCentre({ centre.x, centre.y + 0.025f * diameter });
        const float iconStroke = juce::jmax(1.0f, 0.0072f * diameter);
        const float iconGlow   = state.enabled ? (state.dragging ? 0.30f : (state.hovered ? 0.24f : 0.15f)) : 0.0f;

        EngineeredDepthIcons::drawIcon(g,
                                       icon,
                                       iconBounds,
                                       palette.primary.withAlpha(state.enabled ? 0.92f : 0.55f),
                                       palette.coreHighlight.withAlpha(state.enabled ? 0.88f : 0.45f),
                                       iconStroke,
                                       iconGlow);
    }

    // 8. Luminous indicator needle bar (outer rim inward toward center)
    {
        const float pointerW = juce::jmax(2.4f, 0.024f * diameter);
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
