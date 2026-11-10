#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "DSP/NapSubOscillator.h"
#include "DSP/PolderTransient.h"
#include "DSP/StadsdeelSaturation.h"
#include "DSP/WeerwaterGlide.h"
#include "DSP/MonoLockFilter.h"
#include "DSP/Overdrive036.h"

class AllyBassAudioProcessor : public juce::AudioProcessor
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

    // APVTS
    juce::AudioProcessorValueTreeState apvts;
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    // Visualizer Audio FIFO
    static constexpr int visualizerBufferSize = 512;
    void getVisualizerData(float* destination, int numSamples);
    bool isOverdriveActive() const noexcept;
    float getCurrentSubLevel() const noexcept;

    // MIDI Keyboard State (for on-screen & QWERTY computer keyboard)
    juce::MidiKeyboardState& getKeyboardState() noexcept { return keyboardState; }

private:
    juce::MidiKeyboardState keyboardState;

    // DSP Modules
    AllyDSP::WeerwaterGlide weerwaterGlide;
    AllyDSP::NapSubOscillator napSubOsc;
    AllyDSP::PolderTransient polderTransient;
    AllyDSP::StadsdeelSaturation stadsdeelSaturation;
    AllyDSP::MonoLockFilter monoLockFilter;
    AllyDSP::Overdrive036 overdrive036;

    // Low-pass filter to tame harsh highs
    juce::dsp::ProcessorDuplicator<juce::dsp::IIR::Filter<float>, juce::dsp::IIR::Coefficients<float>> masterLowPass;

    // Click-Free Envelope Generator (Attack - Decay - Release)
    enum class EnvelopeState { Idle, Attack, Decay, Release };
    EnvelopeState envState = EnvelopeState::Idle;
    float envLevel = 0.0f;
    float attackRate = 0.0f;
    float decayRate = 0.999f;
    float releaseRate = 0.999f;
    float targetVelocity = 1.0f;

    // Visualizer Ring Buffer
    std::array<float, visualizerBufferSize> visualizerBuffer {};
    std::atomic<int> visualizerWriteIndex { 0 };
    std::atomic<float> currentSubLevel { 0.0f };

    void handleMidiEvent(const juce::MidiMessage& message, float attackMs, float punchAmount);
    void updateEnvelopeRates(float attackMs, float vervalSeconds, float releaseMs);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AllyBassAudioProcessor)
};
