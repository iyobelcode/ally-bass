#include "NapWaveVisualizer.h"
#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace AllyGUI
{

namespace
{
// 17 smooth topographic contour tracks matching Phase 5 nap_contours.svg (viewBox 640 x 320, x sampled every 32px)
struct ContourPolylineSpec
{
    bool emphasized;
    std::array<float, 21> yCoords; // Normalized in 0..320 SVG space across x = 0, 32, 64, ..., 640
};

static const std::array<ContourPolylineSpec, 17> kNapContours = {{
    { true,  { 32.8f, 39.8f, 39.0f, 34.9f, 32.2f, 27.1f, 17.7f, 10.4f, 11.4f, 17.2f, 21.2f, 24.4f, 32.3f, 38.2f, 35.9f, 27.7f, 21.4f, 18.0f, 13.4f,  6.5f,  8.7f } },
    { false, { 50.4f, 55.6f, 52.7f, 47.2f, 43.2f, 37.4f, 28.2f, 22.2f, 24.9f, 32.1f, 37.2f, 40.6f, 47.7f, 52.7f, 49.5f, 40.2f, 32.6f, 29.1f, 25.4f, 19.8f, 22.0f } },
    { false, { 67.7f, 70.6f, 65.4f, 58.5f, 53.5f, 47.4f, 38.9f, 34.6f, 39.1f, 47.6f, 53.6f, 57.0f, 63.0f, 66.6f, 62.5f, 52.2f, 43.6f, 40.1f, 37.4f, 33.3f, 35.8f } },
    { false, { 84.3f, 84.4f, 77.0f, 69.0f, 63.5f, 57.4f, 50.2f, 47.8f, 54.1f, 63.8f, 70.3f, 73.4f, 77.9f, 80.2f, 75.0f, 63.8f, 54.5f, 51.0f, 49.4f, 47.2f, 50.1f } },
    { true,  { 99.8f, 97.0f, 87.5f, 78.7f, 73.2f, 67.7f, 62.1f, 61.9f, 69.9f, 80.6f, 87.1f, 89.6f, 92.5f, 93.2f, 87.0f, 75.1f, 65.2f, 62.0f, 61.5f, 61.4f, 64.8f } },
    { false, { 113.9f, 108.2f, 97.1f, 88.0f, 82.9f, 78.6f, 74.9f, 76.9f, 86.4f, 97.7f, 104.0f, 105.5f, 106.4f, 105.5f, 98.3f, 86.0f, 76.0f, 73.1f, 73.9f, 76.0f, 80.1f } },
    { false, { 126.5f, 118.0f, 105.8f, 97.1f, 93.1f, 90.3f, 88.7f, 92.8f, 103.7f, 115.1f, 120.6f, 121.0f, 119.8f, 117.1f, 109.1f, 96.6f, 86.8f, 84.5f, 86.6f, 91.0f, 95.8f } },
    { false, { 137.6f, 126.8f, 114.1f, 106.4f, 103.9f, 103.0f, 103.7f, 109.7f, 121.5f, 132.6f, 136.9f, 135.7f, 132.3f, 128.1f, 119.4f, 107.1f, 97.7f, 96.2f, 99.7f, 106.4f, 112.0f } },
    { true,  { 147.1f, 134.6f, 122.3f, 116.2f, 115.7f, 116.9f, 119.7f, 127.4f, 139.6f, 149.8f, 152.6f, 149.7f, 144.1f, 138.4f, 129.3f, 117.5f, 109.0f, 108.5f, 113.3f, 122.3f, 128.6f } },
    { false, { 155.4f, 142.0f, 130.8f, 126.9f, 128.7f, 132.1f, 136.8f, 145.7f, 157.7f, 166.7f, 167.5f, 162.8f, 155.0f, 148.1f, 139.0f, 127.9f, 120.7f, 121.3f, 127.6f, 138.5f, 145.5f } },
    { false, { 162.7f, 149.3f, 140.1f, 138.9f, 143.1f, 148.5f, 154.8f, 164.3f, 175.7f, 182.9f, 181.5f, 174.9f, 165.2f, 157.4f, 148.5f, 138.7f, 132.9f, 134.8f, 142.4f, 155.2f, 162.7f } },
    { false, { 169.5f, 157.1f, 150.5f, 152.2f, 158.9f, 166.1f, 173.5f, 183.1f, 193.2f, 198.1f, 194.4f, 186.0f, 174.6f, 166.4f, 158.1f, 149.8f, 145.8f, 149.1f, 157.9f, 172.2f, 180.0f } },
    { true,  { 176.4f, 165.9f, 162.4f, 167.2f, 176.2f, 184.6f, 192.5f, 201.5f, 209.9f, 212.3f, 206.1f, 196.0f, 183.5f, 175.2f, 168.0f, 161.5f, 159.4f, 164.1f, 174.1f, 189.6f, 197.4f } },
    { false, { 183.9f, 176.0f, 176.0f, 183.9f, 194.6f, 203.9f, 211.6f, 219.4f, 225.5f, 225.3f, 216.6f, 205.2f, 191.9f, 184.1f, 178.4f, 174.0f, 173.9f, 180.0f, 191.0f, 207.2f, 214.7f } },
    { false, { 192.6f, 187.9f, 191.5f, 202.1f, 214.1f, 223.4f, 230.3f, 236.4f, 239.9f, 236.9f, 226.1f, 213.6f, 200.2f, 193.3f, 189.4f, 187.3f, 189.3f, 196.7f, 208.4f, 224.8f, 231.8f } },
    { false, { 203.0f, 201.8f, 208.9f, 221.7f, 234.2f, 242.8f, 248.3f, 252.1f, 252.9f, 247.1f, 234.5f, 221.4f, 208.5f, 203.1f, 201.3f, 201.6f, 205.7f, 214.1f, 226.2f, 242.5f, 248.5f } },
    { true,  { 215.3f, 217.8f, 228.0f, 242.2f, 254.5f, 261.7f, 265.1f, 266.4f, 264.3f, 256.2f, 242.2f, 229.0f, 217.2f, 213.6f, 214.2f, 217.0f, 222.9f, 232.2f, 244.4f, 259.9f, 264.7f } }
}};

float softVisualClip(float x) noexcept
{
    const float absX = std::abs(x);
    if (absX <= 0.96f)
        return x;
    const float excess = absX - 0.96f;
    const float shaped = 0.96f + 0.16f * std::tanh(excess / 0.25f);
    return (x >= 0.0f) ? shaped : -shaped;
}
} // namespace

NapWaveVisualizer::NapWaveVisualizer(AllyBassAudioProcessor& p)
    : processor(p)
{
    setName("weerwaterVectorScope");
    setTitle("Weerwater Vector Scope");
    setDescription("Real-time sub-bass waveform display calibrated to -4.5 m NAP");
    setOpaque(false);
    setWantsKeyboardFocus(false);

    displayWaveform.fill(0.0f);
    waveStrokePath.preallocateSpace(1024);
    waveFillPath.preallocateSpace(1024);

    overdriveState = processor.isOverdriveActive();
    monoLockState  = processor.isMonoLockActive();

    startTimerHz(45);
}

NapWaveVisualizer::~NapWaveVisualizer()
{
    stopTimer();
}

juce::String NapWaveVisualizer::getDisplayedMonoStatusText(bool compact) const
{
    if (compact)
        return monoLockState ? "1300 MONO // LOCK: ACTIVE"
                             : "1300 MONO // LOCK: BYPASSED";

    return monoLockState ? "DUAL MONO ENGINE // 1300 LOCK: ACTIVE"
                         : "DUAL MONO ENGINE // 1300 LOCK: BYPASSED";
}

void NapWaveVisualizer::timerCallback()
{
    const uint64_t writeCounter = processor.getVisualizerWriteCounter();
    const bool overdrive = processor.isOverdriveActive();
    const bool monoLock  = processor.isMonoLockActive();

    if (writeCounter != lastWriteCounter)
    {
        lastWriteCounter = writeCounter;
        staleCounterTicks = 0;
        processor.getVisualizerData(captureScratch.data(), captureSampleCount);
        updateScopeWithSamples(captureScratch.data(),
                               captureSampleCount,
                               overdrive,
                               monoLock,
                               processor.getCurrentSampleRate());
        return;
    }

    // Host paused/suspended without sending trailing zero buffers: settle smoothly onto flat NAP datum
    if (++staleCounterTicks > 4)
    {
        if (!settledSilent || overdrive != overdriveState || monoLock != monoLockState)
        {
            captureScratch.fill(0.0f);
            updateScopeWithSamples(captureScratch.data(),
                                   captureSampleCount,
                                   overdrive,
                                   monoLock,
                                   processor.getCurrentSampleRate());
        }
    }
    else if (overdrive != overdriveState || monoLock != monoLockState)
    {
        overdriveState = overdrive;
        monoLockState  = monoLock;
        repaint();
    }
}

void NapWaveVisualizer::updateScopeWithSamples(const float* samples,
                                               int numSamples,
                                               bool overdriveActive,
                                               bool monoLockActive,
                                               double sampleRateHz)
{
    const bool stateChanged = (overdriveActive != overdriveState) || (monoLockActive != monoLockState);
    overdriveState = overdriveActive;
    monoLockState  = monoLockActive;

    if (samples == nullptr || numSamples <= 4)
    {
        displayWaveform.fill(0.0f);
        renderedWavePeak = 0.0f;
        if (!settledSilent || stateChanged)
        {
            settledSilent = true;
            repaint();
        }
        return;
    }

    // Measure newest active tail (~768 samples or up to numSamples)
    const int tailCheck = juce::jmin(numSamples, 768);
    const int tailStart = numSamples - tailCheck;
    float recentPeak = 0.0f;
    for (int i = tailStart; i < numSamples; ++i)
    {
        const float v = std::isfinite(samples[i]) ? samples[i] : 0.0f;
        recentPeak = juce::jmax(recentPeak, std::abs(v));
    }

    // Also inspect full block so fast transients don't vanish cleanly prematurely
    float fullPeak = recentPeak;
    if (recentPeak < silenceThreshold)
    {
        for (int i = 0; i < tailStart; ++i)
        {
            const float v = std::isfinite(samples[i]) ? samples[i] : 0.0f;
            fullPeak = juce::jmax(fullPeak, std::abs(v));
        }
    }

    const float targetPeak = juce::jmax(recentPeak, fullPeak * 0.65f);
    if (targetPeak >= renderedWavePeak)
        renderedWavePeak = targetPeak;
    else
        renderedWavePeak = renderedWavePeak * 0.80f + targetPeak * 0.20f;

    if (recentPeak < silenceThreshold && renderedWavePeak < silenceThreshold)
    {
        renderedWavePeak = 0.0f;
        displayWaveform.fill(0.0f);
        if (!settledSilent || stateChanged)
        {
            settledSilent = true;
            repaint();
        }
        return;
    }

    settledSilent = false;

    // Choose display window duration (~26ms -> ~1.7 cycles of 65Hz, ~3 cycles of 110Hz) adapted cleanly to input count
    const double sr = sampleRateHz > 8000.0 ? sampleRateHz : 44100.0;
    const int idealSpan = juce::jlimit(64, numSamples, juce::roundToInt(sr * 0.026));
    const int span = juce::jmin(numSamples, idealSpan);

    // Search backwards from newest anchor for clean rising zero-crossing so steady bass notes lock cleanly
    int windowStart = numSamples - span;
    if (windowStart > 2)
    {
        const int searchLimit = juce::jmax(1, windowStart - juce::jmin(windowStart, 384));
        for (int i = windowStart; i >= searchLimit; --i)
        {
            const float prev = std::isfinite(samples[i - 1]) ? samples[i - 1] : 0.0f;
            const float curr = std::isfinite(samples[i]) ? samples[i] : 0.0f;
            if (prev <= 0.0f && curr > 0.0f)
            {
                windowStart = i;
                break;
            }
        }
    }

    const int availableSpan = juce::jmax(2, juce::jmin(span, numSamples - windowStart));
    for (int p = 0; p < displayPointCount; ++p)
    {
        const float t = (static_cast<float>(p) / static_cast<float>(displayPointCount - 1))
                      * static_cast<float>(availableSpan - 1);
        const int idx0 = juce::jlimit(0, numSamples - 1, windowStart + static_cast<int>(t));
        const int idx1 = juce::jlimit(0, numSamples - 1, idx0 + 1);
        const float frac = t - std::floor(t);

        const float s0 = std::isfinite(samples[idx0]) ? samples[idx0] : 0.0f;
        const float s1 = std::isfinite(samples[idx1]) ? samples[idx1] : 0.0f;
        displayWaveform[static_cast<size_t>(p)] = s0 + (s1 - s0) * frac;
    }

    repaint();
}

void NapWaveVisualizer::resized()
{
    const auto local = getLocalBounds().toFloat();
    if (local.getWidth() < 120.0f || local.getHeight() < 56.0f)
    {
        panelBounds  = {};
        headerBounds = {};
        scaleBounds  = {};
        plotBounds   = {};
        rebuildStaticLayers();
        return;
    }

    panelBounds = local.reduced(1.0f, 1.5f);

    const float padX   = juce::jlimit(8.0f, 14.0f, panelBounds.getWidth() * 0.014f);
    const float padTop = juce::jlimit(4.0f, 8.0f, panelBounds.getHeight() * 0.045f);
    const float padBot = juce::jlimit(6.0f, 10.0f, panelBounds.getHeight() * 0.062f);
    const float hdrH   = juce::jlimit(16.0f, 22.0f, panelBounds.getHeight() * 0.145f);

    headerBounds = juce::Rectangle<float>(panelBounds.getX() + padX,
                                          panelBounds.getY() + padTop,
                                          panelBounds.getWidth() - padX * 2.0f,
                                          hdrH);

    const float contentTop = headerBounds.getBottom() + 2.0f;
    const float contentBot = panelBounds.getBottom() - padBot;
    const float scaleW     = juce::jlimit(62.0f, 76.0f, panelBounds.getWidth() * 0.095f);

    scaleBounds = juce::Rectangle<float>(panelBounds.getX() + padX * 0.75f,
                                         contentTop,
                                         scaleW,
                                         juce::jmax(20.0f, contentBot - contentTop));

    plotBounds = juce::Rectangle<float>(scaleBounds.getRight() + 6.0f,
                                        contentTop,
                                        juce::jmax(40.0f, panelBounds.getRight() - padX - (scaleBounds.getRight() + 6.0f)),
                                        juce::jmax(20.0f, contentBot - contentTop));

    rebuildStaticLayers();
}

void NapWaveVisualizer::rebuildStaticLayers()
{
    cachedContoursPrimary.clear();
    cachedContoursSecondary.clear();
    cachedGridLines.clear();
    cachedDatumLine.clear();

    if (plotBounds.isEmpty())
        return;

    const float px = plotBounds.getX();
    const float py = plotBounds.getY();
    const float pw = plotBounds.getWidth();
    const float ph = plotBounds.getHeight();

    // 1. Technical Blueprint Coordinate Grid (matching Phase 5 scope_static_grid 6-row x ~22-col cadence)
    constexpr int horizRows = 6;
    for (int r = 1; r < horizRows; ++r)
    {
        if (r == 3)
            continue; // Centerline gets dedicated dashed NAP datum line
        const float y = py + ph * (static_cast<float>(r) / static_cast<float>(horizRows));
        cachedGridLines.startNewSubPath(px, y);
        cachedGridLines.lineTo(px + pw, y);
    }

    const int vertCols = juce::jlimit(12, 26, juce::roundToInt(pw / 32.0f));
    for (int c = 1; c < vertCols; ++c)
    {
        const float x = px + pw * (static_cast<float>(c) / static_cast<float>(vertCols));
        cachedGridLines.startNewSubPath(x, py);
        cachedGridLines.lineTo(x, py + ph);
    }

    // 2. Dashed Horizontal -4.5 m NAP Reference Datum Line at exact plot centerline
    const float napY = plotBounds.getCentreY();
    {
        juce::Path baseDatum;
        baseDatum.startNewSubPath(px, napY);
        baseDatum.lineTo(px + pw, napY);

        const float dashes[2] = { 5.5f, 5.5f };
        juce::PathStrokeType(1.35f).createDashedStroke(cachedDatumLine, baseDatum, dashes, 2);
    }

    // 3. Topographic NAP Contours subtly receding beneath the -4.5m NAP waterline into seabed depth
    // Mapped across x in [0..640], y normalized so contours emerge softly around mid-height downward
    const float contourTopY = py + ph * 0.20f;
    const float contourH    = ph * 0.92f;

    for (const auto& spec : kNapContours)
    {
        juce::Path p;
        for (size_t i = 0; i < spec.yCoords.size(); ++i)
        {
            const float nx = static_cast<float>(i) / static_cast<float>(spec.yCoords.size() - 1);
            const float ny = spec.yCoords[i] / 270.0f;
            const float x  = px + nx * pw;
            const float y  = contourTopY + ny * contourH;

            if (i == 0)
                p.startNewSubPath(x, y);
            else
                p.lineTo(x, y);
        }

        if (spec.emphasized)
            cachedContoursPrimary.addPath(p);
        else
            cachedContoursSecondary.addPath(p);
    }
}

void NapWaveVisualizer::paint(juce::Graphics& g)
{
    if (panelBounds.isEmpty() || plotBounds.isEmpty())
        return;

    const float radius = juce::jlimit(8.0f, 12.0f, panelBounds.getHeight() * 0.085f);

    // 1. Engineered Depth Metallic Frame & Deep Abyssal Scope Housing
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.88f));
    g.fillRoundedRectangle(panelBounds, radius);

    {
        juce::ColourGradient frameGrad(
            EngineeredDepthTheme::Colors::surface1.interpolatedWith(EngineeredDepthTheme::Colors::bg, 0.45f),
            panelBounds.getCentreX(), panelBounds.getY(),
            EngineeredDepthTheme::Colors::bg.darker(0.35f),
            panelBounds.getCentreX(), panelBounds.getBottom(),
            false);
        g.setGradientFill(frameGrad);
        g.fillRoundedRectangle(panelBounds.reduced(1.0f), juce::jmax(6.0f, radius - 1.0f));
    }

    g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.85f));
    g.drawRoundedRectangle(panelBounds.reduced(0.5f), radius, 1.15f);

    g.setColour(EngineeredDepthTheme::Colors::metalRimLight.withAlpha(0.22f));
    g.drawLine(panelBounds.getX() + radius, panelBounds.getY() + 1.0f,
               panelBounds.getRight() - radius, panelBounds.getY() + 1.0f, 1.0f);

    // 2. Top Header Strip inside Scope:
    // Left: // WEERWATER VECTOR SCOPE
    // Right: Factual Mono Status (NO fake 100% percentage)
    {
        const float hdrH = headerBounds.getHeight();
        const float slashH = juce::jlimit(8.0f, 12.0f, hdrH * 0.56f);
        const float slashW = 2.6f;
        const float slashSkew = 4.2f;
        const float sy0 = headerBounds.getCentreY() + slashH * 0.5f;
        const float sy1 = headerBounds.getCentreY() - slashH * 0.5f;
        const float sx0 = headerBounds.getX() + 1.0f;

        auto makeSlash = [&](float xOffset)
        {
            juce::Path s;
            s.startNewSubPath(xOffset, sy0);
            s.lineTo(xOffset + slashSkew, sy1);
            s.lineTo(xOffset + slashSkew + slashW, sy1);
            s.lineTo(xOffset + slashW, sy0);
            s.closeSubPath();
            return s;
        };

        const juce::Path s1 = makeSlash(sx0);
        const juce::Path s2 = makeSlash(sx0 + 5.6f);

        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.24f));
        g.strokePath(s1, juce::PathStrokeType(2.0f));
        g.strokePath(s2, juce::PathStrokeType(2.0f));
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.fillPath(s1);
        g.fillPath(s2);

        const float titleSize = juce::jlimit(9.5f, 11.5f, hdrH * 0.58f);
        const auto titleFont = EngineeredDepthTheme::makeUiFont(titleSize, true);
        const auto titleBox = headerBounds.withTrimmedLeft(18.0f).withWidth(headerBounds.getWidth() * 0.46f);
        EngineeredDepthTheme::drawTrackedText(g,
                                            "WEERWATER VECTOR SCOPE",
                                            titleBox,
                                            titleFont,
                                            EngineeredDepthTheme::Colors::textPrimary,
                                            0.09f,
                                            juce::Justification::centredLeft);

        // Right factual status (adapts compact prefix cleanly at 680 px minimum width)
        const bool compactStatus = headerBounds.getWidth() < 700.0f;
        const juce::String prefixText = compactStatus
            ? "1300 MONO // LOCK: "
            : "DUAL MONO ENGINE // 1300 LOCK: ";
        const juce::String stateText = monoLockState ? "ACTIVE" : "BYPASSED";

        const float statusSize = juce::jlimit(8.8f, 10.6f, hdrH * 0.53f);
        const auto statusFont = EngineeredDepthTheme::makeDataFont(statusSize, false);
        const auto statusBoldFont = EngineeredDepthTheme::makeDataFont(statusSize, true);

        juce::GlyphArrangement gaPrefix;
        gaPrefix.addLineOfText(statusFont, prefixText, 0.0f, 0.0f);
        juce::GlyphArrangement gaState;
        gaState.addLineOfText(statusBoldFont, stateText, 0.0f, 0.0f);

        const float stateW  = gaState.getBoundingBox(0, -1, false).getWidth() + 4.0f;
        const float prefixW = gaPrefix.getBoundingBox(0, -1, false).getWidth() + 4.0f;

        const auto stateBox = headerBounds.withLeft(headerBounds.getRight() - stateW);
        const auto prefixBox = juce::Rectangle<float>(stateBox.getX() - prefixW,
                                                      headerBounds.getY(),
                                                      prefixW,
                                                      headerBounds.getHeight());

        g.setFont(statusFont);
        g.setColour(EngineeredDepthTheme::Colors::textSecondary.withAlpha(0.86f));
        g.drawText(prefixText, prefixBox, juce::Justification::centredRight, false);

        g.setFont(statusBoldFont);
        g.setColour(monoLockState
                        ? EngineeredDepthTheme::Colors::cyan
                        : EngineeredDepthTheme::Colors::warning.withAlpha(0.88f));
        g.drawText(stateText, stateBox, juce::Justification::centredRight, false);
    }

    // 3. Left Scale Axis Labels (-3.0 m, -4.5 m NAP, -6.0 m) aligned directly to Plot Grid & Datum
    {
        const float rowTopY = plotBounds.getY() + plotBounds.getHeight() * (1.0f / 6.0f);
        const float rowMidY = plotBounds.getCentreY();
        const float rowBotY = plotBounds.getY() + plotBounds.getHeight() * (5.0f / 6.0f);

        const float labelFontSize = juce::jlimit(8.8f, 10.8f, plotBounds.getHeight() * 0.105f);
        const auto labelFont = EngineeredDepthTheme::makeDataFont(labelFontSize, false);
        const auto datumFont = EngineeredDepthTheme::makeDataFont(labelFontSize, true);

        const float labelX = scaleBounds.getX();
        const float labelW = scaleBounds.getWidth();
        const float rowH   = 16.0f;

        g.setFont(labelFont);
        g.setColour(EngineeredDepthTheme::Colors::textSecondary.withAlpha(0.80f));
        g.drawText("-3.0 m", juce::Rectangle<float>(labelX, rowTopY - rowH * 0.5f, labelW, rowH),
                   juce::Justification::centredRight, false);
        g.drawText("-6.0 m", juce::Rectangle<float>(labelX, rowBotY - rowH * 0.5f, labelW, rowH),
                   juce::Justification::centredRight, false);

        g.setFont(datumFont);
        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.drawText("-4.5 m NAP", juce::Rectangle<float>(labelX, rowMidY - rowH * 0.5f, labelW, rowH),
                   juce::Justification::centredRight, false);
    }

    // 4. Seamless Plot Viewport + Clipped Static Layers + Optical Cyan Waveform
    // (No nested box border around plotBounds — screen extends cohesively like master)
    {
        juce::Graphics::ScopedSaveState save(g);
        const float plotRadius = juce::jlimit(4.0f, 7.0f, plotBounds.getHeight() * 0.055f);
        juce::Path clipRectPath;
        clipRectPath.addRoundedRectangle(plotBounds, plotRadius);
        g.reduceClipRegion(clipRectPath);

        // Layer A: Topographic NAP bathymetric contours fading subtly in submerged half
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.085f));
        g.strokePath(cachedContoursSecondary, juce::PathStrokeType(0.9f));

        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.165f));
        g.strokePath(cachedContoursPrimary, juce::PathStrokeType(1.15f));

        // Layer B: Blueprint technical coordinate grid
        g.setColour(EngineeredDepthTheme::Colors::stroke.withAlpha(0.24f));
        g.strokePath(cachedGridLines, juce::PathStrokeType(0.75f));

        // Layer C: Dashed -4.5 m NAP reference baseline
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.45f));
        g.fillPath(cachedDatumLine);

        // Layer D: Live Post-Output Audio Waveform Trace (or calm flat datum line on silence)
        // 0 dBFS (+/-1.0) maps cleanly to -3.0 m / -6.0 m grid lines (+/- 2/6 H), leaving headroom for overdrive!
        const float centerY = plotBounds.getCentreY();
        const float leftX   = plotBounds.getX();
        const float width   = plotBounds.getWidth();
        const float halfAmp = plotBounds.getHeight() * (1.98f / 6.0f);

        if (!isSignalActive())
        {
            // Calm, noise-free resting signal right along -4.5 m NAP (zero fake idle oscillation)
            g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.11f));
            g.drawLine(leftX, centerY, leftX + width, centerY, 5.0f);
            g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.56f));
            g.drawLine(leftX, centerY, leftX + width, centerY, 1.5f);
            g.setColour(EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.72f));
            g.drawLine(leftX, centerY, leftX + width, centerY, 0.70f);
        }
        else
        {
            waveStrokePath.clear();
            waveFillPath.clear();

            const float stepX = width / static_cast<float>(displayPointCount - 1);
            waveStrokePath.startNewSubPath(leftX,
                                           centerY - softVisualClip(displayWaveform[0]) * halfAmp);

            for (int i = 1; i < displayPointCount; ++i)
            {
                const float x = leftX + static_cast<float>(i) * stepX;
                const float y = centerY - softVisualClip(displayWaveform[static_cast<size_t>(i)]) * halfAmp;
                waveStrokePath.lineTo(x, y);
            }

            // Soft submerged underwater gradient fill between wave & -4.5 m NAP datum
            waveFillPath.addPath(waveStrokePath);
            waveFillPath.lineTo(leftX + width, centerY);
            waveFillPath.lineTo(leftX, centerY);
            waveFillPath.closeSubPath();

            juce::ColourGradient fillGrad(EngineeredDepthTheme::Colors::cyan.withAlpha(0.12f),
                                          plotBounds.getCentreX(), plotBounds.getY(),
                                          EngineeredDepthTheme::Colors::cyan.withAlpha(0.10f),
                                          plotBounds.getCentreX(), plotBounds.getBottom(),
                                          false);
            fillGrad.addColour(0.50, juce::Colours::transparentBlack);
            g.setGradientFill(fillGrad);
            g.fillPath(waveFillPath);

            // Feathered multi-step cyan optical bloom (pure cyan + overdrive red fringe if overdrive ON; no murky grey mix)
            if (overdriveState)
            {
                g.setColour(EngineeredDepthTheme::Colors::red.withAlpha(0.11f));
                g.strokePath(waveStrokePath,
                             juce::PathStrokeType(9.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            }

            g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.08f));
            g.strokePath(waveStrokePath,
                         juce::PathStrokeType(9.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.16f));
            g.strokePath(waveStrokePath,
                         juce::PathStrokeType(5.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.45f));
            g.strokePath(waveStrokePath,
                         juce::PathStrokeType(2.8f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

            g.setColour(EngineeredDepthTheme::Colors::cyanBright.withAlpha(0.95f));
            g.strokePath(waveStrokePath,
                         juce::PathStrokeType(1.35f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }
    }
}

} // namespace AllyGUI
