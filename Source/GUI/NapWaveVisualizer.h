#pragma once

#include <array>
#include <cstdint>
#include <juce_gui_basics/juce_gui_basics.h>
#include "EngineeredDepthTheme.h"

class AllyBassAudioProcessor;

namespace AllyGUI
{

/**
 * WEERWATER VECTOR SCOPE (Engineered Depth Edition, Stap C.4B)
 *
 * Displays live post-output waveform audio relative to the -4.5 m NAP datum,
 * overlaid onto topographic vector contours and technical coordinate grid.
 *
 * Thread safety: polls lock-free atomic ring buffer from AllyBassAudioProcessor
 * on a bounded GUI timer with zero audio-thread locks or heap allocations.
 * Displays factual mono engine status alongside live 1300 MONO LOCK state.
 */
class NapWaveVisualizer final : public juce::Component,
                                private juce::Timer
{
public:
    static constexpr int captureSampleCount = 1536;
    static constexpr int displayPointCount  = 256;

    explicit NapWaveVisualizer(AllyBassAudioProcessor& p);
    ~NapWaveVisualizer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Feeds audio samples directly (also enables deterministic unit & visual testing). */
    void updateScopeWithSamples(const float* samples,
                                int numSamples,
                                bool overdriveActive,
                                bool monoLockActive,
                                double sampleRateHz = 44100.0);

    // Deterministic telemetry & layout inspection accessors for regression tests
    juce::Rectangle<float> getPanelBounds() const noexcept { return panelBounds; }
    juce::Rectangle<float> getHeaderBounds() const noexcept { return headerBounds; }
    juce::Rectangle<float> getScaleBounds() const noexcept { return scaleBounds; }
    juce::Rectangle<float> getPlotBounds() const noexcept { return plotBounds; }

    float getRenderedWavePeak() const noexcept { return renderedWavePeak; }
    bool  isSignalActive() const noexcept { return renderedWavePeak >= silenceThreshold; }
    bool  isDisplayedOverdrive() const noexcept { return overdriveState; }
    bool  isDisplayedMonoLock() const noexcept { return monoLockState; }
    juce::String getDisplayedMonoStatusText(bool compact = false) const;

    const std::array<float, displayPointCount>& getDisplayWaveform() const noexcept { return displayWaveform; }

private:
    static constexpr float silenceThreshold = 0.0012f;

    void timerCallback() override;
    void rebuildStaticLayers();

    AllyBassAudioProcessor& processor;

    std::array<float, captureSampleCount> captureScratch {};
    std::array<float, displayPointCount>  displayWaveform {};

    juce::Path cachedContoursPrimary;
    juce::Path cachedContoursSecondary;
    juce::Path cachedGridLines;
    juce::Path cachedDatumLine;
    juce::Path waveStrokePath;
    juce::Path waveFillPath;

    juce::Rectangle<float> panelBounds;
    juce::Rectangle<float> headerBounds;
    juce::Rectangle<float> scaleBounds;
    juce::Rectangle<float> plotBounds;

    float renderedWavePeak = 0.0f;
    bool  overdriveState   = false;
    bool  monoLockState    = true;
    bool  settledSilent    = true;

    uint64_t lastWriteCounter  = 0;
    int      staleCounterTicks = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NapWaveVisualizer)
};

} // namespace AllyGUI
