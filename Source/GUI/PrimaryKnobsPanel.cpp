#include "PrimaryKnobsPanel.h"

#include <cmath>

namespace AllyGUI
{

PrimaryKnobsPanel::PrimaryKnobsPanel(juce::AudioProcessorValueTreeState& apvts,
                                     AllyBassAudioProcessor* processorPtr)
    : apvtsRef(apvts),
      outputMeter(processorPtr)
{
    setInterceptsMouseClicks(false, true);

    auto formatOutputDb = [](double v) -> juce::String
    {
        const double snapped = (std::abs(v) < 0.005) ? 0.0 : v;
        if (snapped > 0.005)
            return "+" + juce::String(snapped, 2) + " dB";
        return juce::String(snapped, 2) + " dB";
    };

    const std::array<DepthRingConfig, knobCount> configs = {{
        {
            "DIEPTE",
            "diepte",
            "NAP",
            EngineeredDepthTheme::AccentTheme::Cyan,
            EngineeredDepthIcons::IconId::DiepteWater,
            2,
            {}
        },
        {
            "POLDER PUNCH",
            "polderPunch",
            "%",
            EngineeredDepthTheme::AccentTheme::Cyan,
            EngineeredDepthIcons::IconId::PolderPunch,
            0,
            {}
        },
        {
            "HAVEN DIRT",
            "havenDirt",
            "DIRT",
            EngineeredDepthTheme::AccentTheme::Red,
            EngineeredDepthIcons::IconId::HavenDirt,
            2,
            {}
        },
        {
            "WATERWIJK GLIDE",
            "waterwijkGlide",
            "ms",
            EngineeredDepthTheme::AccentTheme::Cyan,
            EngineeredDepthIcons::IconId::WaterwijkGlide,
            1,
            {}
        },
        {
            "OUTPUT",
            "masterVolume",
            "dB",
            EngineeredDepthTheme::AccentTheme::Cyan,
            EngineeredDepthIcons::IconId::OutputBars,
            2,
            formatOutputDb
        }
    }};

    for (size_t i = 0; i < configs.size(); ++i)
    {
        knobs[i] = std::make_unique<DepthRingKnob>(configs[i], apvtsRef);
        addAndMakeVisible(*knobs[i]);
    }

    addAndMakeVisible(outputMeter);

    if (auto* havenParam = apvtsRef.getParameter("havenDirt"))
        havenDirtNormalized.store(havenParam->getValue(), std::memory_order_relaxed);

    apvtsRef.addParameterListener("havenDirt", this);
}

PrimaryKnobsPanel::~PrimaryKnobsPanel()
{
    apvtsRef.removeParameterListener("havenDirt", this);
    cancelPendingUpdate();
}

void PrimaryKnobsPanel::parameterChanged(const juce::String& parameterID, float newValue)
{
    if (parameterID == "havenDirt")
    {
        havenDirtNormalized.store(juce::jlimit(0.0f, 1.0f, newValue), std::memory_order_relaxed);
        triggerAsyncUpdate();
    }
}

void PrimaryKnobsPanel::handleAsyncUpdate()
{
    if (!centerRepaintBounds.isEmpty())
        repaint(centerRepaintBounds);
    else
        repaint();
}

DepthRingKnob* PrimaryKnobsPanel::getKnob(int index) const noexcept
{
    if (index < 0 || index >= knobCount)
        return nullptr;
    return knobs[static_cast<size_t>(index)].get();
}

void PrimaryKnobsPanel::resized()
{
    const auto local = getLocalBounds().toFloat();
    if (local.getWidth() < 120.0f || local.getHeight() < 60.0f)
    {
        chassisBounds = {};
        return;
    }

    chassisBounds = local.reduced(1.0f, 2.0f);

    const float sidePad   = juce::jlimit(6.0f, 18.0f, chassisBounds.getWidth() * 0.014f);
    const float topPad    = juce::jlimit(4.0f, 10.0f, chassisBounds.getHeight() * 0.028f);
    const float bottomPad = juce::jlimit(4.0f, 10.0f, chassisBounds.getHeight() * 0.028f);

    const float meterW        = juce::jlimit(11.0f, 17.0f, chassisBounds.getWidth() * 0.0185f);
    const float meterRightPad = juce::jlimit(5.0f, 10.0f, chassisBounds.getWidth() * 0.0095f);
    const float meterLeftGap  = 3.0f;

    const float slotsAvailLeft  = chassisBounds.getX() + sidePad;
    const float meterLeftEdge   = chassisBounds.getRight() - meterRightPad - meterW;
    const float slotsAvailRight = meterLeftEdge - meterLeftGap;

    const auto innerArea = juce::Rectangle<float>(slotsAvailLeft,
                                                  chassisBounds.getY() + topPad,
                                                  juce::jmax(100.0f, slotsAvailRight - slotsAvailLeft),
                                                  chassisBounds.getHeight() - topPad - bottomPad);

    // Exact equal integer width/height/y for every slot guarantees identical dial diameter & baselines
    const int equalSlotW = juce::jmax(40, static_cast<int>(std::floor(innerArea.getWidth() / static_cast<float>(knobCount))));
    const int totalSlotsW = equalSlotW * knobCount;
    const int startX = juce::roundToInt(innerArea.getCentreX() - static_cast<float>(totalSlotsW) * 0.5f);
    const int slotY  = juce::roundToInt(innerArea.getY());
    const int slotH  = juce::jmax(40, juce::roundToInt(innerArea.getHeight()));

    for (int i = 0; i < knobCount; ++i)
    {
        const auto rect = juce::Rectangle<int>(startX + i * equalSlotW, slotY, equalSlotW, slotH);
        slotBounds[static_cast<size_t>(i)] = rect;
        if (knobs[static_cast<size_t>(i)] != nullptr)
            knobs[static_cast<size_t>(i)]->setBounds(rect);
    }

    // Align vertical segmented LED output meter directly alongside OUTPUT dial without infringing on any knob slot
    const float topChamfer    = juce::jlimit(8.0f, 18.0f, chassisBounds.getHeight() * 0.078f);
    const float bottomChamfer = juce::jlimit(7.0f, 15.0f, chassisBounds.getHeight() * 0.064f);
    const int meterX = juce::jmax(startX + totalSlotsW + 2, juce::roundToInt(meterLeftEdge));
    const int meterIntW = juce::jmax(9, juce::roundToInt(meterW));

    float meterTopY = chassisBounds.getY() + topChamfer + 2.0f;
    float meterBotY = chassisBounds.getBottom() - bottomChamfer - 2.0f;

    if (knobs[4] != nullptr && !knobs[4]->getDialBounds().isEmpty())
    {
        const float k4y = static_cast<float>(knobs[4]->getY());
        const auto dialR = knobs[4]->getDialBounds();
        const auto titleR = knobs[4]->getTitleBounds();
        const auto readR = knobs[4]->getReadoutBounds();

        const float desiredTop = k4y + juce::jmin(dialR.getY() - 2.0f, titleR.getBottom() + 2.0f);
        const float desiredBot = k4y + (readR.isEmpty() ? dialR.getBottom() : (readR.getY() + readR.getHeight() * 0.42f));

        meterTopY = juce::jmax(chassisBounds.getY() + topChamfer + 2.0f, desiredTop);
        meterBotY = juce::jmin(chassisBounds.getBottom() - bottomChamfer - 2.0f, desiredBot);
    }

    const int meterIntY = juce::roundToInt(meterTopY);
    const int meterIntH = juce::jmax(32, juce::roundToInt(meterBotY - meterTopY));
    outputMeter.setBounds(meterX, meterIntY, meterIntW, meterIntH);

    cachedCenterBayPath.clear();
    cachedCenterWallsPath.clear();
    centerRepaintBounds = {};
    if (!slotBounds[2].isEmpty())
    {
        const float yTop     = chassisBounds.getY() + 2.0f;
        const float yBot     = chassisBounds.getBottom() - 2.0f;
        const float bendY    = chassisBounds.getY() + topChamfer * 1.18f;
        const float botBendY = chassisBounds.getBottom() - bottomChamfer * 1.15f;
        const float bendDx   = topChamfer * 0.85f;
        const float midLeftX  = static_cast<float>(slotBounds[2].getX());
        const float midRightX = static_cast<float>(slotBounds[2].getRight());

        cachedCenterBayPath.startNewSubPath(midLeftX - bendDx, yTop + 2.0f);
        cachedCenterBayPath.lineTo(midRightX + bendDx, yTop + 2.0f);
        cachedCenterBayPath.lineTo(midRightX + 1.0f, bendY);
        cachedCenterBayPath.lineTo(midRightX + 1.0f, botBendY);
        cachedCenterBayPath.lineTo(midRightX + bendDx, yBot - 2.0f);
        cachedCenterBayPath.lineTo(midLeftX - bendDx, yBot - 2.0f);
        cachedCenterBayPath.lineTo(midLeftX - 1.0f, botBendY);
        cachedCenterBayPath.lineTo(midLeftX - 1.0f, bendY);
        cachedCenterBayPath.closeSubPath();

        cachedCenterWallsPath.startNewSubPath(midLeftX - bendDx, yTop + 1.0f);
        cachedCenterWallsPath.lineTo(midLeftX - 1.0f, bendY);
        cachedCenterWallsPath.lineTo(midLeftX - 1.0f, botBendY);
        cachedCenterWallsPath.lineTo(midLeftX - bendDx, yBot - 1.0f);
        cachedCenterWallsPath.startNewSubPath(midRightX + bendDx, yTop + 1.0f);
        cachedCenterWallsPath.lineTo(midRightX + 1.0f, bendY);
        cachedCenterWallsPath.lineTo(midRightX + 1.0f, botBendY);
        cachedCenterWallsPath.lineTo(midRightX + bendDx, yBot - 1.0f);

        centerRepaintBounds = cachedCenterBayPath.getBounds().expanded(4.0f).getSmallestIntegerContainer();
    }

    cachedW = 0;
    cachedH = 0;
}

void PrimaryKnobsPanel::renderStaticChassisToCache(juce::Graphics& g) const
{
    const float topChamfer    = juce::jlimit(8.0f, 18.0f, chassisBounds.getHeight() * 0.078f);
    const float bottomChamfer = juce::jlimit(7.0f, 15.0f, chassisBounds.getHeight() * 0.064f);

    const auto outerPath = EngineeredDepthTheme::buildChamferedRectPath(chassisBounds, topChamfer, bottomChamfer);
    const auto innerBevel = EngineeredDepthTheme::buildChamferedRectPath(chassisBounds.reduced(1.5f),
                                                                         juce::jmax(4.0f, topChamfer - 1.5f),
                                                                         juce::jmax(4.0f, bottomChamfer - 1.5f));

    // 1. Machined dark outer shadow housing
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.92f));
    g.fillPath(outerPath);

    // 2. Clipped sculpted metallic chassis bays & 3D faceted sub-panels
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(outerPath);

        const auto topBg = EngineeredDepthTheme::Colors::surface1.interpolatedWith(
            EngineeredDepthTheme::Colors::surface2, 0.34f);
        const auto midBg = EngineeredDepthTheme::Colors::surface1.darker(0.32f);
        const auto botBg = EngineeredDepthTheme::Colors::bg.darker(0.30f);

        juce::ColourGradient chassisGrad(topBg,
                                         chassisBounds.getCentreX(), chassisBounds.getY(),
                                         botBg,
                                         chassisBounds.getCentreX(), chassisBounds.getBottom(),
                                         false);
        chassisGrad.addColour(0.24, midBg);
        chassisGrad.addColour(0.78, EngineeredDepthTheme::Colors::bg);
        g.setGradientFill(chassisGrad);
        g.fillRect(chassisBounds);

        // Strong overhead studio metallic sheen across top console rail
        juce::ColourGradient topSheenGrad(
            EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.14f),
            chassisBounds.getCentreX(), chassisBounds.getY(),
            juce::Colours::transparentBlack,
            chassisBounds.getCentreX(), chassisBounds.getY() + chassisBounds.getHeight() * 0.25f,
            false);
        g.setGradientFill(topSheenGrad);
        g.fillRect(chassisBounds);

        const float yTop   = chassisBounds.getY() + 2.0f;
        const float yBot   = chassisBounds.getBottom() - 2.0f;
        const float bendY  = chassisBounds.getY() + topChamfer * 1.18f;
        const float botBendY = chassisBounds.getBottom() - bottomChamfer * 1.15f;
        const float bendDx = topChamfer * 0.85f;

        if (!slotBounds[0].isEmpty() && !slotBounds[2].isEmpty() && !slotBounds[4].isEmpty())
        {
            const float midLeftX  = static_cast<float>(slotBounds[2].getX());
            const float midRightX = static_cast<float>(slotBounds[2].getRight());

            // Recessed left wing bay (DIEPTE + POLDER PUNCH)
            juce::Path leftBay;
            leftBay.startNewSubPath(chassisBounds.getX() + topChamfer * 0.6f, yTop + topChamfer * 0.65f);
            leftBay.lineTo(midLeftX - bendDx, yTop + 4.0f);
            leftBay.lineTo(midLeftX - bendDx * 0.25f, bendY);
            leftBay.lineTo(midLeftX - bendDx * 0.25f, botBendY);
            leftBay.lineTo(midLeftX - bendDx, yBot - 4.0f);
            leftBay.lineTo(chassisBounds.getX() + bottomChamfer * 0.65f, yBot - bottomChamfer * 0.65f);
            leftBay.closeSubPath();

            juce::ColourGradient wingGrad(juce::Colour(0xff0b1b24), chassisBounds.getCentreX(), yTop,
                                          juce::Colour(0xff040c11), chassisBounds.getCentreX(), yBot, false);
            g.setGradientFill(wingGrad);
            g.fillPath(leftBay);

            // Recessed right wing bay (WATERWIJK GLIDE + OUTPUT)
            juce::Path rightBay;
            rightBay.startNewSubPath(midRightX + bendDx, yTop + 4.0f);
            rightBay.lineTo(chassisBounds.getRight() - topChamfer * 0.65f, yTop + topChamfer * 0.65f);
            rightBay.lineTo(chassisBounds.getRight() - bottomChamfer * 0.65f, yBot - bottomChamfer * 0.65f);
            rightBay.lineTo(midRightX + bendDx, yBot - 4.0f);
            rightBay.lineTo(midRightX + bendDx * 0.25f, botBendY);
            rightBay.lineTo(midRightX + bendDx * 0.25f, bendY);
            rightBay.closeSubPath();

            g.setGradientFill(wingGrad);
            g.fillPath(rightBay);

            // Sculpted chamfered hexagonal center compartment framing HAVEN DIRT
            juce::ColourGradient centerGrad(juce::Colour(0xff0f1c24), midLeftX, yTop,
                                            juce::Colour(0xff040b10), midLeftX, yBot, false);
            centerGrad.addColour(0.50, juce::Colour(0xff08131a));
            g.setGradientFill(centerGrad);
            g.fillPath(cachedCenterBayPath);

            // Soft recessed circular seat halos under wing knobs (center Haven Dirt halo sits above red bay spill)
            for (int i = 0; i < knobCount; ++i)
            {
                if (i == 2 || knobs[static_cast<size_t>(i)] == nullptr || knobs[static_cast<size_t>(i)]->getDialBounds().isEmpty())
                    continue;
                const auto dialLocal = knobs[static_cast<size_t>(i)]->getDialBounds();
                const float cx = static_cast<float>(knobs[static_cast<size_t>(i)]->getX()) + dialLocal.getCentreX();
                const float cy = static_cast<float>(knobs[static_cast<size_t>(i)]->getY()) + dialLocal.getCentreY();
                const float haloR = dialLocal.getWidth() * 0.56f;

                juce::ColourGradient seatShadow(EngineeredDepthTheme::Colors::black.withAlpha(0.72f), cx, cy,
                                                juce::Colours::transparentBlack, cx + haloR, cy, true);
                g.setGradientFill(seatShadow);
                g.fillEllipse(cx - haloR, cy - haloR, haloR * 2.0f, haloR * 2.0f);
            }

            // Static side separator seams between DIEPTE & POLDER PUNCH + WATERWIJK GLIDE & OUTPUT
            for (int sepIdx : { 0, 3 })
            {
                const float sx = static_cast<float>(slotBounds[static_cast<size_t>(sepIdx)].getRight());
                juce::Path p;
                p.startNewSubPath(sx, yTop + 10.0f);
                p.lineTo(sx, yBot - 10.0f);

                const auto baseCol = EngineeredDepthTheme::Colors::stroke;
                juce::ColourGradient seamGrad(baseCol.withAlpha(0.24f * 0.65f), 0.0f, yTop,
                                              baseCol.withAlpha(0.24f * 0.28f), 0.0f, yBot, false);
                seamGrad.addColour(0.45, baseCol.withAlpha(0.24f));
                g.setGradientFill(seamGrad);
                g.strokePath(p, juce::PathStrokeType(1.15f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
            }
        }

        // Recessed sunken trough pocket around the Output Level Meter
        if (!outputMeter.getBounds().isEmpty())
        {
            const auto meterPocket = outputMeter.getBounds().toFloat().expanded(3.0f, 3.0f);
            g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.88f));
            g.fillRoundedRectangle(meterPocket, 3.0f);
            g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.45f));
            g.drawRoundedRectangle(meterPocket, 3.0f, 0.85f);
        }
    }

    // 3. Crisp machined outer contour bezel + bright steel-blue top rail catch
    g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.95f));
    g.strokePath(outerPath, juce::PathStrokeType(1.3f));

    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.65f));
    g.strokePath(innerBevel, juce::PathStrokeType(1.0f));

    juce::Path topRimHighlight;
    topRimHighlight.startNewSubPath(chassisBounds.getX() + 2.0f, chassisBounds.getY() + topChamfer - 1.0f);
    topRimHighlight.lineTo(chassisBounds.getX() + topChamfer, chassisBounds.getY() + 1.0f);
    topRimHighlight.lineTo(chassisBounds.getRight() - topChamfer, chassisBounds.getY() + 1.0f);
    topRimHighlight.lineTo(chassisBounds.getRight() - 2.0f, chassisBounds.getY() + topChamfer - 1.0f);

    g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.48f));
    g.strokePath(topRimHighlight, juce::PathStrokeType(1.2f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt));
}

