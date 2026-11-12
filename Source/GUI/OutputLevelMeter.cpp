#include "OutputLevelMeter.h"
#include "../PluginProcessor.h"

#include <algorithm>
#include <cmath>

namespace AllyGUI
{

OutputLevelMeter::OutputLevelMeter(AllyBassAudioProcessor* processorPtr)
    : processor(processorPtr),
      lastRepaintTicksMs(juce::Time::getMillisecondCounter())
{
    setName("outputLevelMeter");
    setTitle("Output Level Meter");
    setDescription("Real-time post-output Peak and RMS meter");
    setWantsKeyboardFocus(false);
    setOpaque(false);

    if (processor != nullptr)
        startTimerHz(45);
}

OutputLevelMeter::~OutputLevelMeter()
{
    stopTimer();
}

void OutputLevelMeter::setProcessor(AllyBassAudioProcessor* processorPtr) noexcept
{
    processor = processorPtr;
    if (processor != nullptr)
    {
        lastRepaintTicksMs = juce::Time::getMillisecondCounter();
        if (!isTimerRunning())
            startTimerHz(45);
    }
    else
    {
        stopTimer();
    }
}

float OutputLevelMeter::linearToDbSafe(float linear) noexcept
{
    if (!std::isfinite(linear) || linear <= 1e-4f)
        return floorSilenceDb;
    const float db = 20.0f * std::log10(linear);
    return juce::jlimit(floorSilenceDb, 12.0f, db);
}

int OutputLevelMeter::dbToSegmentCount(float db) noexcept
{
    if (db <= minDisplayDb + 0.05f)
        return 0;
    const float norm = juce::jlimit(0.0f, 1.0f, (db - minDisplayDb) / (maxDisplayDb - minDisplayDb));
    return juce::jlimit(1, numSegments, juce::roundToInt(std::ceil(norm * static_cast<float>(numSegments))));
}

int OutputLevelMeter::getPeakHoldSegmentIndex() const noexcept
{
    const int count = dbToSegmentCount(peakHoldDb);
    return count > 0 ? (count - 1) : -1;
}

void OutputLevelMeter::reset() noexcept
{
    displayedPeakDb = floorSilenceDb;
    displayedRmsDb  = floorSilenceDb;
    peakHoldDb      = floorSilenceDb;
    peakHoldTimerSec = 0.0f;
    clipHoldTimerSec = 0.0f;
    staleCounterFrames = 0;
    settledSilent = true;
    repaint();
}

void OutputLevelMeter::clearClipHold() noexcept
{
    if (clipHoldTimerSec > 0.0f)
    {
        clipHoldTimerSec = 0.0f;
        repaint();
    }
}

void OutputLevelMeter::advanceWithSampledLevels(float peakLinear,
                                                float rmsLinear,
                                                bool clipped,
                                                float deltaSeconds) noexcept
{
    const float dt = juce::jlimit(0.001f, 0.100f, deltaSeconds);

    const float prevPeakDb  = displayedPeakDb;
    const float prevRmsDb   = displayedRmsDb;
    const float prevHoldDb  = peakHoldDb;
    const bool  prevClip    = (clipHoldTimerSec > 0.0f);

    const float targetPeakDb = linearToDbSafe(peakLinear);
    const float targetRmsDb  = linearToDbSafe(rmsLinear);

    // Peak: instant upward tracking with smooth 220ms release
    if (targetPeakDb >= displayedPeakDb)
    {
        displayedPeakDb = targetPeakDb;
    }
    else
    {
        const float releaseCoeff = 1.0f - std::exp(-dt / 0.220f);
        displayedPeakDb += (targetPeakDb - displayedPeakDb) * releaseCoeff;
        if (displayedPeakDb < floorSilenceDb + 0.05f && targetPeakDb <= floorSilenceDb + 0.05f)
            displayedPeakDb = floorSilenceDb;
    }

    // RMS: 45ms integration attack, 250ms release
    const float rmsTau = (targetRmsDb >= displayedRmsDb) ? 0.045f : 0.250f;
    const float rmsCoeff = 1.0f - std::exp(-dt / rmsTau);
    displayedRmsDb += (targetRmsDb - displayedRmsDb) * rmsCoeff;
    if (displayedRmsDb < floorSilenceDb + 0.05f && targetRmsDb <= floorSilenceDb + 0.05f)
        displayedRmsDb = floorSilenceDb;

    // Peak-hold indicator: 420ms hold, then 180ms smooth fall
    if (displayedPeakDb >= peakHoldDb)
    {
        peakHoldDb = displayedPeakDb;
        peakHoldTimerSec = (displayedPeakDb > minDisplayDb) ? 0.420f : 0.0f;
    }
    else if (peakHoldTimerSec > 0.0f)
    {
        peakHoldTimerSec = juce::jmax(0.0f, peakHoldTimerSec - dt);
    }
    else
    {
        const float holdRelease = 1.0f - std::exp(-dt / 0.180f);
        peakHoldDb += (displayedPeakDb - peakHoldDb) * holdRelease;
        if (peakHoldDb < floorSilenceDb + 0.05f && displayedPeakDb <= floorSilenceDb + 0.05f)
            peakHoldDb = floorSilenceDb;
    }

    // Clip hold ONLY on actual overload (>= 0.0 dBFS / >= 1.0 linear)
    if (clipped || (std::isfinite(peakLinear) && peakLinear >= 1.0f))
        clipHoldTimerSec = 1.400f;
    else if (clipHoldTimerSec > 0.0f)
        clipHoldTimerSec = juce::jmax(0.0f, clipHoldTimerSec - dt);

    const bool nowClip = (clipHoldTimerSec > 0.0f);
    const bool nowSilent = (displayedPeakDb <= minDisplayDb
                         && displayedRmsDb <= minDisplayDb
                         && peakHoldDb <= minDisplayDb
                         && !nowClip);

    const bool visualChanged = (dbToSegmentCount(prevPeakDb) != dbToSegmentCount(displayedPeakDb))
                            || (dbToSegmentCount(prevRmsDb)  != dbToSegmentCount(displayedRmsDb))
                            || (dbToSegmentCount(prevHoldDb) != dbToSegmentCount(peakHoldDb))
                            || (prevClip != nowClip)
                            || (!nowSilent && (std::abs(prevPeakDb - displayedPeakDb) > 0.25f
                                            || std::abs(prevRmsDb  - displayedRmsDb)  > 0.25f));

    if (visualChanged || (!settledSilent && nowSilent))
    {
        settledSilent = nowSilent;
        repaint();
    }
}

void OutputLevelMeter::timerCallback()
{
    if (processor == nullptr)
        return;

    const uint32_t nowMs = juce::Time::getMillisecondCounter();
    const float dt = (lastRepaintTicksMs > 0 && nowMs >= lastRepaintTicksMs)
        ? juce::jlimit(0.005f, 0.100f, static_cast<float>(nowMs - lastRepaintTicksMs) * 0.001f)
        : (1.0f / 45.0f);
    lastRepaintTicksMs = nowMs;

    const auto metrics = processor->consumeOutputMeterMetrics();

    float peakSample = metrics.peakLinear;
    float rmsSample  = metrics.rmsLinear;
    bool clipped     = metrics.clipped;

    if (metrics.updateCounter != lastSeenMeterCounter)
    {
        lastSeenMeterCounter = metrics.updateCounter;
        staleCounterFrames = 0;
    }
    else
    {
        // Host audio suspended/paused without releaseResources(): fall smoothly to silence after ~90 ms
        if (++staleCounterFrames > 4)
        {
            peakSample = 0.0f;
            rmsSample  = 0.0f;
            clipped    = false;
        }
    }

    if (settledSilent && peakSample <= 1e-4f && rmsSample <= 1e-4f && !clipped)
        return;

    advanceWithSampledLevels(peakSample, rmsSample, clipped, dt);
}

juce::Rectangle<float> OutputLevelMeter::getHousingBounds() const noexcept
{
    return getLocalBounds().toFloat().reduced(0.5f, 0.5f);
}

void OutputLevelMeter::mouseDown(const juce::MouseEvent&)
{
    clearClipHold();
}

void OutputLevelMeter::paint(juce::Graphics& g)
{
    const auto housing = getHousingBounds();
    if (housing.getWidth() < 6.0f || housing.getHeight() < 24.0f)
        return;

    // 1. Subtle dark recessed meter housing (no heavy contrasting frame border)
    const float radius = juce::jlimit(2.0f, 4.0f, housing.getWidth() * 0.22f);
    g.setColour(EngineeredDepthTheme::Colors::black.withAlpha(0.42f));
    g.fillRoundedRectangle(housing, radius);
    g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.18f));
    g.drawRoundedRectangle(housing, radius, 0.75f);

    // 2. Segmented LED matrix area
    const float hPad = juce::jlimit(1.2f, 2.2f, housing.getWidth() * 0.12f);
    const float vPad = juce::jlimit(1.8f, 3.0f, housing.getHeight() * 0.02f);
    const auto matrix = housing.reduced(hPad, vPad);
    if (matrix.getWidth() < 3.0f || matrix.getHeight() < 16.0f)
        return;

    const float pitch = matrix.getHeight() / static_cast<float>(numSegments);
    const float gap   = juce::jlimit(1.4f, 2.8f, pitch * 0.30f);
    const float segH  = juce::jmax(1.5f, pitch - gap);
    const float segCorner = juce::jlimit(0.8f, 1.8f, segH * 0.32f);

    const int peakCount = dbToSegmentCount(displayedPeakDb);
    const int rmsCount  = dbToSegmentCount(displayedRmsDb);
    const int holdIdx   = getPeakHoldSegmentIndex();
    const bool clipOn   = (clipHoldTimerSec > 0.0f);

    for (int i = 0; i < numSegments; ++i)
    {
        // Segment 0 at bottom, 15 at top
        const float segTop = matrix.getBottom() - static_cast<float>(i + 1) * pitch + gap * 0.5f;
        const juce::Rectangle<float> segRect(matrix.getX(), segTop, matrix.getWidth(), segH);

        // Determine per-segment level zone based on nominal dBFS boundaries:
        // [0..13] (< -2 dBFS): crisp Cyan
        // [14] (~ -3.4 .. -0.2 dBFS): cyan normally; amber warning ONLY when signal hits >= -1 dBFS
        // [15] (0 dBFS / +3 dBFS overload): crisp Red ONLY on actual >= 0.0 dBFS clipping!
        const bool isTopClipSeg = (i == numSegments - 1);
        const bool isWarnSeg    = (i == numSegments - 2);

        const bool litRms   = (i < rmsCount);
        const bool litPeak  = (i < peakCount);
        const bool litHold  = (i == holdIdx) && (holdIdx >= 0);
        const bool litClip  = isTopClipSeg && clipOn;

        juce::Colour offFill = isTopClipSeg
            ? juce::Colour(0xff161116)
            : EngineeredDepthTheme::Colors::trackDark.darker(0.36f);

        // Unlit LED slot background + subtle bevel top sheen
        g.setColour(offFill);
        g.fillRoundedRectangle(segRect, segCorner);

        if (!litRms && !litPeak && !litHold && !litClip)
        {
            g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.15f));
            g.fillRoundedRectangle(segRect.withHeight(juce::jmin(1.0f, segRect.getHeight() * 0.28f)), segCorner);
            continue;
        }

        // Active LED colour selection (keep nominal peak-hold crisp cyan, reserve red/amber for overload)
        juce::Colour ledColour = EngineeredDepthTheme::Colors::cyan;
        juce::Colour ledCore   = EngineeredDepthTheme::Colors::cyanBright;
        float alpha = 0.56f;

        if (litClip || (isTopClipSeg && (litPeak || litHold)))
        {
            ledColour = EngineeredDepthTheme::Colors::red;
            ledCore   = EngineeredDepthTheme::Colors::redBright;
            alpha     = 0.98f;
        }
        else if (isWarnSeg && displayedPeakDb >= -1.0f)
        {
            ledColour = EngineeredDepthTheme::Colors::warning;
            ledCore   = EngineeredDepthTheme::Colors::textPrimary;
            alpha     = (litRms || litHold) ? 0.94f : 0.72f;
        }
        else if (litHold)
        {
            ledColour = EngineeredDepthTheme::Colors::cyan;
            ledCore   = EngineeredDepthTheme::Colors::cyanBright;
            alpha     = 1.00f;
        }
        else if (litRms)
        {
            ledColour = EngineeredDepthTheme::Colors::cyan;
            ledCore   = EngineeredDepthTheme::Colors::cyanBright;
            alpha     = 0.90f;
        }
        else
        {
            // Between RMS and instant Peak envelope
            ledColour = EngineeredDepthTheme::Colors::cyan;
            ledCore   = EngineeredDepthTheme::Colors::cyanBright;
            alpha     = 0.45f;
        }

        // Soft feathered LED glow + crisp body + inner horizontal phosphor core
        g.setColour(ledColour.withAlpha(alpha * 0.20f));
        g.fillRoundedRectangle(segRect.expanded(1.8f, 1.2f), segCorner + 1.0f);

        g.setColour(ledColour.withAlpha(alpha));
        g.fillRoundedRectangle(segRect, segCorner);

        if (alpha > 0.65f)
        {
            const float coreAlpha = litHold ? 0.34f : (alpha * 0.24f);
            g.setColour(ledCore.withAlpha(coreAlpha));
            g.fillRoundedRectangle(segRect.reduced(1.0f, juce::jmax(0.5f, segRect.getHeight() * 0.30f)), segCorner * 0.6f);
        }
    }
}

void OutputLevelMeter::resized()
{
}

} // namespace AllyGUI
