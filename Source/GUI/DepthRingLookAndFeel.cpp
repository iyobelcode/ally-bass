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

size_t DepthRingLookAndFeel::getEstimatedCacheBytes() const noexcept
{
    size_t total = 0u;
    if (cachedOuterBack.isValid())
        total += static_cast<size_t>(cachedOuterBack.getWidth()) * static_cast<size_t>(cachedOuterBack.getHeight()) * 4u;
    if (cachedInnerBody.isValid())
        total += static_cast<size_t>(cachedInnerBody.getWidth()) * static_cast<size_t>(cachedInnerBody.getHeight()) * 4u;
    return total;
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
    if (diameter < 24.0f || width <= 0 || height <= 0)
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
    const float scale = juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(width) * scale));
    const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(height) * scale));

    // Layer radii calibrated to ALLY1300_APPROVED_MASTER_FULL.png proportions
    const float rTickOuterMajor = 0.490f * diameter;
    const float rTickOuterMinor = 0.470f * diameter;
    const float rTickInner      = 0.450f * diameter;
    const float rWell           = (isCompact ? 0.484f : 0.440f) * diameter;
    const float rArc            = (isCompact ? 0.436f : 0.421f) * diameter;
    const float rMetalOuter     = (isCompact ? 0.398f : 0.402f) * diameter;
    const float rBevelInner     = (isCompact ? 0.330f : 0.348f) * diameter;
    const float rDishOuter      = (isCompact ? 0.312f : 0.326f) * diameter;
    const float rCapInner       = (isCompact ? 0.228f : 0.246f) * diameter;
    const float rPointerOuter   = (isCompact ? 0.376f : 0.378f) * diameter;
    const float rPointerInner   = (isCompact ? 0.165f : 0.215f) * diameter;

    const float arcStroke = isCompact
        ? juce::jmax(2.2f, 0.088f * diameter)
        : juce::jmax(2.8f, 0.032f * diameter);

    const bool isRedAccent = (theme == EngineeredDepthTheme::AccentTheme::Red);
    const float activeMidAngle = rotaryStartAngle + (currentAngle - rotaryStartAngle) * 0.62f;
    const float glowCenterX = centre.x + rArc * 0.84f * std::sin(activeMidAngle);
    const float glowCenterY = centre.y - rArc * 0.84f * std::cos(activeMidAngle);

    // 1 + 3. Cached static circular well + rims + inactive track groove + precomputed tick coordinates
    const uint32_t outerKey = (static_cast<uint32_t>(themeVal) & 0xffu)
                            | ((isCompact ? 1u : 0u) << 8)
                            | ((state.enabled ? 1u : 0u) << 9)
                            | ((state.hovered ? 1u : 0u) << 10);

    if (!cachedOuterBack.isValid()
        || cachedOuterW != width
        || cachedOuterH != height
        || std::abs(scale - cachedOuterScale) > 0.01f
        || cachedOuterKey != outerKey
        || std::abs(rotaryStartAngle - cachedStartAngle) > 1e-4f
        || std::abs(rotaryEndAngle - cachedEndAngle) > 1e-4f)
    {
        cachedOuterBack = juce::Image(juce::Image::ARGB, rw, rh, true);
        juce::Graphics cg(cachedOuterBack);
        cg.addTransform(juce::AffineTransform::translation(static_cast<float>(-x), static_cast<float>(-y)).scaled(scale));

        const auto wellBounds = juce::Rectangle<float>(centre.x - rWell, centre.y - rWell, rWell * 2.0f, rWell * 2.0f);
        cg.setColour(EngineeredDepthTheme::Colors::dialWell);
        cg.fillEllipse(wellBounds);

        cg.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.90f));
        cg.drawEllipse(wellBounds, juce::jmax(0.9f, (isCompact ? 0.015f : 0.006f) * diameter));
        cg.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.45f));
        cg.drawEllipse(wellBounds.reduced(0.5f), juce::jmax(0.75f, (isCompact ? 0.012f : 0.004f) * diameter));

        cachedTrackPath.clear();
        cachedTrackPath.addCentredArc(centre.x, centre.y, rArc, rArc, 0.0f, rotaryStartAngle, rotaryEndAngle, true);

        constexpr int numSteps = 24;
        for (int i = 1; i < numSteps; ++i)
        {
            const float t = static_cast<float>(i) / static_cast<float>(numSteps);
            const float angle = rotaryStartAngle + t * (rotaryEndAngle - rotaryStartAngle);
            const bool isMajor = (i % 2 == 0);
            const float sinA = std::sin(angle);
            const float cosA = -std::cos(angle);
            const float outerR = isMajor ? rTickOuterMajor : rTickOuterMinor;

            cachedTicks[static_cast<size_t>(i - 1)] = {
                centre.x + rTickInner * sinA,
                centre.y + rTickInner * cosA,
                centre.x + outerR * sinA,
                centre.y + outerR * cosA,
                angle,
                isMajor,
                (i == 4)
            };
        }

        cachedOuterW = width;
        cachedOuterH = height;
        cachedOuterScale = scale;
        cachedOuterKey = outerKey;
        cachedStartAngle = rotaryStartAngle;
        cachedEndAngle = rotaryEndAngle;
    }

    g.drawImage(cachedOuterBack, x, y, width, height, 0, 0, cachedOuterBack.getWidth(), cachedOuterBack.getHeight());

    // Dynamic directional well light spill from active value arc
    if (state.active && state.enabled && !isCompact)
    {
        const auto wellBounds = juce::Rectangle<float>(centre.x - rWell, centre.y - rWell, rWell * 2.0f, rWell * 2.0f);
        const float spillAlpha = isRedAccent
            ? (0.14f + 0.14f * norm + (state.hovered ? 0.05f : 0.0f))
            : (0.04f + 0.06f * norm);
        const float spillR = rWell * 0.92f;
        juce::ColourGradient wellSpill(
            palette.primary.withAlpha(spillAlpha),
            glowCenterX, glowCenterY,
            juce::Colours::transparentBlack,
            glowCenterX + spillR, glowCenterY,
            true);
        g.setGradientFill(wellSpill);
        g.fillEllipse(wellBounds);
    }

    // 2. Outer technical scale ticks using precomputed coordinates
    if (!isCompact)
    {
        const float majorTickWidth = juce::jmax(1.1f, 0.0085f * diameter);
        const float minorTickWidth = juce::jmax(0.8f, 0.0055f * diameter);

        for (const auto& tick : cachedTicks)
        {
            const bool underActiveArc = state.active && (tick.angle <= currentAngle + 0.015f);
            juce::Colour tickCol;
            if (!state.enabled)
            {
                tickCol = EngineeredDepthTheme::Colors::disabled.withAlpha(tick.isMajor ? 0.50f : 0.28f);
            }
            else if (tick.isMajor)
            {
                const float hoverLift = (state.hovered || state.dragging) ? 0.12f : 0.0f;
                if (tick.isCardinal9 && underActiveArc)
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
            g.drawLine(tick.x1, tick.y1, tick.x2, tick.y2, tick.isMajor ? majorTickWidth : minorTickWidth);
        }
    }

    // 3. Recessed circular inactive track groove
    g.setColour(palette.trackInactive);
    g.strokePath(cachedTrackPath,
                 juce::PathStrokeType(arcStroke * 0.90f,
                                      juce::PathStrokeType::curved,
                                      juce::PathStrokeType::butt));

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

    // 5 + 6 + 7. Cached sculpted metallic bevel collar, dish, cap & luminous vector icon
    const uint32_t innerKey = (static_cast<uint32_t>(themeVal) & 0xffu)
                            | ((static_cast<uint32_t>(iconVal) & 0xffu) << 8)
                            | ((isCompact ? 1u : 0u) << 16)
                            | ((state.enabled ? 1u : 0u) << 17)
                            | ((state.hovered ? 1u : 0u) << 18)
                            | ((state.dragging ? 1u : 0u) << 19)
                            | ((state.active ? 1u : 0u) << 20);
    const float targetInnerNorm = (isRedAccent && state.active && state.enabled && !isCompact) ? norm : -1.0f;

    if (!cachedInnerBody.isValid()
        || cachedInnerW != width
        || cachedInnerH != height
        || std::abs(scale - cachedInnerScale) > 0.01f
        || cachedInnerKey != innerKey
        || std::abs(targetInnerNorm - cachedInnerNorm) > 1.0e-4f)
    {
        cachedInnerBody = juce::Image(juce::Image::ARGB, rw, rh, true);
        juce::Graphics gBody(cachedInnerBody);
        gBody.addTransform(juce::AffineTransform::translation(static_cast<float>(-x), static_cast<float>(-y)).scaled(scale));

        // 5. Heavy machined metallic outer bevel collar
        {
            const auto rimBounds = juce::Rectangle<float>(centre.x - rMetalOuter,
                                                          centre.y - rMetalOuter,
                                                          rMetalOuter * 2.0f,
                                                          rMetalOuter * 2.0f);

            const float rimTopMix = state.hovered ? 0.88f : 0.72f;
            juce::Colour steelTop = EngineeredDepthTheme::Colors::metalRimMid
                                        .interpolatedWith(juce::Colour(0xff9cc4da), rimTopMix)
                                        .interpolatedWith(palette.primary, state.active ? (isRedAccent ? 0.06f : 0.04f) : 0.0f);
            if (isRedAccent && state.enabled && state.active)
                steelTop = steelTop.interpolatedWith(juce::Colour(0xffc96d7b), 0.14f);

            const auto midShoulder = EngineeredDepthTheme::Colors::metalRimMid
                                         .interpolatedWith(EngineeredDepthTheme::Colors::metalRimLight, 0.24f);

            juce::ColourGradient bevelGrad(
                steelTop,
                centre.x, centre.y - rMetalOuter,
                juce::Colour(0xff040c11),
                centre.x, centre.y + rMetalOuter * 0.94f,
                false);
            bevelGrad.addColour(0.24, midShoulder);
            bevelGrad.addColour(0.54, juce::Colour(0xff0b1922));
            bevelGrad.addColour(0.82, juce::Colour(0xff18313f));
            gBody.setGradientFill(bevelGrad);
            gBody.fillEllipse(rimBounds);

            if (!isCompact)
            {
                juce::Graphics::ScopedSaveState clipBevel(gBody);
                juce::Path collarRing;
                collarRing.addEllipse(rimBounds);
                collarRing.addEllipse(centre.x - rBevelInner, centre.y - rBevelInner, rBevelInner * 2.0f, rBevelInner * 2.0f);
                collarRing.setUsingNonZeroWinding(false);
                gBody.reduceClipRegion(collarRing);

                juce::ColourGradient horizontalMetalGrad(
                    juce::Colour(0x45b8d9eb),
                    centre.x - rMetalOuter * 0.55f, centre.y - rMetalOuter * 0.65f,
                    juce::Colour(0x18040c11),
                    centre.x + rMetalOuter * 0.85f, centre.y + rMetalOuter * 0.45f,
                    false);
                gBody.setGradientFill(horizontalMetalGrad);
                gBody.fillRect(rimBounds);

                if (isRedAccent && state.active && state.enabled)
                {
                    const float reflR = rMetalOuter * 0.78f;
                    juce::ColourGradient arcRedReflect(
                        juce::Colour(0xffff2e3f).withAlpha(0.46f + 0.22f * norm),
                        glowCenterX, glowCenterY,
                        juce::Colours::transparentBlack,
                        glowCenterX + reflR, glowCenterY,
                        true);
                    gBody.setGradientFill(arcRedReflect);
                    gBody.fillRect(rimBounds);

                    const float topBounceY = centre.y - rMetalOuter * 0.96f;
                    juce::ColourGradient topRedBounce(
                        juce::Colour(0xffff3b4b).withAlpha(0.26f),
                        centre.x, topBounceY,
                        juce::Colours::transparentBlack,
                        centre.x + rMetalOuter * 0.42f, topBounceY,
                        true);
                    gBody.setGradientFill(topRedBounce);
                    gBody.fillRect(rimBounds);
                }
            }

            const float rimStroke = isCompact
                ? juce::jmax(0.85f, 0.018f * diameter)
                : juce::jmax(1.05f, 0.0075f * diameter);
            gBody.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.70f));
            gBody.drawEllipse(rimBounds.reduced(0.2f), rimStroke * 0.95f);

            juce::Path topSoftSpec;
            topSoftSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 1.15f, rMetalOuter - rimStroke * 1.15f,
                                      0.0f, -1.32f, 1.32f, true);
            const auto rimSpecCol = (isRedAccent && state.enabled)
                ? juce::Colour(0xff9fc6db).interpolatedWith(juce::Colour(0xffff5060), 0.16f)
                : juce::Colour(0xff9fc6db);
            gBody.setColour(rimSpecCol.withAlpha(state.hovered ? 0.36f : 0.26f));
            gBody.strokePath(topSoftSpec, juce::PathStrokeType(rimStroke * 2.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path topSpec;
            topSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.55f, rMetalOuter - rimStroke * 0.55f,
                                  0.0f, -1.16f, 1.16f, true);
            gBody.setColour(rimSpecCol.interpolatedWith(EngineeredDepthTheme::Colors::textPrimary, 0.35f)
                                .withAlpha(state.hovered ? 0.78f : 0.64f));
            gBody.strokePath(topSpec, juce::PathStrokeType(rimStroke * 1.05f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path lowerRightSpec;
            lowerRightSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.75f, rMetalOuter - rimStroke * 0.75f,
                                         0.0f, 1.92f, 2.78f, true);
            gBody.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(state.hovered ? 0.50f : 0.38f));
            gBody.strokePath(lowerRightSpec, juce::PathStrokeType(rimStroke * 0.95f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            juce::Path lowerLeftSpec;
            lowerLeftSpec.addCentredArc(centre.x, centre.y, rMetalOuter - rimStroke * 0.75f, rMetalOuter - rimStroke * 0.75f,
                                        0.0f, -2.78f, -1.92f, true);
            gBody.setColour((isRedAccent && state.active ? juce::Colour(0xffff5c6c) : EngineeredDepthTheme::Colors::metalRimLight)
                                .withAlpha(state.hovered ? 0.44f : 0.34f));
            gBody.strokePath(lowerLeftSpec, juce::PathStrokeType(rimStroke * 0.95f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // 6. Deep dark recessed canyon + sculpted concave dish + stepped central circular cap
        {
            const auto bevelShadow = juce::Rectangle<float>(centre.x - rBevelInner,
                                                            centre.y - rBevelInner,
                                                            rBevelInner * 2.0f,
                                                            rBevelInner * 2.0f);
            gBody.setColour(juce::Colour(0xff010508));
            gBody.fillEllipse(bevelShadow);

            gBody.setColour(EngineeredDepthTheme::Colors::black);
            gBody.drawEllipse(bevelShadow, juce::jmax(1.0f, 0.008f * diameter));

            const auto dishBounds = juce::Rectangle<float>(centre.x - rDishOuter,
                                                           centre.y - rDishOuter,
                                                           rDishOuter * 2.0f,
                                                           rDishOuter * 2.0f);

            auto dishTopCol = EngineeredDepthTheme::Colors::dialFaceTop
                                  .interpolatedWith(juce::Colour(0xff4a6f82), state.hovered ? 0.38f : 0.24f);
            if (isRedAccent && state.enabled)
                dishTopCol = dishTopCol.interpolatedWith(juce::Colour(0xff38242c), 0.35f);

            juce::ColourGradient dishGrad(
                dishTopCol,
                centre.x, centre.y - rDishOuter * 0.85f,
                juce::Colour(0xff030c12),
                centre.x, centre.y + rDishOuter * 0.92f,
                false);
            dishGrad.addColour(0.40, isRedAccent ? juce::Colour(0xff10191f) : EngineeredDepthTheme::Colors::dialFaceMid);
            dishGrad.addColour(0.78, juce::Colour(0xff06131c));
            gBody.setGradientFill(dishGrad);
            gBody.fillEllipse(dishBounds);

            if (isRedAccent && state.active && state.enabled && !isCompact)
            {
                juce::Graphics::ScopedSaveState clipDish(gBody);
                juce::Path dishPath;
                dishPath.addEllipse(dishBounds);
                gBody.reduceClipRegion(dishPath);

                const float redDishR = rDishOuter * 0.88f;
                juce::ColourGradient dishRedBounce(
                    juce::Colour(0xffff3846).withAlpha(0.24f + 0.16f * norm),
                    glowCenterX, glowCenterY,
                    juce::Colours::transparentBlack,
                    glowCenterX + redDishR, glowCenterY,
                    true);
                gBody.setGradientFill(dishRedBounce);
                gBody.fillRect(dishBounds);
            }

            const float dishStrokeAlpha = isCompact ? (state.hovered ? 0.22f : 0.12f) : (state.hovered ? 0.34f : 0.20f);
            juce::Path dishTopArc;
            dishTopArc.addCentredArc(centre.x, centre.y, rDishOuter - 0.6f, rDishOuter - 0.6f,
                                     0.0f, -1.10f, 1.10f, true);
            gBody.setColour((isRedAccent ? juce::Colour(0xff8e6873) : EngineeredDepthTheme::Colors::metalRimLight).withAlpha(dishStrokeAlpha));
            gBody.strokePath(dishTopArc, juce::PathStrokeType(juce::jmax(0.75f, 0.005f * diameter)));

            gBody.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.65f));
            gBody.drawEllipse(dishBounds.reduced(0.3f), juce::jmax(0.75f, 0.005f * diameter));

            const auto capBounds = juce::Rectangle<float>(centre.x - rCapInner,
                                                          centre.y - rCapInner,
                                                          rCapInner * 2.0f,
                                                          rCapInner * 2.0f);
            gBody.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.72f));
            gBody.drawEllipse(capBounds.expanded(0.8f), juce::jmax(0.9f, 0.007f * diameter));

            const auto capTop = isRedAccent ? juce::Colour(0xff182228) : juce::Colour(0xff0f212c);
            const auto capBot = isRedAccent ? juce::Colour(0xff070e12) : juce::Colour(0xff040e15);
            juce::ColourGradient capGrad(capTop,
                                         centre.x, centre.y - rCapInner,
                                         capBot,
                                         centre.x, centre.y + rCapInner,
                                         false);
            gBody.setGradientFill(capGrad);
            gBody.fillEllipse(capBounds);

            const float capStrokeAlpha = isCompact ? (state.hovered ? 0.24f : 0.14f) : (state.hovered ? 0.42f : 0.24f);
            gBody.setColour((isRedAccent ? juce::Colour(0xff42525c) : EngineeredDepthTheme::Colors::stroke).withAlpha(capStrokeAlpha));
            gBody.drawEllipse(capBounds, juce::jmax(0.75f, 0.0045f * diameter));
        }

        // 7. Crisp luminous central vector icon
        if (icon != EngineeredDepthIcons::IconId::None)
        {
            const bool isHavenIcon = (icon == EngineeredDepthIcons::IconId::HavenDirt);
            const float iw = (isHavenIcon ? 0.248f : 0.214f) * diameter;
            const float ih = (isHavenIcon ? 0.202f : 0.174f) * diameter;
            const auto iconBounds = juce::Rectangle<float>(iw, ih)
                                        .withCentre({ centre.x, centre.y + 0.006f * diameter });
            const float iconStroke = juce::jmax(1.05f, 0.0092f * diameter);
            const float iconGlow   = state.enabled ? (state.dragging ? 0.42f : (state.hovered ? 0.34f : 0.25f)) : 0.0f;

            const bool keepCoolHarborCyan = (icon == EngineeredDepthIcons::IconId::HavenDirt && state.enabled);
            const auto iconPrimary = keepCoolHarborCyan
                ? EngineeredDepthTheme::Colors::cyanBright.interpolatedWith(EngineeredDepthTheme::Colors::cyan, 0.35f)
                : palette.primary.interpolatedWith(palette.coreHighlight, 0.22f).withAlpha(state.enabled ? 0.96f : 0.55f);
            const auto iconSecondary = keepCoolHarborCyan
                ? EngineeredDepthTheme::Colors::cyanBright
                : palette.coreHighlight.withAlpha(state.enabled ? 0.95f : 0.48f);

            EngineeredDepthIcons::drawIcon(gBody,
                                           icon,
                                           iconBounds,
                                           iconPrimary,
                                           iconSecondary,
                                           iconStroke,
                                           iconGlow);
        }

        cachedInnerW = width;
        cachedInnerH = height;
        cachedInnerScale = scale;
        cachedInnerKey = innerKey;
        cachedInnerNorm = targetInnerNorm;
    }

    g.drawImage(cachedInnerBody, x, y, width, height, 0, 0, cachedInnerBody.getWidth(), cachedInnerBody.getHeight());

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
