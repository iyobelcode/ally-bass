#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <array>

class AllyBassAudioProcessor;

namespace AllyGUI
{

class NapWaveVisualizer : public juce::Component, public juce::Timer
{
public:
    explicit NapWaveVisualizer(AllyBassAudioProcessor& p);
    ~NapWaveVisualizer() override;

    void paint(juce::Graphics& g) override;
    void resized() override;
    void timerCallback() override;

private:
    AllyBassAudioProcessor& processor;

    static constexpr int numScopePoints = 256;
    std::array<float, numScopePoints> scopeData {};
    std::array<float, numScopePoints> displayPoints {};

    float waterPhase = 0.0f;
    float smoothRms = 0.0f;

    void drawArchitecturalGrid(juce::Graphics& g, juce::Rectangle<float> bounds);
    void drawNapWaterLine(juce::Graphics& g, juce::Rectangle<float> bounds, float napY);
    void drawWaveform(juce::Graphics& g, juce::Rectangle<float> bounds, float napY, bool isOverdrive);
    void drawTechnicalTelemetry(juce::Graphics& g, juce::Rectangle<float> bounds);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NapWaveVisualizer)
};

} // namespace AllyGUI
