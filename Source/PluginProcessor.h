#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/NapSubOscillator.h"
#include "DSP/PolderTransient.h"
#include "DSP/StadsdeelSaturation.h"
#include "DSP/WaterwijkGlide.h"
#include "DSP/MonoLockFilter.h"
#include "DSP/Overdrive036.h"
#include "Presets/PresetManager.h"

class AllyBassAudioProcessor : public juce::AudioProcessor,
                               private juce::AudioProcessorValueTreeState::Listener,
                               private juce::ValueTree::Listener
{
public:
    AllyBassAudioProcessor();
    ~AllyBassAudioProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override;

    const juce::String getName() const override;

    bool acceptsMidi() const override;
    bool producesMidi() const override;
    bool isMidiEffect() const override;
    double getTailLengthSeconds() const override;

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram(int index) override;
    const juce::String getProgramName(int index) override;
    void changeProgramName(int index, const juce::String& newName) override;

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    // Preset & State Management API (message thread)
    AllyPresets::PresetStateInfo getPresetStateInfo() const;
    void selectRelativeFactoryPreset(int delta);
    bool loadUserPresetFromFile(const juce::File& file, juce::String& outError);
    bool saveUserPresetToFile(const juce::File& file,
                              const juce::String& presetName,
                              bool allowOverwrite,
                              juce::String& outError);

    // APVTS
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Visualizer Audio FIFO & Real-Time Output Meter Telemetry (Lock-Free Audio -> GUI)
    static constexpr int visualizerBufferSize = 2048;
    static_assert((visualizerBufferSize & (visualizerBufferSize - 1)) == 0,
                  "visualizerBufferSize must be a power of two");

    struct OutputMeterMetrics
    {
        float peakLinear      = 0.0f;
        float rmsLinear       = 0.0f;
        float leftPeakLinear  = 0.0f;
        float rightPeakLinear = 0.0f;
        int   channelCount    = 2;
        bool  clipped         = false;
        uint64_t updateCounter = 0;
    };

    void getVisualizerData(float* destination, int numSamples);
    uint64_t getVisualizerWriteCounter() const noexcept;
    bool isOverdriveActive() const noexcept;
    bool isMonoLockActive() const noexcept;
    float getCurrentSubLevel() const noexcept;
    double getCurrentSampleRate() const noexcept;

    OutputMeterMetrics peekOutputMeterMetrics() const noexcept;
    OutputMeterMetrics consumeOutputMeterMetrics() noexcept;
    void resetOutputTelemetry() noexcept;

    // MIDI Keyboard State (for on-screen & QWERTY computer keyboard)
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

private:
    juce::MidiKeyboardState keyboardState;

    // DSP Modules
    AllyDSP::WaterwijkGlide waterwijkGlide;
    AllyDSP::NapSubOscillator napSubOsc;
    AllyDSP::PolderTransient polderTransient;
    AllyDSP::StadsdeelSaturation stadsdeelSaturation;
    AllyDSP::MonoLockFilter monoLockFilter;
    AllyDSP::Overdrive036 overdrive036;

    // Low-pass filter to tame harsh highs
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> masterLowPass;

    // Click-Free Envelope Generator (Attack - Sustain - Release)
    enum class EnvelopeState { Idle, Attack, Sustain, Release };
    EnvelopeState envState = EnvelopeState::Idle;
    float envLevel = 0.0f;
    float attackRate = 0.0f;
    float releaseRate = 0.999f;
    float targetVelocity = 1.0f;

    // Lock-free Visualizer Ring Buffer & Post-Output Meter Atomics
    std::array<std::atomic<float>, visualizerBufferSize> visualizerBuffer {};
    std::atomic<int> visualizerWriteIndex { 0 };
    std::atomic<uint64_t> visualizerWriteCounter { 0 };
    std::atomic<float> currentSubLevel { 0.0f };
    std::atomic<double> currentSampleRateHz { 44100.0 };

    std::atomic<float> outputLastBlockPeak { 0.0f };
    std::atomic<float> outputLastBlockRms { 0.0f };
    std::atomic<float> outputPeakLatched { 0.0f };
    std::atomic<float> outputRmsLatched { 0.0f };
    std::atomic<float> outputLeftPeak { 0.0f };
    std::atomic<float> outputRightPeak { 0.0f };
    std::atomic<int>   outputChannelCount { 2 };
    std::atomic<bool>  outputClipLatched { false };
    std::atomic<uint64_t> outputMeterCounter { 0 };

    void handleMidiEvent(const juce::MidiMessage& message, float attackMs, float punchAmount);
    void updateEnvelopeRates(float attackMs, float releaseMs);

    // APVTS & ValueTree listeners for lock-free parameter edit / preset modified tracking
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;

    std::atomic<int> currentProgramAtomic { 0 };
    std::atomic<bool> presetModifiedAtomic { false };
    std::atomic<bool> suppressParamModifiedAtomic { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllyBassAudioProcessor)
};
