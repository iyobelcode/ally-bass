#pragma once

#include <array>
#include <atomic>
#include <cstdint>
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
                               private juce::ValueTree::Listener,
                               private juce::MidiKeyboardState::Listener
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

    // MIDI Keyboard State (message/GUI thread owner; bridged lock-free to audio thread)
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }
    uint64_t getExternalMidiVersion() const noexcept { return externalMidiVersion.load(std::memory_order_acquire); }
    void syncExternalMidiToKeyboardState(uint64_t& inOutLastVersion,
                                         std::array<uint64_t, 2>& inOutAppliedExternalMask);
    void releaseGuiHeldKeysOnKeyboardState();
    uint64_t getGuiMidiQueueOverflowCount() const noexcept { return guiQueueOverflowCount.load(std::memory_order_acquire); }

    // Read-only DSP Envelope inspection accessors (D.1B/D.1C single-sample Attack verification)
    int getEnvelopeStateInt() const noexcept { return static_cast<int>(envState); }
    float getEnvelopeLevel() const noexcept { return envLevel; }
    float getCurrentVelocityGain() const noexcept { return currentVelocityGain; }

private:
    static constexpr int guiMidiQueueCapacity = 256;
    static_assert((guiMidiQueueCapacity & (guiMidiQueueCapacity - 1)) == 0,
                  "guiMidiQueueCapacity must be a power of two");

    static constexpr uint8_t kTripleSlotMask = 0x03u;
    static constexpr uint8_t kTripleDirtyBit = 0x04u;

    struct GuiMidiEvent
    {
        bool isNoteOn = false;
        uint8_t channel = 1;
        uint8_t note = 0;
        float velocity = 0.0f;
    };

    struct OutputMeterBlockSnapshot
    {
        float peakLinear      = 0.0f;
        float rmsLinear       = 0.0f;
        float leftPeakLinear  = 0.0f;
        float rightPeakLinear = 0.0f;
        int   channelCount    = 2;
        uint64_t updateCounter = 0;
    };

    juce::MidiKeyboardState keyboardState;

    // Lock-free SPSC GUI -> Audio MIDI queue + authoritative 128-note atomic reconciliation bitmask & velocity mirror
    std::array<GuiMidiEvent, guiMidiQueueCapacity> guiToAudioMidiQueue {};
    std::atomic<uint32_t> guiMidiWriteIdx { 0 };
    std::atomic<uint32_t> guiMidiReadIdx { 0 };
    std::array<std::atomic<uint64_t>, 2> guiDesiredNotesMask {};
    std::array<std::atomic<float>, 128> guiDesiredVelocities {};
    std::atomic<int> guiLastPressedNote { -1 };
    std::atomic<bool> guiMidiOverflowPending { false };
    std::atomic<uint64_t> guiQueueOverflowCount { 0 };
    static_assert(std::atomic<float>::is_always_lock_free, "guiDesiredVelocities requires lock-free float");
    static_assert(std::atomic<int>::is_always_lock_free, "guiLastPressedNote requires lock-free int");
    bool suppressGuiMidiForwarding = false;

    // Audio-thread private GUI held-note tracking + lock-free Audio -> GUI DAW MIDI mirror
    std::array<uint64_t, 2> audioGuiHeldNotesMask {};
    std::array<uint64_t, 2> audioExternalNotesMask {};
    std::array<std::atomic<uint64_t>, 2> externalMidiNotesMask {};
    std::atomic<uint64_t> externalMidiVersion { 0 };

    // Cached APVTS raw parameter pointers (avoid string-map lookups in realtime audio thread)
    const std::atomic<float>* rawDiepte = nullptr;
    const std::atomic<float>* rawPolderPunch = nullptr;
    const std::atomic<float>* rawHavenDirt = nullptr;
    const std::atomic<float>* rawWaterwijkGlide = nullptr;
    const std::atomic<float>* rawMasterVolume = nullptr;
    const std::atomic<float>* rawAttack = nullptr;
    const std::atomic<float>* rawRelease = nullptr;
    const std::atomic<float>* rawStadsdeel = nullptr;
    const std::atomic<float>* rawOverdrive036 = nullptr;
    const std::atomic<float>* rawMonoLock1300 = nullptr;

    // DSP Modules
    AllyDSP::WaterwijkGlide waterwijkGlide;
    AllyDSP::NapSubOscillator napSubOsc;
    AllyDSP::PolderTransient polderTransient;
    AllyDSP::StadsdeelSaturation stadsdeelSaturation;
    AllyDSP::MonoLockFilter monoLockFilter;
    AllyDSP::Overdrive036 overdrive036;

    // Low-pass filter to tame harsh highs
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> masterLowPass;

    // Sample-rate-invariant parameter smoothers (prevents zipper/step clicks on automation)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedDiepte;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedHavenDirt;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> smoothedMasterGain;

    // Normalized [0..1] Envelope Generator (Attack - Sustain - Release) + decoupled smoothed velocity gain
    enum class EnvelopeState { Idle, Attack, Sustain, Release };
    EnvelopeState envState = EnvelopeState::Idle;
    float envLevel = 0.0f;
    float attackRate = 0.0f;
    float releaseRate = 0.999f;
    float targetVelocity = 1.0f;
    float currentVelocityGain = 1.0f;
    float velocitySmoothCoeff = 0.01f;

    // Wait-Free 3-Slot Triple-Buffer Scope Snapshot Mailbox + Audio-Private History
    std::array<float, visualizerBufferSize> scopeHistory {};
    int scopeHistoryWritePos = 0;
    std::array<std::array<float, visualizerBufferSize>, 3> scopeTripleSlots {};
    uint8_t scopeProducerSlot = 0;
    uint8_t scopeConsumerSlot = 1;
    std::atomic<uint8_t> scopeTripleMiddle { 2 };
    std::atomic<uint64_t> visualizerWriteCounter { 0 };
    std::atomic<float> currentSubLevel { 0.0f };
    std::atomic<double> currentSampleRateHz { 44100.0 };

    // Wait-Free 3-Slot Triple-Buffer Output Meter Block + Atomic Latched Transients
    std::array<OutputMeterBlockSnapshot, 3> meterTripleSlots {};
    uint8_t meterProducerSlot = 0;
    mutable uint8_t meterConsumerSlot = 1;
    mutable std::atomic<uint8_t> meterTripleMiddle { 2 };
    uint64_t audioMeterSeq = 0;

    std::atomic<float> outputPeakLatched { 0.0f };
    std::atomic<float> outputRmsLatched { 0.0f };
    std::atomic<bool>  outputClipLatched { false };
    std::atomic<uint64_t> outputMeterCounter { 0 };

    void handleMidiEvent(const juce::MidiMessage& message, float attackMs, float punchAmount);
    void reconcileGuiOverflowNotes(float attackMs, float punchAmount);
    void updateEnvelopeRates(float attackMs, float releaseMs);
    void publishScopeHistoryToTripleBuffer() noexcept;
    OutputMeterMetrics pullLatestMeterBlock() const noexcept;

    // MidiKeyboardState::Listener (serialized off-audio-thread by keyboardState's internal lock)
    void handleNoteOn(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
    void handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity) override;
    void pushGuiMidiEvent(const GuiMidiEvent& ev) noexcept;

    // APVTS & ValueTree listeners for lock-free parameter edit / preset modified tracking
    void parameterChanged(const juce::String& parameterID, float newValue) override;
    void valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                  const juce::Identifier& property) override;

    std::atomic<int> currentProgramAtomic { 0 };
    std::atomic<bool> presetModifiedAtomic { false };
    std::atomic<bool> suppressParamModifiedAtomic { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllyBassAudioProcessor)
};