void PrimaryKnobsPanel::renderDynamicHavenGlow(juce::Graphics& g, float dirtLevel) const
{
    if (slotBounds[0].isEmpty() || slotBounds[2].isEmpty() || slotBounds[4].isEmpty() || cachedCenterBayPath.isEmpty())
        return;

    const float yTop = chassisBounds.getY() + 2.0f;
    const float yBot = chassisBounds.getBottom() - 2.0f;
    const auto& havenSlot = slotBounds[2];
    const auto havenCentre = havenSlot.getCentre().toFloat();
    const float spillR = static_cast<float>(havenSlot.getWidth()) * 0.72f;
    const float redAlpha = 0.08f + 0.16f * dirtLevel;

    juce::ColourGradient redSpill(EngineeredDepthTheme::Colors::red.withAlpha(redAlpha),
                                  havenCentre.x - spillR * 0.18f, havenCentre.y + 4.0f,
                                  juce::Colours::transparentBlack,
                                  havenCentre.x + spillR * 0.82f, havenCentre.y + 4.0f,
                                  true);
    g.setGradientFill(redSpill);
    g.fillPath(cachedCenterBayPath);

    const float redTopY = static_cast<float>(havenSlot.getY()) + 16.0f;
    juce::ColourGradient topRedGlint(EngineeredDepthTheme::Colors::red.withAlpha(0.08f + 0.08f * dirtLevel),
                                     havenCentre.x, redTopY,
                                     juce::Colours::transparentBlack,
                                     havenCentre.x + spillR * 0.45f, redTopY,
                                     true);
    g.setGradientFill(topRedGlint);
    g.fillPath(cachedCenterBayPath);

    if (knobs[2] != nullptr && !knobs[2]->getDialBounds().isEmpty())
    {
        const auto dialLocal = knobs[2]->getDialBounds();
        const float cx = static_cast<float>(knobs[2]->getX()) + dialLocal.getCentreX();
        const float cy = static_cast<float>(knobs[2]->getY()) + dialLocal.getCentreY();
        const float haloR = dialLocal.getWidth() * 0.56f;

        juce::ColourGradient seatShadow(EngineeredDepthTheme::Colors::black.withAlpha(0.72f), cx, cy,
                                        juce::Colours::transparentBlack, cx + haloR, cy, true);
        g.setGradientFill(seatShadow);
        g.fillEllipse(cx - haloR, cy - haloR, haloR * 2.0f, haloR * 2.0f);
    }

    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.80f));
    g.strokePath(cachedCenterWallsPath, juce::PathStrokeType(2.2f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));

    const auto centerSeamCol = EngineeredDepthTheme::Colors::metalRimLight
                                   .interpolatedWith(EngineeredDepthTheme::Colors::red, 0.10f + 0.24f * dirtLevel);
    juce::ColourGradient seamGrad(centerSeamCol.withAlpha(0.44f * 0.65f), 0.0f, yTop,
                                  centerSeamCol.withAlpha(0.44f * 0.28f), 0.0f, yBot, false);
    seamGrad.addColour(0.45, centerSeamCol.withAlpha(0.44f));
    g.setGradientFill(seamGrad);
    g.strokePath(cachedCenterWallsPath, juce::PathStrokeType(1.15f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
}

size_t PrimaryKnobsPanel::getEstimatedCacheBytes() const noexcept
{
    size_t total = cachedStaticChassisImage.isValid()
        ? static_cast<size_t>(cachedStaticChassisImage.getWidth()) * static_cast<size_t>(cachedStaticChassisImage.getHeight()) * 4u
        : 0u;
    for (const auto& k : knobs)
        if (k != nullptr)
            total += k->getEstimatedCacheBytes();
    total += outputMeter.getEstimatedCacheBytes();
    return total;
}

void PrimaryKnobsPanel::paint(juce::Graphics& g)
{
    if (chassisBounds.isEmpty() || getWidth() <= 0 || getHeight() <= 0)
        return;

    const float scale = juce::jlimit(1.0f, 4.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    const float dirtLevel = havenDirtNormalized.load(std::memory_order_relaxed);
    const int w = getWidth();
    const int h = getHeight();

    if (!cachedStaticChassisImage.isValid()
        || cachedW != w
        || cachedH != h
        || std::abs(scale - cachedScale) > 0.01f)
    {
        const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(w) * scale));
        const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(h) * scale));
        cachedStaticChassisImage = juce::Image(juce::Image::ARGB, rw, rh, true);

        juce::Graphics cacheG(cachedStaticChassisImage);
        cacheG.addTransform(juce::AffineTransform::scale(scale));
        renderStaticChassisToCache(cacheG);

        cachedW = w;
        cachedH = h;
        cachedScale = scale;
    }

    g.drawImage(cachedStaticChassisImage, 0, 0, w, h,
                0, 0, cachedStaticChassisImage.getWidth(), cachedStaticChassisImage.getHeight());
    renderDynamicHavenGlow(g, dirtLevel);
}

} // namespace AllyGUI
