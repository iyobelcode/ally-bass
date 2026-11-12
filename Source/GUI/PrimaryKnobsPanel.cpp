#include "PrimaryKnobsPanel.h"

#include <cmath>

namespace AllyGUI
{

PrimaryKnobsPanel::PrimaryKnobsPanel(juce::AudioProcessorValueTreeState& apvts)
    : apvtsRef(apvts)
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

    const auto innerArea = chassisBounds.reduced(sidePad, 0.0f)
                                        .withTop(chassisBounds.getY() + topPad)
                                        .withBottom(chassisBounds.getBottom() - bottomPad);

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
}

void PrimaryKnobsPanel::paint(juce::Graphics& g)
{
    if (chassisBounds.isEmpty())
        return;

    const float topChamfer    = juce::jlimit(8.0f, 18.0f, chassisBounds.getHeight() * 0.078f);
    const float bottomChamfer = juce::jlimit(7.0f, 15.0f, chassisBounds.getHeight() * 0.064f);

    const auto outerPath = EngineeredDepthTheme::buildChamferedRectPath(chassisBounds, topChamfer, bottomChamfer);
    const auto innerBevel = EngineeredDepthTheme::buildChamferedRectPath(chassisBounds.reduced(1.5f),
                                                                         juce::jmax(4.0f, topChamfer - 1.5f),
                                                                         juce::jmax(4.0f, bottomChamfer - 1.5f));

    // 1. Machined dark outer shadow bevel
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.88f));
    g.fillPath(outerPath);

    // 2. Clipped interior metallic marine/slate console fill & subtle pocket contours
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(outerPath);

        const auto topBg = EngineeredDepthTheme::Colors::surface1.interpolatedWith(
            EngineeredDepthTheme::Colors::surface2, 0.24f);
        const auto midBg = EngineeredDepthTheme::Colors::surface1.darker(0.36f);
        const auto botBg = EngineeredDepthTheme::Colors::bg.darker(0.24f);

        juce::ColourGradient chassisGrad(topBg,
                                         chassisBounds.getCentreX(), chassisBounds.getY(),
                                         botBg,
                                         chassisBounds.getCentreX(), chassisBounds.getBottom(),
                                         false);
        chassisGrad.addColour(0.28, midBg);
        chassisGrad.addColour(0.82, EngineeredDepthTheme::Colors::bg);
        g.setGradientFill(chassisGrad);
        g.fillRect(chassisBounds);

        // Soft upper brushed metallic wash along top console band
        juce::ColourGradient topSheenGrad(
            EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.08f),
            chassisBounds.getCentreX(), chassisBounds.getY(),
            juce::Colours::transparentBlack,
            chassisBounds.getCentreX(), chassisBounds.getY() + chassisBounds.getHeight() * 0.24f,
            false);
        g.setGradientFill(topSheenGrad);
        g.fillRect(chassisBounds);

        // Subtle Haven Dirt bay red ambient radial tone (static gradient, no heavy blur filters)
        const auto& havenSlot = slotBounds[2];
        if (!havenSlot.isEmpty())
        {
            const float dirtLevel = havenDirtNormalized.load(std::memory_order_relaxed);
            const float redAlpha = 0.035f + 0.055f * dirtLevel;
            const auto havenCentre = havenSlot.getCentre().toFloat();
            const float spillR = static_cast<float>(havenSlot.getWidth()) * 0.62f;

            juce::ColourGradient redSpill(EngineeredDepthTheme::Colors::red.withAlpha(redAlpha),
                                          havenCentre.x, havenCentre.y,
                                          juce::Colours::transparentBlack,
                                          havenCentre.x + spillR, havenCentre.y,
                                          true);
            g.setGradientFill(redSpill);
            g.fillEllipse(havenCentre.x - spillR, havenCentre.y - spillR, spillR * 2.0f, spillR * 2.0f);
        }

        // Subtle industrial bay contour traces (chamfered middle bay + side pockets from master)
        const float yTop    = chassisBounds.getY() + 2.0f;
        const float yBot    = chassisBounds.getBottom() - 2.0f;
        const float bendY   = chassisBounds.getY() + topChamfer * 1.15f;
        const float bendDx  = topChamfer * 0.72f;

        auto drawPocketLine = [&](const juce::Path& linePath, float peakAlpha)
        {
            juce::ColourGradient lineGrad(EngineeredDepthTheme::Colors::stroke.withAlpha(peakAlpha * 0.55f),
                                          0.0f, yTop,
                                          EngineeredDepthTheme::Colors::stroke.withAlpha(peakAlpha * 0.25f),
                                          0.0f, yBot,
                                          false);
            lineGrad.addColour(0.45, EngineeredDepthTheme::Colors::stroke.withAlpha(peakAlpha));
            g.setGradientFill(lineGrad);
            g.strokePath(linePath, juce::PathStrokeType(1.0f, juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        };

        if (!slotBounds[0].isEmpty() && !slotBounds[4].isEmpty())
        {
            // Left outer bay pocket trace
            {
                juce::Path p;
                const float lx = static_cast<float>(slotBounds[0].getX()) + 2.0f;
                p.startNewSubPath(chassisBounds.getX() + topChamfer * 0.45f, yTop + topChamfer * 0.55f);
                p.lineTo(lx, bendY);
                p.lineTo(lx, yBot - bottomChamfer * 0.35f);
                drawPocketLine(p, 0.34f);
            }

            // Left boundary of middle HAVEN DIRT bay (chamfered top angle separating bay 1 & 2)
            {
                juce::Path p;
                const float mx = static_cast<float>(slotBounds[2].getX());
                p.startNewSubPath(mx - bendDx, yTop);
                p.lineTo(mx, bendY);
                p.lineTo(mx, yBot);
                drawPocketLine(p, 0.42f);
            }

            // Right boundary of middle HAVEN DIRT bay (chamfered top angle separating bay 2 & 3)
            {
                juce::Path p;
                const float mx = static_cast<float>(slotBounds[2].getRight());
                p.startNewSubPath(mx + bendDx, yTop);
                p.lineTo(mx, bendY);
                p.lineTo(mx, yBot);
                drawPocketLine(p, 0.42f);
            }

            // Subtle straight separator between WATERWIJK GLIDE & OUTPUT (bay 3 & 4)
            {
                juce::Path p;
                const float rx = static_cast<float>(slotBounds[3].getRight());
                p.startNewSubPath(rx, yTop + 4.0f);
                p.lineTo(rx, yBot - 4.0f);
                drawPocketLine(p, 0.30f);
            }
        }
    }

    // 3. Outer contour bevel & top specular rim edge matching Engineered Depth hardware chassis
    g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.90f));
    g.strokePath(outerPath, juce::PathStrokeType(1.25f));

    g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(0.28f));
    g.strokePath(innerBevel, juce::PathStrokeType(0.85f));

    // Top metallic bevel catch-line along horizontal top rail + upper chamfers
    juce::Path topRimHighlight;
    topRimHighlight.startNewSubPath(chassisBounds.getX() + 2.0f, chassisBounds.getY() + topChamfer - 1.0f);
    topRimHighlight.lineTo(chassisBounds.getX() + topChamfer, chassisBounds.getY() + 1.0f);
    topRimHighlight.lineTo(chassisBounds.getRight() - topChamfer, chassisBounds.getY() + 1.0f);
    topRimHighlight.lineTo(chassisBounds.getRight() - 2.0f, chassisBounds.getY() + topChamfer - 1.0f);

    g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.32f));
    g.strokePath(topRimHighlight, juce::PathStrokeType(1.1f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt));
}

} // namespace AllyGUI
