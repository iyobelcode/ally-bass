#pragma once

#include <atomic>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthTheme.h"

class AllyBassAudioProcessor;

namespace AllyGUI
{

/**
 * Reusable Engineered Depth segmented vertical Output Peak/RMS Meter.
 *
 * Reads lock-free post-masterGain telemetry from AllyBassAudioProcessor on a bounded
 * GUI refresh timer, rendering 16 horizontal LED segments matching ALLY1300_APPROVED_MASTER_FULL.png:
 * - RMS core body + Peak crest + latched Peak-Hold cap in crisp cyan
 * - Amber/Red top overload indication ONLY at >= -1.0 dBFS / >= 0.0 dBFS real clipping
 * - Decays cleanly to dark idle on silence and pauses unnecessary idle repaints
 */
class OutputLevelMeter final : public juce::Component,
                               private juce::Timer
{
public:
    static constexpr int numSegments = 16;
    static constexpr float minDisplayDb = -48.0f;
    static constexpr float maxDisplayDb = 3.0f;
    static constexpr float floorSilenceDb = -60.0f;

    explicit OutputLevelMeter(AllyBassAudioProcessor* processorPtr = nullptr);
    ~OutputLevelMeter() override;

    void setProcessor(AllyBassAudioProcessor* processorPtr) noexcept;

    /** Advances ballistics deterministically (also used directly by automated unit tests). */
    void advanceWithSampledLevels(float peakLinear,
                                  float rmsLinear,
                                  bool clipped,
                                  float deltaSeconds) noexcept;

    void reset() noexcept;
    void clearClipHold() noexcept;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& e) override;

    // Accessors for deterministic unit/UI inspection
    float getDisplayedPeakDb() const noexcept { return displayedPeakDb; }
    float getDisplayedRmsDb() const noexcept  { return displayedRmsDb; }
    float getPeakHoldDb() const noexcept      { return peakHoldDb; }
    bool  isClipLatched() const noexcept      { return clipHoldTimerSec > 0.0f; }
    int   getLitPeakSegments() const noexcept { return dbToSegmentCount(displayedPeakDb); }
    int   getLitRmsSegments() const noexcept  { return dbToSegmentCount(displayedRmsDb); }
    int   getPeakHoldSegmentIndex() const noexcept;

    static float linearToDbSafe(float linear) noexcept;
    static int   dbToSegmentCount(float db) noexcept;

private:
    void timerCallback() override;
    juce::Rectangle<float> getHousingBounds() const noexcept;

    AllyBassAudioProcessor* processor = nullptr;

    float displayedPeakDb = floorSilenceDb;
    float displayedRmsDb  = floorSilenceDb;
    float peakHoldDb      = floorSilenceDb;
    float peakHoldTimerSec = 0.0f;
    float clipHoldTimerSec = 0.0f;

    uint64_t lastSeenMeterCounter = 0;
    int      staleCounterFrames   = 0;
    uint32_t lastRepaintTicksMs   = 0;
    bool     settledSilent        = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputLevelMeter)
};

} // namespace AllyGUI
