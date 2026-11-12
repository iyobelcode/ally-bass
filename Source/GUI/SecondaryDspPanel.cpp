#include "SecondaryDspPanel.h"

#include <cmath>

namespace AllyGUI
{

SecondaryDspPanel::SecondaryDspPanel(juce::AudioProcessorValueTreeState& apvts)
{
    setInterceptsMouseClicks(false, true);

    ComboBoxConfig stadsdeelCfg;
    stadsdeelCfg.title         = "STADSDEEL DSP";
    stadsdeelCfg.paramId       = "stadsdeel";
    stadsdeelCfg.componentName = "stadsdeel";
    stadsdeelCfg.icon          = EngineeredDepthIcons::IconId::StadsdeelDsp;
    stadsdeelCfg.theme         = EngineeredDepthTheme::AccentTheme::Cyan;
    stadsdeelCfg.items         = {
        "Haven (1350) - Tape/Tube",
        "Centrum (1315) - Hard Clip",
        "Poort (1363) - Clean Exciter",
        "Buiten (1330) - Foldback"
    };
    stadsdeelControl = std::make_unique<EngineeredDepthComboBox>(stadsdeelCfg, apvts);
    addAndMakeVisible(*stadsdeelControl);

    DepthRingConfig attackCfg;
    attackCfg.title         = "ATTACK";
    attackCfg.paramId       = "attack";
    attackCfg.unitSuffix    = "ms";
    attackCfg.theme         = EngineeredDepthTheme::AccentTheme::Cyan;
    attackCfg.icon          = EngineeredDepthIcons::IconId::None;
    attackCfg.valueDecimals = 1;
    attackCfg.compact       = true;
    attackKnob = std::make_unique<DepthRingKnob>(attackCfg, apvts);
    addAndMakeVisible(*attackKnob);

    DepthRingConfig releaseCfg;
    releaseCfg.title         = "RELEASE";
    releaseCfg.paramId       = "release";
    releaseCfg.unitSuffix    = "ms";
    releaseCfg.theme         = EngineeredDepthTheme::AccentTheme::Cyan;
    releaseCfg.icon          = EngineeredDepthIcons::IconId::None;
    releaseCfg.valueDecimals = 1;
    releaseCfg.compact       = true;
    releaseKnob = std::make_unique<DepthRingKnob>(releaseCfg, apvts);
    addAndMakeVisible(*releaseKnob);

    ToggleConfig overdriveCfg;
    overdriveCfg.title         = "036 OVERDRIVE";
    overdriveCfg.paramId       = "overdrive036";
    overdriveCfg.componentName = "036_overdrive";
    overdriveCfg.theme         = EngineeredDepthTheme::AccentTheme::Red;
    overdriveToggle = std::make_unique<EngineeredDepthToggle>(overdriveCfg, apvts);
    addAndMakeVisible(*overdriveToggle);

    ToggleConfig monoCfg;
    monoCfg.title         = "1300 MONO LOCK";
    monoCfg.paramId       = "monoLock1300";
    monoCfg.componentName = "mono_lock";
    monoCfg.theme         = EngineeredDepthTheme::AccentTheme::Cyan;
    monoLockToggle = std::make_unique<EngineeredDepthToggle>(monoCfg, apvts);
    addAndMakeVisible(*monoLockToggle);
}

float SecondaryDspPanel::getDividerX(int index) const noexcept
{
    if (index < 0 || index >= static_cast<int>(dividerX.size()))
        return 0.0f;
    return dividerX[static_cast<size_t>(index)];
}

void SecondaryDspPanel::resized()
{
    const auto local = getLocalBounds().toFloat();
    if (local.getWidth() < 260.0f || local.getHeight() < 42.0f)
    {
        chassisBounds = {};
        return;
    }

    chassisBounds = local.reduced(1.0f, 2.0f);

    const float sidePad   = juce::jlimit(8.0f, 18.0f, chassisBounds.getWidth() * 0.015f);
    const float topBotPad = juce::jlimit(2.0f, 6.0f, chassisBounds.getHeight() * 0.045f);
    const auto inner      = chassisBounds.reduced(sidePad, topBotPad);

    const float divZoneW  = juce::jlimit(8.0f, 20.0f, inner.getWidth() * 0.019f);
    const float usableW   = juce::jmax(100.0f, inner.getWidth() - 3.0f * divZoneW);

    const int stadsdeelW  = juce::roundToInt(usableW * 0.450f);
    const int minisW      = juce::roundToInt(usableW * 0.170f);
    const int overdriveW  = juce::roundToInt(usableW * 0.190f);
    const int monoW       = juce::jmax(40, juce::roundToInt(inner.getWidth() - 3.0f * divZoneW)
                                            - stadsdeelW - minisW - overdriveW);

    const int rowY = juce::roundToInt(inner.getY());
    const int rowH = juce::jmax(34, juce::roundToInt(inner.getHeight()));
    float cursorX  = inner.getX();

    // 1. STADSDEEL DSP section
    stadsdeelControl->setBounds(juce::roundToInt(cursorX), rowY, stadsdeelW, rowH);
    cursorX += static_cast<float>(stadsdeelW);
    dividerX[0] = cursorX + divZoneW * 0.5f;
    cursorX += divZoneW;

    // 2. ATTACK & RELEASE Mini Depth Rings
    const int miniGap = juce::jmax(2, juce::roundToInt(static_cast<float>(minisW) * 0.045f));
    const int miniW   = juce::jmax(24, (minisW - miniGap) / 2);
    const int miniPad = juce::jmax(0, (minisW - (miniW * 2 + miniGap)) / 2);
    attackKnob->setBounds(juce::roundToInt(cursorX) + miniPad, rowY, miniW, rowH);
    releaseKnob->setBounds(juce::roundToInt(cursorX) + miniPad + miniW + miniGap, rowY, miniW, rowH);
    cursorX += static_cast<float>(minisW);
    dividerX[1] = cursorX + divZoneW * 0.5f;
    cursorX += divZoneW;

    // 3. 036 OVERDRIVE toggle
    overdriveToggle->setBounds(juce::roundToInt(cursorX), rowY, overdriveW, rowH);
    cursorX += static_cast<float>(overdriveW);
    dividerX[2] = cursorX + divZoneW * 0.5f;
    cursorX += divZoneW;

    // 4. 1300 MONO LOCK toggle
    monoLockToggle->setBounds(juce::roundToInt(cursorX), rowY, monoW, rowH);
}

void SecondaryDspPanel::paint(juce::Graphics& g)
{
    if (chassisBounds.isEmpty())
        return;

    const float chamfer = juce::jlimit(6.0f, 11.0f, chassisBounds.getHeight() * 0.145f);
    const auto outerPath  = EngineeredDepthTheme::buildChamferedRectPath(chassisBounds, chamfer, chamfer);
    const auto innerBevel = EngineeredDepthTheme::buildChamferedRectPath(chassisBounds.reduced(1.5f),
                                                                         juce::jmax(3.5f, chamfer - 1.5f),
                                                                         juce::jmax(3.5f, chamfer - 1.5f));

    // 1. Machined dark outer shadow bevel
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.88f));
    g.fillPath(outerPath);

    // 2. Clipped interior metallic dark console bar gradient
    {
        juce::Graphics::ScopedSaveState save(g);
        g.reduceClipRegion(outerPath);

        const auto topBg = EngineeredDepthTheme::Colors::surface1.interpolatedWith(
            EngineeredDepthTheme::Colors::surface2, 0.20f);
        const auto midBg = EngineeredDepthTheme::Colors::surface1.darker(0.38f);
        const auto botBg = EngineeredDepthTheme::Colors::bg.darker(0.26f);

        juce::ColourGradient panelGrad(topBg,
                                       chassisBounds.getCentreX(), chassisBounds.getY(),
                                       botBg,
                                       chassisBounds.getCentreX(), chassisBounds.getBottom(),
                                       false);
        panelGrad.addColour(0.32, midBg);
        panelGrad.addColour(0.84, EngineeredDepthTheme::Colors::bg);
        g.setGradientFill(panelGrad);
        g.fillRect(chassisBounds);

        // Soft upper brushed metallic wash matching PrimaryKnobsPanel
        juce::ColourGradient topWash(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.075f),
                                     chassisBounds.getCentreX(), chassisBounds.getY(),
                                     juce::Colours::transparentBlack,
                                     chassisBounds.getCentreX(),
                                     chassisBounds.getY() + chassisBounds.getHeight() * 0.30f,
                                     false);
        g.setGradientFill(topWash);
        g.fillRect(chassisBounds);
    }

    // 3. Outer contour bevel & subtle metallic top rail catch-line
    g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.90f));
    g.strokePath(outerPath, juce::PathStrokeType(1.2f));

    g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(0.28f));
    g.strokePath(innerBevel, juce::PathStrokeType(0.85f));

    juce::Path topRimHighlight;
    topRimHighlight.startNewSubPath(chassisBounds.getX() + 2.0f, chassisBounds.getY() + chamfer - 1.0f);
    topRimHighlight.lineTo(chassisBounds.getX() + chamfer, chassisBounds.getY() + 1.0f);
    topRimHighlight.lineTo(chassisBounds.getRight() - chamfer, chassisBounds.getY() + 1.0f);
    topRimHighlight.lineTo(chassisBounds.getRight() - 2.0f, chassisBounds.getY() + chamfer - 1.0f);
    g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.30f));
    g.strokePath(topRimHighlight, juce::PathStrokeType(1.05f, juce::PathStrokeType::beveled, juce::PathStrokeType::butt));

    // 4. Vertical section dividers matching the approved master bar
    const float divPad = juce::jlimit(7.0f, 14.0f, chassisBounds.getHeight() * 0.16f);
    const float divTop = chassisBounds.getY() + divPad;
    const float divBot = chassisBounds.getBottom() - divPad;

    for (const float x : dividerX)
    {
        if (x <= chassisBounds.getX() || x >= chassisBounds.getRight())
            continue;

        juce::ColourGradient divGrad(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.25f),
                                     x, divTop,
                                     EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.25f),
                                     x, divBot,
                                     false);
        divGrad.addColour(0.5, EngineeredDepthTheme::Colors::textSecondary.withAlpha(0.62f));
        g.setGradientFill(divGrad);
        g.drawLine(x, divTop, x, divBot, 1.15f);
    }
}

} // namespace AllyGUI
