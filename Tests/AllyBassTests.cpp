#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PresetManager.h"
#include "DSP/WaterwijkGlide.h"
#include "DSP/PolderTransient.h"
#include "GUI/DepthRingKnob.h"
#include "GUI/EngineeredDepthComboBox.h"
#include "GUI/EngineeredDepthIcons.h"
#include "GUI/EngineeredDepthTheme.h"
#include "GUI/EngineeredDepthToggle.h"
#include "GUI/BrutalistToggle.h"
#include "GUI/HeavyKnob.h"
#include "GUI/PrimaryKnobsPanel.h"
#include "GUI/SecondaryDspPanel.h"
#include "GUI/HeaderPanel.h"
#include "GUI/SettingsOverlay.h"
#include "GUI/SavePresetOverlay.h"
#include "GUI/NapWaveVisualizer.h"
#include "GUI/OutputLevelMeter.h"
#include "GUI/KeyboardPanel.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <ctime>
#include <thread>
#include <vector>

namespace
{

// Controlled via CLI `--mode` (default `all` preserves every assertion & timing budget).
bool gEnforceWallClockRenderBudgets = true;

juce::Slider* findSliderByName(juce::Component& root, const juce::String& name)
{
    if (auto* slider = dynamic_cast<juce::Slider*>(&root))
    {
        if (slider->getName() == name)
            return slider;
    }

    for (auto* child : root.getChildren())
    {
        if (auto* found = findSliderByName(*child, name))
            return found;
    }

    return nullptr;
}

float getParamPlainValue(const juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId)
{
    if (auto* raw = apvts.getRawParameterValue(paramId))
        return raw->load();
    return 0.0f;
}

void setParamPlainValue(juce::AudioProcessorValueTreeState& apvts, const juce::String& paramId, float plainValue)
{
    if (auto* param = apvts.getParameter(paramId))
        param->setValueNotifyingHost(param->convertTo0to1(plainValue));
}

float bufferPeak(const juce::AudioBuffer<float>& buffer)
{
    float peak = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
    {
        const auto* data = buffer.getReadPointer(ch);
        for (int i = 0; i < buffer.getNumSamples(); ++i)
        {
            if (!std::isfinite(data[i]))
                return -1.0f;
            peak = juce::jmax(peak, std::abs(data[i]));
        }
    }
    return peak;
}

struct RenderedMetrics
{
    std::vector<float> mono;
    float peak = 0.0f;
    float maxSampleDelta = 0.0f;
    int maxDeltaIndex = -1;
    bool allFinite = true;
};

RenderedMetrics renderProcessorNote(double sampleRate, int blockSize, int totalSamples,
                                    float punchAmount, int presetIndex = -1,
                                    bool overdriveOverride = false, bool applyOverdriveOverride = false)
{
    AllyBassAudioProcessor proc;
    proc.setRateAndBufferSizeDetails(sampleRate, blockSize);
    proc.prepareToPlay(sampleRate, blockSize);

    if (presetIndex >= 0)
        AllyPresets::applyPreset(proc.apvts, presetIndex);

    setParamPlainValue(proc.apvts, "polderPunch", punchAmount);
    if (applyOverdriveOverride)
        setParamPlainValue(proc.apvts, "overdrive036", overdriveOverride ? 1.0f : 0.0f);

    RenderedMetrics metrics;
    metrics.mono.reserve(static_cast<size_t>(totalSamples));

    juce::AudioBuffer<float> buffer(2, blockSize);
    juce::MidiBuffer midi;
    float prevSample = 0.0f;
    int rendered = 0;

    while (rendered < totalSamples)
    {
        midi.clear();
        if (rendered == 0)
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);

        buffer.clear();
        proc.processBlock(buffer, midi);

        const int count = juce::jmin(blockSize, totalSamples - rendered);
        const auto* left = buffer.getReadPointer(0);
        for (int i = 0; i < count; ++i)
        {
            const float s = left[i];
            if (!std::isfinite(s))
                metrics.allFinite = false;
            metrics.mono.push_back(s);
            metrics.peak = juce::jmax(metrics.peak, std::abs(s));
            const float delta = std::abs(s - prevSample);
            if (delta > metrics.maxSampleDelta)
            {
                metrics.maxSampleDelta = delta;
                metrics.maxDeltaIndex = rendered + i;
            }
            prevSample = s;
        }
        rendered += count;
    }

    return metrics;
}

float windowHighPassRms(const std::vector<float>& samples, double sampleRate, float cutoffHz,
                        int startSample, int endSample, int stages = 3)
{
    const int clampedStart = juce::jmax(0, startSample);
    const int clampedEnd = juce::jmin(static_cast<int>(samples.size()), endSample);
    if (clampedEnd <= clampedStart)
        return 0.0f;

    const int numStages = juce::jlimit(1, 6, stages);
    const float rc = 1.0f / (juce::MathConstants<float>::twoPi * cutoffHz);
    const float dt = 1.0f / static_cast<float>(sampleRate);
    const float alpha = rc / (rc + dt);

    float prevIn[6]{};
    float prevOut[6]{};
    double sumSq = 0.0;

    for (int i = 0; i < clampedEnd; ++i)
    {
        float sig = samples[static_cast<size_t>(i)];
        for (int s = 0; s < numStages; ++s)
        {
            const float y = alpha * (prevOut[s] + sig - prevIn[s]);
            prevIn[s] = sig;
            prevOut[s] = y;
            sig = y;
        }
        if (i >= clampedStart)
            sumSq += static_cast<double>(sig) * static_cast<double>(sig);
    }

    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(clampedEnd - clampedStart)));
}

float windowDiffRms(const std::vector<float>& a, const std::vector<float>& b, int startSample, int endSample)
{
    const int clampedStart = juce::jmax(0, startSample);
    const int clampedEnd = juce::jmin(static_cast<int>(juce::jmin(a.size(), b.size())), endSample);
    if (clampedEnd <= clampedStart)
        return 0.0f;

    double sumSq = 0.0;
    for (int i = clampedStart; i < clampedEnd; ++i)
    {
        const double d = static_cast<double>(a[static_cast<size_t>(i)]) - static_cast<double>(b[static_cast<size_t>(i)]);
        sumSq += d * d;
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(clampedEnd - clampedStart)));
}

class ParameterAndStateTests final : public juce::UnitTest
{
public:
    ParameterAndStateTests() : juce::UnitTest("ALLY1300 Parameters & State", "AllyBass") {}

    void runTest() override
    {
        beginTest("APVTS exposes waterwijkGlide without legacy weerwaterGlide alias");
        {
            AllyBassAudioProcessor proc;

            expect(proc.apvts.getParameter("weerwaterGlide") == nullptr,
                   "Legacy parameter ID weerwaterGlide must not exist");
            expect(proc.apvts.getRawParameterValue("weerwaterGlide") == nullptr,
                   "Legacy raw parameter weerwaterGlide must not exist");

            auto* glideParam = proc.apvts.getParameter("waterwijkGlide");
            expect(glideParam != nullptr, "Parameter waterwijkGlide must exist");

            if (glideParam != nullptr)
            {
                expectEquals(glideParam->getName(64), juce::String("WATERWIJK GLIDE"));
                expectEquals(glideParam->getLabel(), juce::String("ms"));

                const auto& range = proc.apvts.getParameterRange("waterwijkGlide");
                expectWithinAbsoluteError(range.start, 0.0f, 1e-5f);
                expectWithinAbsoluteError(range.end, 500.0f, 1e-5f);
                expectWithinAbsoluteError(range.interval, 0.5f, 1e-5f);
                expectWithinAbsoluteError(range.skew, 0.45f, 1e-5f);

                const float defaultPlain = glideParam->convertFrom0to1(glideParam->getDefaultValue());
                expectWithinAbsoluteError(defaultPlain, 40.0f, 1e-4f);
            }

            const juce::StringArray expectedIds {
                "diepte", "polderPunch", "havenDirt", "waterwijkGlide",
                "masterVolume", "attack", "release", "stadsdeel",
                "overdrive036", "monoLock1300"
            };
            for (const auto& id : expectedIds)
                expect(proc.apvts.getParameter(id) != nullptr, "Missing APVTS parameter: " + id);

            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "diepte"), 0.50f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "polderPunch"), 0.00f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "havenDirt"), 0.15f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "waterwijkGlide"), 40.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "masterVolume"), 0.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "attack"), 8.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "release"), 250.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "stadsdeel"), 1.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "overdrive036"), 0.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "monoLock1300"), 1.0f, 1e-4f);
        }

        beginTest("All 14 factory presets apply exact values including waterwijkGlide");
        {
            AllyBassAudioProcessor proc;
            const auto& presets = AllyPresets::getFactoryPresets();

            expectEquals(static_cast<int>(presets.size()), 14);
            expectEquals(proc.getNumPrograms(), 14);

            for (int i = 0; i < static_cast<int>(presets.size()); ++i)
            {
                const auto& preset = presets[static_cast<size_t>(i)];
                expectEquals(proc.getProgramName(i), preset.name);

                if (i % 2 == 0)
                    AllyPresets::applyPreset(proc.apvts, i);
                else
                    proc.setCurrentProgram(i);

                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "diepte"), preset.diepte, 0.011f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "polderPunch"), preset.polderPunch, 0.011f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "havenDirt"), preset.havenDirt, 0.011f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "waterwijkGlide"), preset.waterwijkGlide, 0.51f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "release"), preset.releaseMs, 1.01f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "stadsdeel"),
                                          static_cast<float>(preset.stadsdeelIndex), 1e-4f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "overdrive036"),
                                          preset.overdrive036 ? 1.0f : 0.0f, 1e-4f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "monoLock1300"),
                                          preset.monoLock1300 ? 1.0f : 0.0f, 1e-4f);

                // All factory presets must stay within APVTS attack range [1.0, 300.0] ms (including 05_Muziekwijk_Pluck)
                expect(preset.attackMs >= 1.0f && preset.attackMs <= 300.0f,
                       "All factory presets must specify attackMs within [1.0, 300.0] ms");
                auto* attackParam = proc.apvts.getParameter("attack");
                expect(attackParam != nullptr, "Attack parameter must exist");
                if (attackParam != nullptr)
                {
                    const float expectedAttack = attackParam->convertFrom0to1(attackParam->convertTo0to1(preset.attackMs));
                    expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "attack"), expectedAttack, 0.11f);
                }
            }
        }

        beginTest("State save and restore preserves waterwijkGlide and all parameters");
        {
            juce::MemoryBlock savedState;
            {
                AllyBassAudioProcessor procA;
                setParamPlainValue(procA.apvts, "diepte", 0.88f);
                setParamPlainValue(procA.apvts, "polderPunch", 0.35f);
                setParamPlainValue(procA.apvts, "havenDirt", 0.67f);
                setParamPlainValue(procA.apvts, "waterwijkGlide", 185.5f);
                setParamPlainValue(procA.apvts, "masterVolume", -3.5f);
                setParamPlainValue(procA.apvts, "attack", 24.5f);
                setParamPlainValue(procA.apvts, "release", 900.0f);
                setParamPlainValue(procA.apvts, "stadsdeel", 3.0f);
                setParamPlainValue(procA.apvts, "overdrive036", 1.0f);
                setParamPlainValue(procA.apvts, "monoLock1300", 0.0f);

                procA.getStateInformation(savedState);
            }

            expect(savedState.getSize() > 0, "Saved state block must not be empty");

            auto xml = juce::AudioProcessor::getXmlFromBinary(savedState.getData(),
                                                             static_cast<int>(savedState.getSize()));
            expect(xml != nullptr, "Saved state must deserialize as XML");
            if (xml != nullptr)
            {
                const auto xmlStr = xml->toString();
                expect(xmlStr.contains("waterwijkGlide"), "Serialized state must contain waterwijkGlide");
                expect(!xmlStr.contains("weerwaterGlide"), "Serialized state must not contain weerwaterGlide");
            }

            AllyBassAudioProcessor procB;
            procB.setStateInformation(savedState.getData(), static_cast<int>(savedState.getSize()));

            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "diepte"), 0.88f, 0.011f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "polderPunch"), 0.35f, 0.011f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "havenDirt"), 0.67f, 0.011f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "waterwijkGlide"), 185.5f, 0.51f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "masterVolume"), -3.5f, 0.11f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "attack"), 24.5f, 0.11f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "release"), 900.0f, 1.01f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "stadsdeel"), 3.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "overdrive036"), 1.0f, 1e-4f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "monoLock1300"), 0.0f, 1e-4f);
        }

        beginTest("Editor slider binding and host automation work for waterwijkGlide");
        {
            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
            expect(editor != nullptr, "Editor must be created");

            if (editor != nullptr)
            {
                expectEquals(editor->getWidth(), 780);
                expectEquals(editor->getHeight(), 580);

                auto* glideSlider = findSliderByName(*editor, "waterwijkGlide");
                expect(glideSlider != nullptr, "Knob slider named waterwijkGlide must exist");
                expect(findSliderByName(*editor, "weerwaterGlide") == nullptr,
                       "Old slider name weerwaterGlide must not exist");

                if (glideSlider != nullptr)
                {
                    expectWithinAbsoluteError(static_cast<float>(glideSlider->getValue()), 40.0f, 0.51f);

                    setParamPlainValue(proc.apvts, "waterwijkGlide", 175.0f);
                    expectWithinAbsoluteError(static_cast<float>(glideSlider->getValue()), 175.0f, 0.51f);

                    glideSlider->setValue(310.0, juce::sendNotificationSync);
                    expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "waterwijkGlide"), 310.0f, 0.51f);
                }
            }
        }
    }
};

class MidiGlideAndAudioTests final : public juce::UnitTest
{
public:
    MidiGlideAndAudioTests() : juce::UnitTest("ALLY1300 WaterwijkGlide & Audio", "AllyBass") {}

    void runTest() override
    {
        beginTest("WaterwijkGlide monophonic legato stack, velocity scaling, and zero-glide");
        {
            AllyDSP::WaterwijkGlide glide;
            glide.prepare(48000.0);

            const float freqC2 = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(36));
            const float freqC3 = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(48));

            glide.noteOn(36, 0.8f, 100.0f);
            expect(glide.getIsNoteActive(), "Note 36 should activate voice");
            expectWithinAbsoluteError(glide.processNextFrequency(), freqC2, 0.05f);

            // Legato slide up to C3
            glide.noteOn(48, 1.0f, 100.0f);
            const float firstLegatoSample = glide.processNextFrequency();
            expect(firstLegatoSample > freqC2 && firstLegatoSample < freqC3,
                   "Legato glide should move gradually instead of jumping immediately");

            for (int i = 0; i < 400; ++i)
                glide.processNextFrequency();
            const float fastVelFreq = glide.getCurrentFrequency();
            expect(fastVelFreq > freqC2 + 10.0f && fastVelFreq < freqC3,
                   "High velocity legato glide should advance smoothly toward C3");

            // Compare velocity scaling (lower velocity = slower slide)
            AllyDSP::WaterwijkGlide slowGlide;
            slowGlide.prepare(48000.0);
            slowGlide.noteOn(36, 0.2f, 100.0f);
            slowGlide.processNextFrequency();
            slowGlide.noteOn(48, 0.2f, 100.0f);
            for (int i = 0; i < 401; ++i)
                slowGlide.processNextFrequency();
            expect(fastVelFreq > slowGlide.getCurrentFrequency(),
                   "Higher velocity must result in faster Waterwijk glide curve");

            // Release upper note -> legato return to held C2
            glide.noteOff(48, 100.0f);
            expect(glide.getIsNoteActive(), "Voice must stay active while C2 is still held");
            const float beforeReturn = glide.getCurrentFrequency();
            for (int i = 0; i < 500; ++i)
                glide.processNextFrequency();
            expect(glide.getCurrentFrequency() < beforeReturn && glide.getCurrentFrequency() > freqC2,
                   "Releasing upper legato note must glide back down toward C2");

            // Final release
            glide.noteOff(36, 100.0f);
            expect(!glide.getIsNoteActive(), "Releasing all notes must deactivate voice");

            // Zero-glide snap
            glide.noteOn(36, 1.0f, 0.0f);
            glide.noteOn(48, 1.0f, 0.0f);
            expectWithinAbsoluteError(glide.processNextFrequency(), freqC3, 0.05f);
        }

        beginTest("AudioProcessor produces audio on MIDI noteOn, glides, and releases cleanly");
        {
            AllyBassAudioProcessor proc;
            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 256;
            proc.setRateAndBufferSizeDetails(sampleRate, blockSize);
            proc.prepareToPlay(sampleRate, blockSize);

            setParamPlainValue(proc.apvts, "attack", 8.0f);
            setParamPlainValue(proc.apvts, "release", 25.0f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 80.0f);
            setParamPlainValue(proc.apvts, "masterVolume", 0.0f);

            juce::AudioBuffer<float> audio(2, blockSize);
            juce::MidiBuffer midi;

            // 1. Silent before any note
            audio.clear();
            proc.processBlock(audio, midi);
            expectWithinAbsoluteError(bufferPeak(audio), 0.0f, 1e-6f);

            // 2. Note-on C2 -> non-zero finite audio + visualizer update
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
            float maxNotePeak = 0.0f;
            for (int b = 0; b < 6; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                midi.clear();
                const float peak = bufferPeak(audio);
                expect(peak >= 0.0f, "Audio output must be finite");
                maxNotePeak = juce::jmax(maxNotePeak, peak);
            }
            expect(maxNotePeak > 0.05f, "Note-on should produce audible sub output");
            expect(proc.getCurrentSubLevel() > 0.01f, "Sub RMS level should update on note-on");

            std::vector<float> scopeSamples(256, 0.0f);
            proc.getVisualizerData(scopeSamples.data(), 256);
            float scopePeak = 0.0f;
            for (float s : scopeSamples)
                scopePeak = juce::jmax(scopePeak, std::abs(s));
            expect(scopePeak > 0.01f, "Visualizer FIFO should receive audio samples");

            // 3. Legato note-on C3 -> finite continuous output
            midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.95f), 0);
            for (int b = 0; b < 4; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                midi.clear();
                expect(bufferPeak(audio) > 0.05f, "Legato transition must remain finite and audible");
            }

            // 4. Note-off both notes -> release to silence (no stuck notes)
            midi.addEvent(juce::MidiMessage::noteOff(1, 48), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, 36), 16);
            float tailPeak = 1.0f;
            for (int b = 0; b < 30; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                midi.clear();
                tailPeak = bufferPeak(audio);
                expect(tailPeak >= 0.0f, "Release audio must remain finite");
            }
            expect(tailPeak < 1e-3f, "Audio must decay to silence after noteOff");

            // 5. Same-sample noteOn + noteOff must not stick
            midi.addEvent(juce::MidiMessage::noteOn(1, 40, 0.9f), 0);
            midi.addEvent(juce::MidiMessage::noteOff(1, 40), 0);
            for (int b = 0; b < 30; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                midi.clear();
                tailPeak = bufferPeak(audio);
                expect(tailPeak >= 0.0f, "Same-sample noteOn/noteOff audio must remain finite");
            }
            expect(tailPeak < 1e-3f, "Same-sample noteOn/noteOff must not leave a stuck note");

            // 6. AllNotesOff clears active voice immediately
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
            audio.clear();
            proc.processBlock(audio, midi);
            midi.clear();
            expect(bufferPeak(audio) > 0.0f, "Note should sound before allNotesOff");

            midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            audio.clear();
            proc.processBlock(audio, midi);
            midi.clear();
            expectWithinAbsoluteError(proc.getCurrentSubLevel(), 0.0f, 1e-6f);

            // Allow downstream 90 Hz MonoLock IIR filter state to settle to silence
            float settledPeak = 1.0f;
            for (int b = 0; b < 30; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                settledPeak = bufferPeak(audio);
                expect(settledPeak >= 0.0f, "Post-allNotesOff audio must remain finite");
            }
            expect(settledPeak < 1e-4f, "Audio must settle to silence after allNotesOff");
        }
    }
};

class PolderPunchTests final : public juce::UnitTest
{
public:
    PolderPunchTests() : juce::UnitTest("ALLY1300 Polder Punch", "AllyBass") {}

    void runTest() override
    {
        beginTest("PolderTransient DSP produces exact zero at 0% and click-free retrigger");
        {
            AllyDSP::PolderTransient transient;
            transient.prepare(48000.0);

            transient.trigger(0.95f, 0.0f);
            expect(!transient.getIsActive(), "0% punch must not activate transient");
            for (int i = 0; i < 256; ++i)
            {
                expectWithinAbsoluteError(transient.processSample(0.0f), 0.0f, 1e-7f);
                expectWithinAbsoluteError(transient.getNormalizedEnvelope(), 0.0f, 1e-7f);
            }

            // Trigger at 100%, interrupt mid-knock, verify step discontinuity is near zero
            transient.trigger(1.0f, 1.0f);
            expect(transient.getIsActive(), "100% punch must activate transient");

            float prevOut = 0.0f;
            float maxDelta = 0.0f;
            float maxPeak = 0.0f;
            for (int i = 0; i < 200; ++i)
            {
                const float s = transient.processSample(1.0f);
                maxDelta = juce::jmax(maxDelta, std::abs(s - prevOut));
                maxPeak = juce::jmax(maxPeak, std::abs(s));
                prevOut = s;
            }
            expect(maxPeak > 0.25f && maxPeak <= 0.55f,
                   "Standalone PolderTransient peak must be audible and bounded");
            expect(maxDelta < 0.03f,
                   "Standalone PolderTransient onset must have smooth derivative without step click");

            // Rapid retrigger while still sounding
            transient.trigger(1.0f, 1.0f);
            const float retriggerFirst = transient.processSample(1.0f);
            expect(std::abs(retriggerFirst - prevOut) < 0.03f,
                   "Retrigger sample must crossfade seamlessly without step discontinuity");
        }

        beginTest("Polder Punch 0%, 25%, 50%, 75%, 100% produce monotonic click-free audio response");
        {
            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 256;
            constexpr int totalSamples = 14400; // 300 ms
            const int onset15End = static_cast<int>(0.015 * sampleRate);
            const int onset20End = static_cast<int>(0.020 * sampleRate);
            const int onset40End = static_cast<int>(0.040 * sampleRate);
            const int sustainStart = static_cast<int>(0.200 * sampleRate);

            const float levels[] = { 0.0f, 0.25f, 0.50f, 0.75f, 1.00f };
            std::vector<RenderedMetrics> renders;
            for (float lvl : levels)
                renders.push_back(renderProcessorNote(sampleRate, blockSize, totalSamples, lvl));

            std::vector<float> diffOnsetRms;
            std::vector<float> hp160OnsetRms;

            for (size_t i = 0; i < renders.size(); ++i)
            {
                const auto& r = renders[i];
                expect(r.allFinite, "Audio must be finite for all Polder Punch levels");
                expect(r.peak <= 1.15f, "Peak output must remain controlled without extreme clipping spikes");
                expect(r.maxSampleDelta < 0.07f,
                       "Max sample-to-sample jump must stay click-free");

                diffOnsetRms.push_back(windowDiffRms(r.mono, renders[0].mono, 0, onset40End));
                // 3rd-order 160 Hz high-pass suppresses the 65.4 Hz sub fundamental (~-25 dB)
                // to measure direct non-differential transient attack punch energy.
                hp160OnsetRms.push_back(windowHighPassRms(r.mono, sampleRate, 160.0f, 0, onset20End, 3));

                const float sustainDiff = windowDiffRms(r.mono, renders[0].mono, sustainStart, totalSamples);
                expect(sustainDiff < 2e-4f,
                       "Polder Punch must not alter steady-state sustain after transient decay");

                logMessage("Punch " + juce::String(static_cast<int>(levels[i] * 100.0f)) + "% -> hp160Rms="
                           + juce::String(hp160OnsetRms.back(), 4)
                           + ", diffOnsetRms=" + juce::String(diffOnsetRms.back(), 4)
                           + ", peak=" + juce::String(r.peak, 4)
                           + ", maxDelta=" + juce::String(r.maxSampleDelta, 5)
                           + " @" + juce::String(r.maxDeltaIndex)
                           + ", sustainDiff=" + juce::String(sustainDiff, 6));
            }

            expectWithinAbsoluteError(diffOnsetRms[0], 0.0f, 1e-7f);
            for (size_t i = 1; i < renders.size(); ++i)
            {
                expect(diffOnsetRms[i] > diffOnsetRms[i - 1] + 0.01f,
                       "Transient difference vs 0% must increase monotonically across 0/25/50/75/100%");
                expect(hp160OnsetRms[i] > hp160OnsetRms[i - 1] + 0.003f,
                       "Attack >160 Hz punch-band RMS must increase monotonically across 0/25/50/75/100%");
            }

            // Also verify clean sub path (preset 07 Noorderplassen_Clean: havenDirt = 0.0, Poort)
            std::vector<RenderedMetrics> cleanRenders;
            for (float lvl : levels)
                cleanRenders.push_back(renderProcessorNote(sampleRate, blockSize, totalSamples, lvl, 6));

            std::vector<float> cleanHpRms;
            std::vector<float> cleanDiff15;
            for (size_t i = 0; i < cleanRenders.size(); ++i)
            {
                const auto& cr = cleanRenders[i];
                expect(cr.peak <= 1.15f && cr.maxSampleDelta < 0.05f,
                       "Clean sub path with Polder Punch must remain bounded and click-free");
                cleanHpRms.push_back(windowHighPassRms(cr.mono, sampleRate, 160.0f, 0, onset20End, 3));
                cleanDiff15.push_back(windowDiffRms(cr.mono, cleanRenders[0].mono, 0, onset15End));
                logMessage("Clean Punch " + juce::String(static_cast<int>(levels[i] * 100.0f)) + "% -> hp160Rms="
                           + juce::String(cleanHpRms.back(), 4)
                           + ", diff15=" + juce::String(cleanDiff15.back(), 4)
                           + ", peak=" + juce::String(cr.peak, 4));
            }
            for (size_t i = 1; i < cleanRenders.size(); ++i)
            {
                expect(cleanHpRms[i] > cleanHpRms[i - 1] + 0.003f,
                       "Clean path >160 Hz punch energy must increase monotonically across 0/25/50/75/100%");
                expect(cleanDiff15[i] > cleanDiff15[i - 1] + 0.01f,
                       "Clean path onset difference must increase monotonically across 0/25/50/75/100%");
            }

            // Also verify measurable effect inside preset 04 (04_42_BASS_DaBaby_Punch, presetIndex = 3)
            const auto preset04WithPunch = renderProcessorNote(sampleRate, blockSize, totalSamples, 0.42f, 3);
            const auto preset04NoPunch   = renderProcessorNote(sampleRate, blockSize, totalSamples, 0.00f, 3);
            expect(preset04WithPunch.allFinite && preset04WithPunch.maxSampleDelta < 0.12f,
                   "Preset 04 with Polder Punch and 036 Overdrive must remain finite and click-free");
            const float preset04Diff = windowDiffRms(preset04WithPunch.mono, preset04NoPunch.mono, 0, onset40End);
            expect(preset04Diff > 0.02f,
                   "Preset 04 Polder Punch (42%) must measurably shape the attack transient");
        }

        beginTest("Polder Punch stays click-free across rapid retrigger, legato, note-off, and all-notes-off");
        {
            constexpr double sampleRate = 48000.0;
            constexpr int blockSize = 128;

            // 1. Rapid detached retrigger sequence every ~15 ms with Polder Punch = 100%
            {
                AllyBassAudioProcessor proc;
                proc.setRateAndBufferSizeDetails(sampleRate, blockSize);
                proc.prepareToPlay(sampleRate, blockSize);
                setParamPlainValue(proc.apvts, "polderPunch", 1.0f);
                setParamPlainValue(proc.apvts, "attack", 8.0f);
                setParamPlainValue(proc.apvts, "release", 40.0f);

                juce::AudioBuffer<float> audio(2, blockSize);
                juce::MidiBuffer midi;
                float prevSample = 0.0f;
                float maxDelta = 0.0f;
                float maxPeak = 0.0f;

                const int notes[] = { 36, 38, 36, 40, 36, 43 };
                for (int step = 0; step < 6; ++step)
                {
                    // Note-on for 4 blocks (~10.7 ms)
                    for (int b = 0; b < 4; ++b)
                    {
                        midi.clear();
                        if (b == 0)
                            midi.addEvent(juce::MidiMessage::noteOn(1, notes[step], 0.9f), 0);
                        audio.clear();
                        proc.processBlock(audio, midi);
                        const auto* ch0 = audio.getReadPointer(0);
                        for (int s = 0; s < blockSize; ++s)
                        {
                            expect(std::isfinite(ch0[s]), "Rapid retrigger audio must stay finite");
                            maxPeak = juce::jmax(maxPeak, std::abs(ch0[s]));
                            maxDelta = juce::jmax(maxDelta, std::abs(ch0[s] - prevSample));
                            prevSample = ch0[s];
                        }
                    }

                    // Note-off for 2 blocks (~5.3 ms) before next retrigger
                    for (int b = 0; b < 2; ++b)
                    {
                        midi.clear();
                        if (b == 0)
                            midi.addEvent(juce::MidiMessage::noteOff(1, notes[step]), 0);
                        audio.clear();
                        proc.processBlock(audio, midi);
                        const auto* ch0 = audio.getReadPointer(0);
                        for (int s = 0; s < blockSize; ++s)
                        {
                            expect(std::isfinite(ch0[s]), "Rapid release audio must stay finite");
                            maxPeak = juce::jmax(maxPeak, std::abs(ch0[s]));
                            maxDelta = juce::jmax(maxDelta, std::abs(ch0[s] - prevSample));
                            prevSample = ch0[s];
                        }
                    }
                }

                expect(maxPeak <= 1.15f, "Rapid retrigger must not produce extreme volume spikes");
                expect(maxDelta < 0.08f, "Rapid retrigger sequence must stay free of step clicks");
            }

            // 2. Legato slide after transient decay must not re-trigger Polder Punch
            {
                auto runLegato = [&](float punchAmount)
                {
                    AllyBassAudioProcessor proc;
                    proc.setRateAndBufferSizeDetails(sampleRate, blockSize);
                    proc.prepareToPlay(sampleRate, blockSize);
                    setParamPlainValue(proc.apvts, "polderPunch", punchAmount);
                    setParamPlainValue(proc.apvts, "waterwijkGlide", 80.0f);

                    std::vector<float> samples;
                    juce::AudioBuffer<float> audio(2, blockSize);
                    juce::MidiBuffer midi;

                    for (int b = 0; b < 112; ++b) // ~300 ms
                    {
                        midi.clear();
                        if (b == 0)
                            midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
                        if (b == 60) // ~160 ms: held legato transition to C3
                            midi.addEvent(juce::MidiMessage::noteOn(1, 48, 0.9f), 0);

                        audio.clear();
                        proc.processBlock(audio, midi);
                        const auto* ch0 = audio.getReadPointer(0);
                        samples.insert(samples.end(), ch0, ch0 + blockSize);
                    }
                    return samples;
                };

                const auto legatoPunch0 = runLegato(0.0f);
                const auto legatoPunch100 = runLegato(1.0f);
                const float legatoWindowDiff = windowDiffRms(legatoPunch100, legatoPunch0, 60 * blockSize, 112 * blockSize);
                expect(legatoWindowDiff < 1e-4f,
                       "Legato slide must not retrigger Polder Punch while previous note is held");
            }

            // 3. Note-off and all-notes-off mid-transient settle cleanly to silence
            {
                AllyBassAudioProcessor proc;
                proc.setRateAndBufferSizeDetails(sampleRate, blockSize);
                proc.prepareToPlay(sampleRate, blockSize);
                setParamPlainValue(proc.apvts, "polderPunch", 1.0f);
                setParamPlainValue(proc.apvts, "release", 20.0f);

                juce::AudioBuffer<float> audio(2, blockSize);
                juce::MidiBuffer midi;

                midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.95f), 0);
                audio.clear();
                proc.processBlock(audio, midi);

                midi.clear();
                midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                audio.clear();
                proc.processBlock(audio, midi);
                expectWithinAbsoluteError(proc.getCurrentSubLevel(), 0.0f, 1e-6f);

                float settledPeak = 1.0f;
                midi.clear();
                for (int b = 0; b < 40; ++b)
                {
                    audio.clear();
                    proc.processBlock(audio, midi);
                    settledPeak = bufferPeak(audio);
                    expect(settledPeak >= 0.0f, "Post-allNotesOff output must remain finite");
                }
                expect(settledPeak < 1e-4f, "Audio must settle to silence after allNotesOff mid-punch");
            }
        }

        beginTest("Polder Punch behaves consistently across 44.1 kHz, 48 kHz, and 96 kHz");
        {
            const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
            std::vector<float> diffRms50;
            std::vector<float> diffRms100;

            for (double sr : sampleRates)
            {
                const int totalSamples = static_cast<int>(0.250 * sr);
                const int onset40End = static_cast<int>(0.040 * sr);

                const auto r0   = renderProcessorNote(sr, 256, totalSamples, 0.00f);
                const auto r50  = renderProcessorNote(sr, 256, totalSamples, 0.50f);
                const auto r100 = renderProcessorNote(sr, 256, totalSamples, 1.00f);

                expect(r0.allFinite && r50.allFinite && r100.allFinite,
                       "Audio must stay finite across all sample rates");
                expect(r100.peak <= 1.15f && r50.peak <= 1.15f,
                       "Peak output must remain bounded across all sample rates");
                expect(r100.maxSampleDelta < 0.08f,
                       "Sample-to-sample transitions must remain click-free across all sample rates");

                const float d50  = windowDiffRms(r50.mono, r0.mono, 0, onset40End);
                const float d100 = windowDiffRms(r100.mono, r0.mono, 0, onset40End);
                expect(d50 > 0.04f && d100 > d50 + 0.03f,
                       "50% and 100% punch must scale cleanly at every sample rate");

                diffRms50.push_back(d50);
                diffRms100.push_back(d100);

                logMessage("SR " + juce::String(sr, 0) + " Hz -> diff50=" + juce::String(d50, 4)
                           + ", diff100=" + juce::String(d100, 4)
                           + ", peak100=" + juce::String(r100.peak, 4)
                           + ", maxDelta100=" + juce::String(r100.maxSampleDelta, 5));
            }

            // Compare 44.1 kHz and 96 kHz against 48 kHz baseline (within 15% relative tolerance)
            for (size_t i = 0; i < 3; ++i)
            {
                expect(std::abs(diffRms50[i] - diffRms50[1]) / diffRms50[1] < 0.15f,
                       "50% transient RMS must be consistent across 44.1 / 48 / 96 kHz");
                expect(std::abs(diffRms100[i] - diffRms100[1]) / diffRms100[1] < 0.15f,
                       "100% transient RMS must be consistent across 44.1 / 48 / 96 kHz");
            }
        }
    }
};

class MonoLockDspTests final : public juce::UnitTest
{
public:
    MonoLockDspTests() : juce::UnitTest("ALLY1300 Mono Lock DSP (D.1A)", "AllyBass") {}

    void runTest() override
    {
        constexpr double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
        constexpr int blockSizes[] = { 32, 64, 256, 1024 };

        auto processChunked = [](AllyDSP::MonoLockFilter& filter, juce::AudioBuffer<float>& buffer,
                                 bool enabled, int blockSize)
        {
            const int total = buffer.getNumSamples();
            juce::AudioBuffer<float> chunk(2, blockSize);
            for (int offset = 0; offset < total; offset += blockSize)
            {
                const int n = juce::jmin(blockSize, total - offset);
                chunk.setSize(2, n, false, false, true);
                for (int ch = 0; ch < 2; ++ch)
                    chunk.copyFrom(ch, 0, buffer, ch, offset, n);

                filter.process(chunk, enabled);

                for (int ch = 0; ch < 2; ++ch)
                    buffer.copyFrom(ch, offset, chunk, ch, 0, n);
            }
        };

        beginTest("Mono L=R frequency sweep 20-200 Hz, phase, transient & M+R reconstruction across SR/BS");
        {
            constexpr double sweepFreqs[] = {
                20.0, 30.0, 41.2, 50.0, 60.0, 65.0, 70.0, 80.0, 90.0, 100.0, 120.0, 150.0, 200.0
            };

            for (double sr : sampleRates)
            {
                for (int bs : blockSizes)
                {
                    AllyDSP::MonoLockFilter filter;
                    filter.prepare({ sr, static_cast<juce::uint32>(bs), 2 });

                    for (double freq : sweepFreqs)
                    {
                        const int totalSamples = static_cast<int>(sr * 0.25);
                        const int settleStart = static_cast<int>(sr * 0.10);
                        juce::AudioBuffer<float> ref(2, totalSamples);
                        for (int i = 0; i < totalSamples; ++i)
                        {
                            const float x = static_cast<float>(
                                0.8 * std::sin(2.0 * juce::MathConstants<double>::pi * freq * i / sr));
                            ref.setSample(0, i, x);
                            ref.setSample(1, i, x);
                        }

                        juce::AudioBuffer<float> dut;
                        dut.makeCopyOf(ref);
                        filter.reset();
                        processChunked(filter, dut, true, bs);

                        double sumRefSq = 0.0, sumDutSq = 0.0, sumRefMean = 0.0, sumDutMean = 0.0;
                        double refSin = 0.0, refCos = 0.0, dutSin = 0.0, dutCos = 0.0;
                        float maxSampleErr = 0.0f;
                        const int count = totalSamples - settleStart;

                        for (int i = settleStart; i < totalSamples; ++i)
                        {
                            const double ph = 2.0 * juce::MathConstants<double>::pi * freq * i / sr;
                            const double r = ref.getSample(0, i);
                            const double d = dut.getSample(0, i);
                            expect(std::isfinite(d) && std::isfinite(dut.getSample(1, i)),
                                   "MonoLock output must stay finite");
                            sumRefSq += r * r;
                            sumDutSq += d * d;
                            sumRefMean += r;
                            sumDutMean += d;
                            const double sBasis = std::sin(ph);
                            const double cBasis = std::cos(ph);
                            refCos += r * sBasis;
                            refSin += r * cBasis;
                            dutCos += d * sBasis;
                            dutSin += d * cBasis;
                            maxSampleErr = juce::jmax(maxSampleErr,
                                std::abs(dut.getSample(0, i) - ref.getSample(0, i)),
                                std::abs(dut.getSample(1, i) - ref.getSample(1, i)));
                        }

                        const double dbDelta = 20.0 * std::log10(
                            std::sqrt(sumDutSq / count) / juce::jmax(1e-12, std::sqrt(sumRefSq / count)));
                        double phaseDeltaDeg = (std::atan2(dutSin, dutCos) - std::atan2(refSin, refCos))
                                               * 180.0 / juce::MathConstants<double>::pi;
                        while (phaseDeltaDeg > 180.0) phaseDeltaDeg -= 360.0;
                        while (phaseDeltaDeg < -180.0) phaseDeltaDeg += 360.0;
                        const double dcAdded = (sumDutMean - sumRefMean) / count;

                        expect(std::abs(dbDelta) <= 0.1,
                               "Mono L=R magnitude delta must be within +-0.1 dB across 20-200 Hz");
                        expect(std::abs(phaseDeltaDeg) < 0.05,
                               "Mono L=R phase shift must be ~0 deg");
                        expect(maxSampleErr < 1e-6f,
                               "Mono L=R waveform must be bit-transparent");
                        expect(std::abs(dcAdded) < 1e-5,
                               "Mono L=R must not introduce DC offset");
                    }

                    // Transient & impulse preservation on L=R
                    AllyDSP::MonoLockFilter transientFilter;
                    transientFilter.prepare({ sr, static_cast<juce::uint32>(bs), 2 });
                    const int nTrans = static_cast<int>(sr * 0.1);
                    juce::AudioBuffer<float> impRef(2, nTrans), impDut(2, nTrans);
                    impRef.clear();
                    impRef.setSample(0, 64, 1.0f);
                    impRef.setSample(1, 64, 1.0f);
                    double ph = 0.0;
                    for (int i = 128; i < nTrans; ++i)
                    {
                        const double t = static_cast<double>(i - 128) / sr;
                        const double f = 55.0 + 545.0 * std::exp(-t / 0.004);
                        ph += 2.0 * juce::MathConstants<double>::pi * f / sr;
                        const float chirp = static_cast<float>(0.65 * std::sin(ph) * std::exp(-t / 0.05));
                        impRef.setSample(0, i, chirp);
                        impRef.setSample(1, i, chirp);
                    }
                    impDut.makeCopyOf(impRef);
                    processChunked(transientFilter, impDut, true, bs);

                    float maxTransErr = 0.0f;
                    for (int i = 0; i < nTrans; ++i)
                    {
                        maxTransErr = juce::jmax(maxTransErr,
                            std::abs(impDut.getSample(0, i) - impRef.getSample(0, i)),
                            std::abs(impDut.getSample(1, i) - impRef.getSample(1, i)));
                    }
                    expect(maxTransErr < 1e-6f, "Mono impulse and 808 knock chirp must have zero transient dispersion");
                }
            }
        }

        beginTest("Side suppression below 90 Hz and stereo phase invariance / reconstruction");
        {
            for (double sr : sampleRates)
            {
                AllyDSP::MonoLockFilter filter;
                filter.prepare({ sr, 256, 2 });

                auto measureStereo = [&](double freqHz, double ampL, double ampR, double phaseOffsetR)
                {
                    const int total = static_cast<int>(sr * 0.6);
                    const int start = static_cast<int>(sr * 0.3);
                    juce::AudioBuffer<float> buf(2, total);
                    for (int i = 0; i < total; ++i)
                    {
                        const double ph = 2.0 * juce::MathConstants<double>::pi * freqHz * i / sr;
                        buf.setSample(0, i, static_cast<float>(ampL * std::sin(ph)));
                        buf.setSample(1, i, static_cast<float>(ampR * std::sin(ph + phaseOffsetR)));
                    }

                    juce::AudioBuffer<float> inCopy;
                    inCopy.makeCopyOf(buf);
                    filter.reset();
                    processChunked(filter, buf, true, 256);

                    double inSideSq = 0.0, outSideSq = 0.0, outLSq = 0.0, outRSq = 0.0, meanL = 0.0, meanR = 0.0;
                    float maxSumErr = 0.0f;
                    const int n = total - start;
                    for (int i = start; i < total; ++i)
                    {
                        const double iL = inCopy.getSample(0, i);
                        const double iR = inCopy.getSample(1, i);
                        const double oL = buf.getSample(0, i);
                        const double oR = buf.getSample(1, i);
                        expect(std::isfinite(oL) && std::isfinite(oR), "Stereo output must remain finite");
                        const double iS = 0.5 * (iL - iR);
                        const double oS = 0.5 * (oL - oR);
                        inSideSq += iS * iS;
                        outSideSq += oS * oS;
                        outLSq += oL * oL;
                        outRSq += oR * oR;
                        meanL += oL;
                        meanR += oR;
                        maxSumErr = juce::jmax(maxSumErr,
                            static_cast<float>(std::abs((oL + oR) - (iL + iR))));
                    }
                    struct Res { double sideAttenDb; double lDb; double rDb; float sumErr; double dcMax; };
                    return Res {
                        20.0 * std::log10(std::sqrt(outSideSq / n) / juce::jmax(1e-12, std::sqrt(inSideSq / n))),
                        20.0 * std::log10(juce::jmax(1e-12, std::sqrt(outLSq / n))),
                        20.0 * std::log10(juce::jmax(1e-12, std::sqrt(outRSq / n))),
                        maxSumErr,
                        juce::jmax(std::abs(meanL / n), std::abs(meanR / n))
                    };
                };

                // 180 deg pure side suppression curve
                const auto s20  = measureStereo(20.0,  0.75, -0.75, 0.0);
                const auto s30  = measureStereo(30.0,  0.75, -0.75, 0.0);
                const auto s41  = measureStereo(41.2,  0.75, -0.75, 0.0);
                const auto s65  = measureStereo(65.0,  0.75, -0.75, 0.0);
                const auto s90  = measureStereo(90.0,  0.75, -0.75, 0.0);
                const auto s200 = measureStereo(200.0, 0.75, -0.75, 0.0);

                expect(s20.sideAttenDb  < -50.0, "20 Hz Side must be strongly suppressed (<-50 dB)");
                expect(s30.sideAttenDb  < -37.0, "30 Hz Side must be strongly suppressed (<-37 dB)");
                expect(s41.sideAttenDb  < -26.0, "41.2 Hz Side must be suppressed (<-26 dB)");
                expect(s65.sideAttenDb  < -12.5, "65 Hz Side must be suppressed (<-12.5 dB)");
                expect(s90.sideAttenDb  < -5.5 && s90.sideAttenDb > -6.6, "90 Hz LR4 Side crossover must be ~-6 dB");
                expect(s200.sideAttenDb > -0.5 && s200.sideAttenDb <= 0.01, "200 Hz Side must pass cleanly");
                expect(s30.dcMax < 1e-4 && s90.dcMax < 1e-4, "Must not add DC offset");

                // 90 deg quadrature stereo preserves L+R sum and suppresses sub side
                const auto q30 = measureStereo(30.0, 0.7, 0.7, juce::MathConstants<double>::halfPi);
                const auto q90 = measureStereo(90.0, 0.7, 0.7, juce::MathConstants<double>::halfPi);
                expect(q30.sumErr < 1e-5f && q90.sumErr < 1e-5f,
                       "90 deg stereo mono sum L+R must reconstruct identically");
                expect(q30.sideAttenDb < -37.0, "90 deg sub Side at 30 Hz must collapse below -37 dB");

                // Hard-panned stereo collapses sub to center and preserves top panning & L+R sum
                const auto pan30  = measureStereo(30.0,  0.8, 0.0, 0.0);
                const auto pan200 = measureStereo(200.0, 0.8, 0.0, 0.0);
                const auto pan400 = measureStereo(400.0, 0.8, 0.0, 0.0);
                expect(pan30.sumErr < 1e-5f && pan200.sumErr < 1e-5f && pan400.sumErr < 1e-5f,
                       "Hard-panned stereo L+R sum must stay exact");
                expect(std::abs(pan30.lDb - pan30.rDb) < 0.25, "Hard-panned 30 Hz sub must lock to center");
                expect((pan200.lDb - pan200.rDb) > 1.5, "Hard-panned 200 Hz top must remain left-weighted");
                expect((pan400.lDb - pan400.rDb) > 8.0, "Hard-panned 400 Hz upper band must retain clear L panning");

                // Composite signal: 40 Hz out-of-phase sub + 180 Hz quadrature top
                const int compLen = static_cast<int>(sr * 0.6);
                const int compStart = static_cast<int>(sr * 0.3);
                juce::AudioBuffer<float> comp(2, compLen);
                for (int i = 0; i < compLen; ++i)
                {
                    const double phSub = 2.0 * juce::MathConstants<double>::pi * 40.0 * i / sr;
                    const double phTop = 2.0 * juce::MathConstants<double>::pi * 180.0 * i / sr;
                    const float sub = static_cast<float>(0.5 * std::sin(phSub));
                    comp.setSample(0, i, sub + static_cast<float>(0.4 * std::sin(phTop)));
                    comp.setSample(1, i, -sub + static_cast<float>(0.4 * std::cos(phTop)));
                }
                filter.reset();
                processChunked(filter, comp, true, 256);

                double subS = 0.0, subC = 0.0, topS = 0.0, topC = 0.0;
                const int cn = compLen - compStart;
                for (int i = compStart; i < compLen; ++i)
                {
                    const double phSub = 2.0 * juce::MathConstants<double>::pi * 40.0 * i / sr;
                    const double phTop = 2.0 * juce::MathConstants<double>::pi * 180.0 * i / sr;
                    const double s = 0.5 * (comp.getSample(0, i) - comp.getSample(1, i));
                    subS += s * std::sin(phSub);
                    subC += s * std::cos(phSub);
                    topS += s * std::sin(phTop);
                    topC += s * std::cos(phTop);
                }
                const double sideSubAmp = 2.0 * std::hypot(subS, subC) / cn;
                const double sideTopAmp = 2.0 * std::hypot(topS, topC) / cn;
                expect(sideSubAmp < 0.03, "40 Hz Side sub component (~0.5 in) must be rejected (<-24 dB)");
                expect(sideTopAmp > 0.24, "180 Hz Side top stereo (~0.28 in) must survive in composite mix");
            }
        }

        beginTest("Click-free Mono Lock ON/OFF toggle, rapid retriggers, and all 14 factory presets on C2");
        {
            // 1. Live toggle ON/OFF/ON on active worst-case 50 Hz L=-R stereo and mono L=R
            for (double sr : sampleRates)
            {
                for (int bs : blockSizes)
                {
                    AllyDSP::MonoLockFilter filter;
                    filter.prepare({ sr, static_cast<juce::uint32>(bs), 2 });
                    const int totalBlocks = juce::jmax(16, static_cast<int>(sr * 0.25) / bs);
                    juce::AudioBuffer<float> block(2, bs);
                    float prevL = 0.0f, prevR = 0.0f;
                    float maxDelta = 0.0f;
                    int sampleCounter = 0;

                    for (int b = 0; b < totalBlocks; ++b)
                    {
                        const bool enabled = ((b / (totalBlocks / 4)) % 2) == 0;
                        for (int i = 0; i < bs; ++i)
                        {
                            const double ph = 2.0 * juce::MathConstants<double>::pi * 50.0 * sampleCounter++ / sr;
                            const float s = static_cast<float>(0.8 * std::sin(ph));
                            block.setSample(0, i, s);
                            block.setSample(1, i, -s);
                        }
                        filter.process(block, enabled);

                        for (int i = 0; i < bs; ++i)
                        {
                            const float l = block.getSample(0, i);
                            const float r = block.getSample(1, i);
                            expect(std::isfinite(l) && std::isfinite(r) && std::abs(l) <= 1.0f && std::abs(r) <= 1.0f,
                                   "Toggle transition must stay finite and unclipped");
                            if (b > 0 || i > 0)
                            {
                                maxDelta = juce::jmax(maxDelta, std::abs(l - prevL), std::abs(r - prevR));
                            }
                            prevL = l;
                            prevR = r;
                        }
                    }

                    expect(maxDelta < 0.03f,
                           "Live Mono Lock ON/OFF toggle on active stereo must remain click-free");
                }
            }

            // 2. All 14 factory presets on C2 (65.4 Hz): Mono Lock ON vs OFF delta must be within +-0.1 dB
            const auto& presets = AllyPresets::getFactoryPresets();
            expectEquals(static_cast<int>(presets.size()), 14);

            for (int pIdx = 0; pIdx < static_cast<int>(presets.size()); ++pIdx)
            {
                auto measurePresetC2 = [&](bool monoLockOn)
                {
                    AllyBassAudioProcessor proc;
                    proc.setPlayConfigDetails(0, 2, 44100.0, 256);
                    proc.prepareToPlay(44100.0, 256);
                    AllyPresets::applyPreset(proc.apvts, pIdx);
                    setParamPlainValue(proc.apvts, "monoLock1300", monoLockOn ? 1.0f : 0.0f);

                    juce::AudioBuffer<float> buf(2, 256);
                    juce::MidiBuffer midi;
                    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.95f), 0);

                    double sumSq = 0.0;
                    int countedSamples = 0;
                    for (int b = 0; b < 45; ++b) // ~260 ms total
                    {
                        buf.clear();
                        proc.processBlock(buf, midi);
                        midi.clear();
                        if (b >= 15) // ~87 ms onwards: steady sub & post-transient tail
                        {
                            for (int ch = 0; ch < 2; ++ch)
                            {
                                const float* ptr = buf.getReadPointer(ch);
                                for (int s = 0; s < buf.getNumSamples(); ++s)
                                    sumSq += static_cast<double>(ptr[s]) * static_cast<double>(ptr[s]);
                            }
                            countedSamples += buf.getNumSamples() * 2;
                        }
                    }
                    return 20.0 * std::log10(juce::jmax(1e-12, std::sqrt(sumSq / countedSamples)));
                };

                const double offDb = measurePresetC2(false);
                const double onDb  = measurePresetC2(true);
                const double deltaDb = onDb - offDb;
                DBG("Preset[" << pIdx << "] " << presets[static_cast<size_t>(pIdx)].name
                    << " C2 ON-OFF delta = " << juce::String(deltaDb, 6) << " dB");
                expect(std::abs(deltaDb) < 0.1,
                       "Preset C2 output with Mono Lock ON vs OFF must match within +-0.1 dB");
            }

            // 3. Rapid MIDI retriggers + preset switches with Mono Lock toggled ON/OFF
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 128);
                proc.prepareToPlay(48000.0, 128);

                juce::AudioBuffer<float> buf(2, 128);
                juce::MidiBuffer midi;
                const int notes[] = { 28, 36, 41, 33, 45, 31 };
                float maxPeak = 0.0f;

                for (int b = 0; b < 56; ++b)
                {
                    midi.clear();
                    if (b % 4 == 0)
                    {
                        const int presetIdx = (b / 4) % 14;
                        AllyPresets::applyPreset(proc.apvts, presetIdx);
                        setParamPlainValue(proc.apvts, "monoLock1300", ((b / 4) % 2 == 0) ? 1.0f : 0.0f);
                        const int note = notes[(b / 4) % 6];
                        midi.addEvent(juce::MidiMessage::noteOn(1, note, 0.9f), 0);
                    }
                    else if (b % 4 == 3)
                    {
                        midi.addEvent(juce::MidiMessage::allNotesOff(1), 64);
                    }

                    buf.clear();
                    proc.processBlock(buf, midi);

                    for (int ch = 0; ch < 2; ++ch)
                    {
                        const float* ptr = buf.getReadPointer(ch);
                        for (int s = 0; s < buf.getNumSamples(); ++s)
                        {
                            expect(std::isfinite(ptr[s]), "Rapid retrigger/preset switch audio must be finite");
                            maxPeak = juce::jmax(maxPeak, std::abs(ptr[s]));
                        }
                    }
                }
                expect(maxPeak < 2.0f && maxPeak > 0.01f, "Rapid preset/MIDI retriggers must remain stable");
            }
        }
    }
};

class AttackEnvelopeD1BTests final : public juce::UnitTest
{
public:
    AttackEnvelopeD1BTests() : juce::UnitTest("ALLY1300 Attack Envelope & Velocity (D.1B)", "AllyBass") {}

    void runTest() override
    {
        struct ScheduledNote
        {
            int samplePos = 0;
            bool isNoteOn = true;
            int note = 69;
            float velocity = 1.0f;
        };

        auto renderSequence = [](double sr,
                                 int blockSize,
                                 int totalSamples,
                                 float attackMs,
                                 float releaseMs,
                                 float polderPunch,
                                 float glideMs,
                                 const std::vector<ScheduledNote>& events)
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, sr, blockSize);
            proc.prepareToPlay(sr, blockSize);

            setParamPlainValue(proc.apvts, "diepte", 0.0f);
            setParamPlainValue(proc.apvts, "polderPunch", polderPunch);
            setParamPlainValue(proc.apvts, "havenDirt", 0.0f);
            setParamPlainValue(proc.apvts, "stadsdeel", 2.0f); // clean Poort
            setParamPlainValue(proc.apvts, "overdrive036", 0.0f);
            setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);
            setParamPlainValue(proc.apvts, "masterVolume", 0.0f);
            setParamPlainValue(proc.apvts, "attack", attackMs);
            setParamPlainValue(proc.apvts, "release", releaseMs);
            setParamPlainValue(proc.apvts, "waterwijkGlide", glideMs);

            std::vector<float> out;
            out.reserve(static_cast<size_t>(totalSamples));
            juce::AudioBuffer<float> buf(2, blockSize);
            juce::MidiBuffer midi;

            for (int off = 0; off < totalSamples; off += blockSize)
            {
                const int n = juce::jmin(blockSize, totalSamples - off);
                midi.clear();
                for (const auto& ev : events)
                {
                    if (ev.samplePos >= off && ev.samplePos < off + n)
                    {
                        const int localPos = ev.samplePos - off;
                        midi.addEvent(ev.isNoteOn
                                          ? juce::MidiMessage::noteOn(1, ev.note, ev.velocity)
                                          : juce::MidiMessage::noteOff(1, ev.note),
                                      localPos);
                    }
                }

                buf.setSize(2, n, false, false, true);
                buf.clear();
                proc.processBlock(buf, midi);
                const float* ch0 = buf.getReadPointer(0);
                out.insert(out.end(), ch0, ch0 + n);
            }
            return out;
        };

        auto measureT90Ms = [](const std::vector<float>& audio, double sr, int startSample, int steadyStart, double& outSteadyPeak)
        {
            outSteadyPeak = 0.0;
            for (int i = steadyStart; i < static_cast<int>(audio.size()); ++i)
                outSteadyPeak = juce::jmax(outSteadyPeak, static_cast<double>(std::abs(audio[static_cast<size_t>(i)])));

            const int halfWin = juce::jmax(4, static_cast<int>(sr / 880.0));
            for (int i = startSample; i < static_cast<int>(audio.size()); ++i)
            {
                const int a = juce::jmax(startSample, i - halfWin / 2);
                const int b = juce::jmin(static_cast<int>(audio.size()) - 1, i + halfWin / 2);
                double localPk = 0.0;
                for (int k = a; k <= b; ++k)
                    localPk = juce::jmax(localPk, static_cast<double>(std::abs(audio[static_cast<size_t>(k)])));
                if (localPk >= 0.90 * outSteadyPeak)
                    return 1000.0 * static_cast<double>(i - startSample) / sr;
            }
            return -1.0;
        };

        beginTest("Buffer-size invariance (<= 1 sample) across 44.1/48/96 kHz, 32..16384 BS, and Attack 1..300 ms");
        {
            constexpr double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
            constexpr int blockSizes[] = { 32, 64, 256, 1024 };
            constexpr float attackTimes[] = { 1.0f, 2.0f, 3.5f, 8.0f, 25.0f, 85.0f, 300.0f };

            for (double sr : sampleRates)
            {
                const int totalSamples = static_cast<int>(sr * 0.42);
                const int steadyStart = static_cast<int>(sr * 0.34);
                const int midiOffset = 19; // mid-block sample-accurate trigger

                std::vector<double> prevT90;
                for (float attMs : attackTimes)
                {
                    const std::vector<ScheduledNote> seq = {
                        { midiOffset, true, 69, 0.5f },
                        { static_cast<int>(sr * 0.38), false, 69, 0.0f }
                    };

                    const auto refBuf = renderSequence(sr, 16384, totalSamples, attMs, 150.0f, 0.0f, 0.0f, seq);
                    double refSteadyPk = 0.0;
                    const double refT90 = measureT90Ms(refBuf, sr, midiOffset, steadyStart, refSteadyPk);
                    expect(refSteadyPk > 0.25, "Note at vel=0.5 must reach steady peak");
                    expect(refT90 >= 0.0, "Attack t90 must be reached");
                    prevT90.push_back(refT90);

                    for (int bs : blockSizes)
                    {
                        const auto bsBuf = renderSequence(sr, bs, totalSamples, attMs, 150.0f, 0.0f, 0.0f, seq);
                        expectEquals(static_cast<int>(bsBuf.size()), static_cast<int>(refBuf.size()));

                        float maxWaveformDiff = 0.0f;
                        for (size_t s = 0; s < refBuf.size(); ++s)
                        {
                            expect(std::isfinite(bsBuf[s]), "Buffer-size invariance output must be finite");
                            maxWaveformDiff = juce::jmax(maxWaveformDiff, std::abs(bsBuf[s] - refBuf[s]));
                        }
                        expect(maxWaveformDiff < 1e-5f,
                               "Attack/Release waveform must be sample-exact identical across BS 32..16384");
                    }
                }

                // Verify genuine sub-8 ms Attack differentiation: 1 < 2 < 3.5 < 8 < 25 < 85 < 300 ms
                expect(prevT90[0] < prevT90[1] && prevT90[1] < prevT90[2] && prevT90[2] < prevT90[3]
                    && prevT90[3] < prevT90[4] && prevT90[4] < prevT90[5] && prevT90[5] < prevT90[6],
                       "Attack 1..300 ms (including <8 ms) must differentiate monotonically without hidden 8 ms floor");
            }
        }

        beginTest("Velocity amplitude scaling & attack timing invariance across 0.1..1.0 and 1 ms safety");
        {
            constexpr float velocities[] = { 0.10f, 0.25f, 0.50f, 0.75f, 1.00f };
            constexpr double sr = 48000.0;
            constexpr int bs = 256;
            const int totalSamples = static_cast<int>(sr * 0.20);
            const int steadyStart = static_cast<int>(sr * 0.14);

            // 85 ms Attack must have identical t90 across all velocities 0.1 .. 1.0
            double baseT90 = -1.0;
            double basePeak = 0.0;
            for (int v = 4; v >= 0; --v)
            {
                const float vel = velocities[v];
                const auto buf = renderSequence(sr, bs, totalSamples, 85.0f, 200.0f, 0.0f, 0.0f,
                                                { { 0, true, 69, vel } });
                double steadyPk = 0.0;
                const double t90 = measureT90Ms(buf, sr, 0, steadyStart, steadyPk);
                if (v == 4)
                {
                    baseT90 = t90;
                    basePeak = steadyPk;
                }
                else
                {
                    expectWithinAbsoluteError(static_cast<float>(t90), static_cast<float>(baseT90),
                                              static_cast<float>(1000.0 / sr));
                    expectWithinAbsoluteError(static_cast<float>(steadyPk),
                                              static_cast<float>(basePeak * vel), 0.015f);
                }
            }

            // 1.0 ms fast Attack across 44.1/48/96 kHz, Punch OFF & ON: finite, no overshoot, no step clicks
            for (double testSr : { 44100.0, 48000.0, 96000.0 })
            {
                for (float punch : { 0.0f, 1.0f })
                {
                    const auto buf = renderSequence(testSr, 128, static_cast<int>(testSr * 0.12),
                                                    1.0f, 150.0f, punch, 0.0f,
                                                    { { 0, true, 36, 1.0f } });
                    float maxPk = 0.0f, maxDelta = 0.0f;
                    for (size_t i = 0; i < buf.size(); ++i)
                    {
                        expect(std::isfinite(buf[i]), "1 ms attack output must remain finite");
                        maxPk = juce::jmax(maxPk, std::abs(buf[i]));
                        if (i > 0)
                            maxDelta = juce::jmax(maxDelta, std::abs(buf[i] - buf[i - 1]));
                    }
                    expect(maxPk <= 1.15f && maxPk > 0.25f, "1 ms attack amplitude must stay controlled");
                    expect(maxDelta < 0.07f, "1 ms attack must not produce discontinuity click");
                }
            }
        }

        beginTest("Fast note-on/off, legato overlap, Attack/Release retriggers, Polder Punch & Waterwijk Glide interaction");
        {
            constexpr double sr = 48000.0;

            // 1. Retrigger from loud sustain/release (vel=1.0) down to quiet (vel=0.25) -> zero amplitude snap click
            {
                const std::vector<ScheduledNote> seq = {
                    { 0,    true,  36, 1.00f },
                    { 3072, false, 36, 0.00f },
                    { 3584, true,  36, 0.25f } // retrigger mid-Release at lower velocity
                };
                const auto bufBs64   = renderSequence(sr, 64,   12000, 25.0f, 100.0f, 0.0f, 0.0f, seq);
                const auto bufBs2048 = renderSequence(sr, 2048, 12000, 25.0f, 100.0f, 0.0f, 0.0f, seq);

                float maxRetrigStep = 0.0f;
                float maxBsDiff = 0.0f;
                for (size_t i = 1; i < bufBs64.size(); ++i)
                {
                    expect(std::isfinite(bufBs64[i]), "Release retrigger output must be finite");
                    maxBsDiff = juce::jmax(maxBsDiff, std::abs(bufBs64[i] - bufBs2048[i]));
                    if (i >= 3072 && i <= 4500)
                        maxRetrigStep = juce::jmax(maxRetrigStep, std::abs(bufBs64[i] - bufBs64[i - 1]));
                }
                expect(maxBsDiff < 1e-6f, "Retrigger sequence must match identically across BS 64 vs 2048");
                expect(maxRetrigStep < 0.02f, "Downward velocity retrigger mid-Release must stay completely click-free");
            }

            // 2. Retrigger mid-Attack + Legato overlap with Polder Punch & Waterwijk Glide (0 ms and 80 ms)
            for (float glideMs : { 0.0f, 80.0f })
            {
                for (float punch : { 0.0f, 0.8f })
                {
                    const std::vector<ScheduledNote> seq = {
                        { 0,     true,  36, 0.85f }, // initial attack (85 ms)
                        { 600,   true,  48, 0.35f }, // overlapping legato mid-Attack (vel drop!)
                        { 2400,  false, 48, 0.00f }, // release upper note -> glide back to 36
                        { 4800,  false, 36, 0.00f }, // release to tail
                        { 5200,  true,  40, 0.95f }, // fast retrigger during Release
                        { 6000,  false, 40, 0.00f },
                        { 6200,  true,  33, 0.60f }, // quick detached staccato
                        { 6450,  false, 33, 0.00f }
                    };
                    const auto b32   = renderSequence(sr, 32,   14000, 85.0f, 35.0f, punch, glideMs, seq);
                    const auto b1024 = renderSequence(sr, 1024, 14000, 85.0f, 35.0f, punch, glideMs, seq);

                    float maxPk = 0.0f, maxJump = 0.0f, maxDiff = 0.0f;
                    for (size_t i = 0; i < b32.size(); ++i)
                    {
                        expect(std::isfinite(b32[i]), "Legato/retrigger stress output must remain finite");
                        maxPk = juce::jmax(maxPk, std::abs(b32[i]));
                        maxDiff = juce::jmax(maxDiff, std::abs(b32[i] - b1024[i]));
                        if (i > 0)
                            maxJump = juce::jmax(maxJump, std::abs(b32[i] - b32[i - 1]));
                    }
                    expect(maxDiff < 1e-5f, "Legato/retrigger + Glide + Punch must be buffer-size invariant");
                    expect(maxPk <= 1.15f, "Must never overshoot or clip abnormally");
                    expect(maxJump < 0.06f, "Legato & retrigger transitions must stay click-free");
                    expect(std::abs(b32.back()) < 0.02f, "Note-off tail must decay cleanly");
                }
            }
        }
    }
};

class ConcurrencyRealtimeSafetyD1CTests final : public juce::UnitTest
{
public:
    ConcurrencyRealtimeSafetyD1CTests()
        : juce::UnitTest("ALLY1300 Concurrency, Realtime Safety & Attack Phase (D.1C)", "AllyBass") {}

    void runTest() override
    {
        beginTest("Direct internal envelope phase verifies exact single-sample Attack timing independent of oscillator");
        {
            const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
            const float attacksMs[] = { 1.0f, 3.5f, 8.0f, 85.0f, 300.0f };
            const float velocities[] = { 0.1f, 0.5f, 1.0f };

            for (double sr : sampleRates)
            {
                for (float attackMs : attacksMs)
                {
                    for (float vel : velocities)
                    {
                        AllyBassAudioProcessor proc;
                        proc.setPlayConfigDetails(0, 2, sr, 512);
                        proc.prepareToPlay(sr, 512);
                        setParamPlainValue(proc.apvts, "attack", attackMs);
                        setParamPlainValue(proc.apvts, "release", 250.0f);
                        setParamPlainValue(proc.apvts, "polderPunch", 0.0f);

                        // Exact expected sample count to reach envLevel >= 1.0f for normalized rate 1/N
                        const float attackSec = juce::jmax(0.001f, attackMs * 0.001f);
                        const float exactRate = 1.0f / juce::jmax(1.0f, attackSec * static_cast<float>(sr));
                        int expectedSamples = 0;
                        float simLevel = 0.0f;
                        while (simLevel < 1.0f && expectedSamples < 100000)
                        {
                            simLevel += exactRate;
                            ++expectedSamples;
                        }
                        expect(expectedSamples >= 1, "Expected attack sample count must be >= 1");

                        const auto noteOnMsg = juce::MidiMessage::noteOn(1, 36, vel);
                        const float expectedMidiVel = noteOnMsg.getFloatVelocity();

                        // Step exact N - 1 samples across a first block, then 1 single sample in the next block
                        const int preSamples = expectedSamples - 1;
                        if (preSamples > 0)
                        {
                            juce::AudioBuffer<float> preBuf(2, preSamples);
                            juce::MidiBuffer midi;
                            midi.addEvent(noteOnMsg, 0);
                            preBuf.clear();
                            proc.processBlock(preBuf, midi);

                            expectEquals(proc.getEnvelopeStateInt(), 1); // EnvelopeState::Attack
                            expect(proc.getEnvelopeLevel() < 1.0f,
                                   "At sample N-1 internal envelope level must still be in Attack (< 1.0)");
                        }

                        juce::AudioBuffer<float> finalBuf(2, 1);
                        juce::MidiBuffer finalMidi;
                        if (preSamples == 0)
                            finalMidi.addEvent(noteOnMsg, 0);
                        finalBuf.clear();
                        proc.processBlock(finalBuf, finalMidi);

                        expectEquals(proc.getEnvelopeStateInt(), 2); // EnvelopeState::Sustain
                        expectWithinAbsoluteError(proc.getEnvelopeLevel(), 1.0f, 1e-6f);
                        expectWithinAbsoluteError(proc.getCurrentVelocityGain(), expectedMidiVel, 1e-5f);
                    }
                }
            }
        }

        beginTest("Triple-buffered scope and output meter remain snapshot-consistent across SR, BS 32..16384, and editor lifecycle");
        {
            const int blockSizes[] = { 32, 256, 1024, 4096, 16384 };
            for (int bs : blockSizes)
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, bs);
                proc.prepareToPlay(48000.0, bs);
                setParamPlainValue(proc.apvts, "diepte", 0.0f);
                setParamPlainValue(proc.apvts, "polderPunch", 0.0f);
                setParamPlainValue(proc.apvts, "havenDirt", 0.0f);
                setParamPlainValue(proc.apvts, "attack", 1.0f);
                setParamPlainValue(proc.apvts, "masterVolume", 0.0f);

                std::atomic<bool> consumerReady { false };
                std::atomic<bool> stop { false };
                std::atomic<int> tornFrames { 0 };
                std::atomic<bool> sawNonFinite { false };
                std::atomic<uint64_t> prevMeterCounter { 0 };
                std::atomic<uint64_t> consumerReads { 0 };
                std::atomic<bool> counterWentBackwards { false };

                juce::AudioBuffer<float> buf(2, bs);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0); // 440 Hz steady sine
                buf.clear();
                proc.processBlock(buf, midi);
                midi.clear();

                std::thread consumer([&]()
                {
                    std::array<float, 1536> snap {};
                    consumerReady.store(true, std::memory_order_release);
                    while (!stop.load(std::memory_order_acquire))
                    {
                        proc.getVisualizerData(snap.data(), 1536);
                        for (size_t i = 0; i < snap.size(); ++i)
                        {
                            if (!std::isfinite(snap[i]))
                                sawNonFinite.store(true, std::memory_order_relaxed);
                            if (i > 0 && std::abs(snap[i] - snap[i - 1]) > 0.25f)
                            {
                                tornFrames.fetch_add(1, std::memory_order_relaxed);
                                break;
                            }
                        }

                        const auto m = proc.peekOutputMeterMetrics();
                        if (!std::isfinite(m.peakLinear) || !std::isfinite(m.rmsLinear))
                            sawNonFinite.store(true, std::memory_order_relaxed);
                        const uint64_t prev = prevMeterCounter.load(std::memory_order_relaxed);
                        if (m.updateCounter < prev)
                            counterWentBackwards.store(true, std::memory_order_relaxed);
                        prevMeterCounter.store(m.updateCounter, std::memory_order_relaxed);
                        consumerReads.fetch_add(1, std::memory_order_release);
                    }
                });

                while (!consumerReady.load(std::memory_order_acquire))
                    std::this_thread::yield();

                const int numBlocks = (bs <= 256) ? 200 : 80;
                int executedBlocks = 0;
                while (executedBlocks < numBlocks || consumerReads.load(std::memory_order_acquire) < 4)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                    ++executedBlocks;
                }

                stop.store(true, std::memory_order_release);
                consumer.join();

                expect(consumerReads.load() >= 4, "Concurrent consumer thread must overlap with producer");
                expectEquals(tornFrames.load(), 0);
                expect(!sawNonFinite.load(), "Consumer must never observe NaN/Inf floats");
                expect(!counterWentBackwards.load(), "Meter counter must remain monotonic");
                expectEquals(static_cast<int>(proc.getVisualizerWriteCounter()), executedBlocks + 1);
            }

            // Long GUI preemption simulation (>500 producer blocks cycling triple slots cleanly with 0 torn frames)
            {
                AllyBassAudioProcessor preemptProc;
                preemptProc.setPlayConfigDetails(0, 2, 48000.0, 256);
                preemptProc.prepareToPlay(48000.0, 256);
                setParamPlainValue(preemptProc.apvts, "attack", 1.0f);
                setParamPlainValue(preemptProc.apvts, "diepte", 0.0f);
                setParamPlainValue(preemptProc.apvts, "havenDirt", 0.0f);

                juce::AudioBuffer<float> pBuf(2, 256);
                juce::MidiBuffer pMidi;
                pMidi.addEvent(juce::MidiMessage::noteOn(1, 69, 1.0f), 0);
                pBuf.clear();
                preemptProc.processBlock(pBuf, pMidi);
                pMidi.clear();

                // Take an initial snapshot (claims consumer slot), then preempt consumer while producer wraps 600 blocks
                std::array<float, 1536> snapA {};
                preemptProc.getVisualizerData(snapA.data(), 1536);

                for (int i = 0; i < 600; ++i)
                {
                    pBuf.clear();
                    preemptProc.processBlock(pBuf, pMidi);
                }

                std::array<float, 1536> snapB {};
                preemptProc.getVisualizerData(snapB.data(), 1536);
                for (size_t i = 1; i < snapB.size(); ++i)
                {
                    expect(std::isfinite(snapB[i]), "Post-preemption scope sample must be finite");
                    expect(std::abs(snapB[i] - snapB[i - 1]) <= 0.25f,
                           "Post-preemption scope snapshot must stay phase-continuous without wrap tear");
                }
                const auto mAfter = preemptProc.consumeOutputMeterMetrics();
                expectEquals(static_cast<int>(mAfter.updateCounter), 601);
            }

            // Editor open/close/reopen + sample-rate change during live audio processing
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
            buf.clear();
            proc.processBlock(buf, midi);
            midi.clear();

            for (int cycle = 0; cycle < 3; ++cycle)
            {
                std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
                expect(ed != nullptr);
                ed->setSize(780, 580);
                buf.clear();
                proc.processBlock(buf, midi);
                std::array<float, 1536> snap {};
                proc.getVisualizerData(snap.data(), 1536);
                expect(std::isfinite(snap.back()), "Scope snapshot after editor open must be valid");
                ed.reset();
                buf.clear();
                proc.processBlock(buf, midi);
            }

            proc.releaseResources();
            proc.prepareToPlay(96000.0, 512);
            std::array<float, 1536> zeroSnap {};
            proc.getVisualizerData(zeroSnap.data(), 1536);
            float peakAfterReset = 0.0f;
            for (float s : zeroSnap)
                peakAfterReset = juce::jmax(peakAfterReset, std::abs(s));
            expectWithinAbsoluteError(peakAfterReset, 0.0f, 1e-6f);
        }

        beginTest("Bounded GUI->Audio MIDI queue prevents stuck notes on overflow, rapid chords, and DAW MIDI sync");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 48000.0, 256);
            proc.prepareToPlay(48000.0, 256);
            setParamPlainValue(proc.apvts, "attack", 1.0f);
            setParamPlainValue(proc.apvts, "release", 15.0f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 0.0f);

            // 1. Flood queue beyond its 256-event capacity without running processBlock, then release all notes
            for (int i = 0; i < 420; ++i)
            {
                const int note = 36 + (i % 12);
                proc.getKeyboardState().noteOn(1, note, 0.9f);
                if (i < 400)
                    proc.getKeyboardState().noteOff(1, note, 0.0f);
            }
            expect(proc.getGuiMidiQueueOverflowCount() > 0,
                   "Burst > 256 GUI events must exercise bounded overflow path");

            proc.getKeyboardState().allNotesOff(1);

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer emptyMidi;
            for (int b = 0; b < 30; ++b)
            {
                buf.clear();
                proc.processBlock(buf, emptyMidi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 0); // EnvelopeState::Idle (zero stuck notes)
            float tailPeak = 0.0f;
            for (int s = 0; s < buf.getNumSamples(); ++s)
                tailPeak = juce::jmax(tailPeak, std::abs(buf.getSample(0, s)));
            expect(tailPeak < 1e-4f, "Audio output must decay completely after overflowed allNotesOff");

            // 2. Simultaneous chord note-on/note-off ordering & release
            proc.getKeyboardState().noteOn(1, 36, 0.9f);
            proc.getKeyboardState().noteOn(1, 40, 0.9f);
            proc.getKeyboardState().noteOn(1, 43, 0.9f);
            buf.clear();
            proc.processBlock(buf, emptyMidi);
            expect(proc.getEnvelopeStateInt() != 0, "GUI note chord must trigger synth audio");
            proc.getKeyboardState().noteOff(1, 36, 0.0f);
            proc.getKeyboardState().noteOff(1, 40, 0.0f);
            buf.clear();
            proc.processBlock(buf, emptyMidi);
            expectEquals(proc.getEnvelopeStateInt(), 2); // Note 43 still held -> Sustain
            proc.getKeyboardState().noteOff(1, 43, 0.0f);
            for (int b = 0; b < 25; ++b)
            {
                buf.clear();
                proc.processBlock(buf, emptyMidi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 0); // Released cleanly

            // 3. DAW MIDI -> KeyboardState visual sync without loopback
            juce::MidiBuffer dawMidi;
            dawMidi.addEvent(juce::MidiMessage::noteOn(1, 41, 0.9f), 0);
            buf.clear();
            proc.processBlock(buf, dawMidi);
            dawMidi.clear();

            uint64_t extVer = 0;
            std::array<uint64_t, 2> extMask {};
            proc.syncExternalMidiToKeyboardState(extVer, extMask);
            expect(proc.getKeyboardState().isNoteOn(1, 41),
                   "External DAW Note-On must light corresponding key on KeyboardState");

            dawMidi.addEvent(juce::MidiMessage::noteOff(1, 41), 0);
            buf.clear();
            proc.processBlock(buf, dawMidi);
            proc.syncExternalMidiToKeyboardState(extVer, extMask);
            expect(!proc.getKeyboardState().isNoteOn(1, 41),
                   "External DAW Note-Off must clear key without stuck note");
        }

        beginTest("Concurrent preset switching, oversized blocks with Overdrive036, and parameter sanitization");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 48000.0, 256); // Prepared for 256 samples
            proc.prepareToPlay(48000.0, 256);

            std::atomic<bool> stop { false };
            std::thread presetThread([&]()
            {
                for (int i = 0; i < 42 && !stop.load(std::memory_order_acquire); ++i)
                {
                    proc.setCurrentProgram(i % proc.getNumPrograms());
                    std::this_thread::sleep_for(std::chrono::microseconds(250));
                }
            });

            // Feed normal AND oversized 2048-sample blocks while presets toggle Overdrive036/MonoLock/Stadsdeel
            juce::AudioBuffer<float> bigBuf(2, 2048);
            juce::MidiBuffer midi;
            for (int b = 0; b < 30; ++b)
            {
                midi.clear();
                if (b % 5 == 0)
                    midi.addEvent(juce::MidiMessage::noteOn(1, 36 + (b % 12), 0.9f), 0);
                else if (b % 5 == 4)
                    midi.addEvent(juce::MidiMessage::allNotesOff(1), 100);

                bigBuf.clear();
                proc.processBlock(bigBuf, midi);

                for (int ch = 0; ch < 2; ++ch)
                {
                    const float* ptr = bigBuf.getReadPointer(ch);
                    for (int s = 0; s < bigBuf.getNumSamples(); ++s)
                        expect(std::isfinite(ptr[s]), "Output must remain finite across concurrent presets and chunked Overdrive036");
                }
            }

            stop.store(true, std::memory_order_release);
            presetThread.join();
        }

        beginTest("D.1C.1 Overflow missing-NoteOn recovery, 0-sample host ping, releaseResources->prepareToPlay queue reset & DAW+GUI coexistence");
        {
            // Bug 1: Partial/full queue overflow dropping a STILL-HELD NoteOn must still trigger note + velocity via mask reconciliation
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 256);
                proc.prepareToPlay(48000.0, 256);
                setParamPlainValue(proc.apvts, "attack", 1.0f);
                setParamPlainValue(proc.apvts, "release", 15.0f);
                setParamPlainValue(proc.apvts, "waterwijkGlide", 0.0f);

                // Fill queue past partial -> full capacity using alternating noteOn/noteOff that leaves everything released
                for (int i = 0; i < 128; ++i)
                {
                    proc.getKeyboardState().noteOn(1, 36 + (i % 12), 0.3f);
                    proc.getKeyboardState().noteOff(1, 36 + (i % 12), 0.0f);
                }
                // Now queue is full (256 events): this final held NoteOn (60, vel=0.80f) drops into overflow
                proc.getKeyboardState().noteOn(1, 60, 0.80f);
                expect(proc.getGuiMidiQueueOverflowCount() > 0, "Expected queue overflow before checking held NoteOn recovery");

                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer midi;
                buf.clear();
                proc.processBlock(buf, midi);

                expect(proc.getEnvelopeStateInt() != 0,
                       "Held GUI NoteOn dropped during queue overflow must still trigger synth via bitmask reconciliation");
                expect(bufferPeak(buf) > 0.01f, "Recovered overflow NoteOn must produce audible output");
                const float expectedRecoveredVel = juce::MidiMessage::noteOn(1, 60, 0.80f).getFloatVelocity();
                expectWithinAbsoluteError(proc.getCurrentVelocityGain(), expectedRecoveredVel, 1e-4f);

                proc.getKeyboardState().noteOff(1, 60, 0.0f);
                for (int b = 0; b < 30; ++b)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                }
                expectEquals(proc.getEnvelopeStateInt(), 0);
            }

            // Bug 2: 0-sample host ping immediately after overflow must NOT discard guiMidiOverflowPending without reconciling
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 256);
                proc.prepareToPlay(48000.0, 256);
                setParamPlainValue(proc.apvts, "attack", 1.0f);
                setParamPlainValue(proc.apvts, "release", 15.0f);

                // Start note 36 and consume it cleanly in audio
                proc.getKeyboardState().noteOn(1, 36, 0.9f);
                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer midi;
                buf.clear();
                proc.processBlock(buf, midi);
                expect(proc.getEnvelopeStateInt() != 0);

                // Overflow queue with unrelated pulses, then release note 36 inside overflow
                for (int i = 0; i < 140; ++i)
                {
                    proc.getKeyboardState().noteOn(1, 48 + (i % 12), 0.5f);
                    proc.getKeyboardState().noteOff(1, 48 + (i % 12), 0.0f);
                }
                proc.getKeyboardState().noteOff(1, 36, 0.0f);

                // Host sends 0-sample ping block BEFORE normal 256-sample blocks
                juce::AudioBuffer<float> zeroBuf(2, 0);
                proc.processBlock(zeroBuf, midi);

                for (int b = 0; b < 30; ++b)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                }
                expectEquals(proc.getEnvelopeStateInt(), 0); // Must NOT stick after 0-sample block
            }

            // Bug 3: Queued GUI NoteOn before releaseResources()->prepareToPlay() must NOT replay as phantom note
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 256);
                proc.prepareToPlay(48000.0, 256);

                proc.getKeyboardState().noteOn(1, 48, 0.9f); // Queued in guiToAudioMidiQueue before processBlock
                proc.releaseResources();
                proc.prepareToPlay(48000.0, 256);

                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer midi;
                buf.clear();
                proc.processBlock(buf, midi);

                expectEquals(proc.getEnvelopeStateInt(), 0);
                expectWithinAbsoluteError(bufferPeak(buf), 0.0f, 1e-6f);
            }

            // Bug 4: Editor focusLost / octave change must release GUI/QWERTY keys WITHOUT silencing held External DAW MIDI
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 256);
                proc.prepareToPlay(48000.0, 256);

                std::unique_ptr<juce::AudioProcessorEditor> baseEd(proc.createEditor());
                auto* ed = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEd.get());
                expect(ed != nullptr);

                if (ed != nullptr)
                {
                    juce::AudioBuffer<float> buf(2, 256);
                    juce::MidiBuffer dawMidi;
                    dawMidi.addEvent(juce::MidiMessage::noteOn(1, 41, 0.92f), 0);
                    buf.clear();
                    proc.processBlock(buf, dawMidi);
                    dawMidi.clear();

                    uint64_t extVer = 0;
                    std::array<uint64_t, 2> extMask {};
                    proc.syncExternalMidiToKeyboardState(extVer, extMask);
                    expect(proc.getKeyboardState().isNoteOn(1, 41), "External DAW note must be visually active");

                    // Also press GUI note 36, then trigger focusLost & octave step
                    proc.getKeyboardState().noteOn(1, 36, 0.85f);
                    ed->focusLost(juce::Component::focusChangedDirectly);
                    ed->setCurrentOctaveSafe(3);

                    for (int b = 0; b < 10; ++b)
                    {
                        buf.clear();
                        proc.processBlock(buf, dawMidi);
                    }

                    expect(!proc.getKeyboardState().isNoteOn(1, 36), "GUI note 36 must be cleared on focusLost/octave change");
                    expectEquals(proc.getEnvelopeStateInt(), 2); // DAW note 41 must stay in Sustain (not Release)
                    expect(proc.getKeyboardState().isNoteOn(1, 41),
                           "External DAW MIDI note 41 must stay lit after GUI focusLost / octave shift");

                    // Simultaneous DAW MIDI (41) + GUI/Mouse (45) + QWERTY (48) legato + editor close/reopen re-sync
                    proc.getKeyboardState().noteOn(1, 45, 0.90f);
                    buf.clear();
                    proc.processBlock(buf, dawMidi);
                    expectEquals(proc.getEnvelopeStateInt(), 2);

                    // Close & reopen editor while DAW note 41 stays held
                    baseEd.reset();
                    std::unique_ptr<juce::AudioProcessorEditor> reopenedEd(proc.createEditor());
                    expect(reopenedEd != nullptr);
                    buf.clear();
                    proc.processBlock(buf, dawMidi);
                    proc.getKeyboardState().noteOff(1, 45, 0.0f);
                    dawMidi.addEvent(juce::MidiMessage::noteOff(1, 41), 0);
                    setParamPlainValue(proc.apvts, "release", 15.0f);
                    for (int b = 0; b < 30; ++b)
                    {
                        buf.clear();
                        proc.processBlock(buf, dawMidi);
                        dawMidi.clear();
                    }
                    expectEquals(proc.getEnvelopeStateInt(), 0);
                }
            }
        }

        beginTest("D.1C.1 Monophonic LIFO semantics, repeated NoteOn, overlapping velocities & queued preset swaps");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 48000.0, 256);
            proc.prepareToPlay(48000.0, 256);
            setParamPlainValue(proc.apvts, "attack", 1.0f);
            setParamPlainValue(proc.apvts, "release", 20.0f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 0.0f);
            setParamPlainValue(proc.apvts, "polderPunch", 0.0f);

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer midi;

            // 1. Overlapping notes with different velocities + LIFO stack return on release:
            // Note 36 (vel 0.25) -> Note 48 (vel 0.90) -> release Note 48 -> returns to 36 (Sustain, NOT Idle/Release)
            proc.getKeyboardState().noteOn(1, 36, 0.25f);
            buf.clear();
            proc.processBlock(buf, midi);
            const float firstVelGain = proc.getCurrentVelocityGain();
            expect(firstVelGain > 0.15f && firstVelGain < 0.35f, "Initial note velocity ~0.25 should apply");

            proc.getKeyboardState().noteOn(1, 48, 0.90f);
            // Trigger preset change WHILE queued NoteOn(48) waits for processBlock
            proc.setCurrentProgram(1);
            setParamPlainValue(proc.apvts, "attack", 1.0f);
            setParamPlainValue(proc.apvts, "release", 20.0f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 0.0f);
            buf.clear();
            proc.processBlock(buf, midi);
            expectEquals(proc.getEnvelopeStateInt(), 2); // Sustain (legato, no re-attack click)
            expect(proc.getCurrentVelocityGain() > 0.70f, "Overlapping NoteOn(48, 0.90) updates velocity gain");

            // Repeated NoteOn of SAME held pitch (48, vel 0.45) must not duplicate stack entries
            proc.getKeyboardState().noteOn(1, 48, 0.45f);
            buf.clear();
            proc.processBlock(buf, midi);
            expectEquals(proc.getEnvelopeStateInt(), 2);

            // Release 48 -> must return to held 36 cleanly (still Sustain)
            proc.getKeyboardState().noteOff(1, 48, 0.0f);
            buf.clear();
            proc.processBlock(buf, midi);
            expectEquals(proc.getEnvelopeStateInt(), 2);

            // Release 36 -> now must enter Release -> Idle
            proc.getKeyboardState().noteOff(1, 36, 0.0f);
            for (int b = 0; b < 30; ++b)
            {
                buf.clear();
                proc.processBlock(buf, midi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 0);

            // 2. Partial queue overflow (~150 queued then 150 more with NoteOn followed by NoteOff inside overflow)
            for (int i = 0; i < 75; ++i)
            {
                proc.getKeyboardState().noteOn(1, 36 + (i % 12), 0.7f);
                proc.getKeyboardState().noteOff(1, 36 + (i % 12), 0.0f);
            }
            // Now 150 events queued (partially full). Burst 150 more: NoteOn(55) followed by NoteOff(55) across overflow boundary
            for (int i = 0; i < 74; ++i)
            {
                proc.getKeyboardState().noteOn(1, 36 + (i % 12), 0.8f);
                proc.getKeyboardState().noteOff(1, 36 + (i % 12), 0.0f);
            }
            proc.getKeyboardState().noteOn(1, 55, 0.95f);  // drops into overflow
            proc.getKeyboardState().noteOff(1, 55, 0.0f);  // drops into overflow too
            for (int b = 0; b < 30; ++b)
            {
                buf.clear();
                proc.processBlock(buf, midi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 0);
        }
    }
};

template <typename ComponentType>
ComponentType* findChildOfType(juce::Component& root)
{
    if (auto* match = dynamic_cast<ComponentType*>(&root))
        return match;

    for (auto* child : root.getChildren())
    {
        if (auto* found = findChildOfType<ComponentType>(*child))
            return found;
    }
    return nullptr;
}

juce::Image renderAtScale(juce::Component& component, float scaleFactor)
{
    const int imgW = juce::jmax(1, juce::roundToInt(static_cast<float>(component.getWidth()) * scaleFactor));
    const int imgH = juce::jmax(1, juce::roundToInt(static_cast<float>(component.getHeight()) * scaleFactor));
    juce::Image img(juce::Image::ARGB, imgW, imgH, true);
    juce::Graphics g(img);
    g.addTransform(juce::AffineTransform::scale(scaleFactor));
    component.paintEntireComponent(g, true);
    return img;
}

double imageDiffRms(const juce::Image& a, const juce::Image& b)
{
    if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
        return 1.0;

    const int w = a.getWidth();
    const int h = a.getHeight();
    double sumSq = 0.0;
    const double count = static_cast<double>(w) * static_cast<double>(h) * 3.0;

    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            const auto ca = a.getPixelAt(x, y);
            const auto cb = b.getPixelAt(x, y);
            const double dr = static_cast<double>(ca.getRed() - cb.getRed()) / 255.0;
            const double dg = static_cast<double>(ca.getGreen() - cb.getGreen()) / 255.0;
            const double db = static_cast<double>(ca.getBlue() - cb.getBlue()) / 255.0;
            sumSq += dr * dr + dg * dg + db * db;
        }
    }
    return std::sqrt(sumSq / count);
}

class EngineeredDepthTests final : public juce::UnitTest
{
public:
    EngineeredDepthTests() : juce::UnitTest("ALLY1300 Engineered Depth & DIEPTE Ring", "AllyBass") {}

    void runTest() override
    {
        beginTest("EngineeredDepthTheme tokens, state palettes, and vector icons are valid");
        {
            const auto cyanNormal = AllyGUI::EngineeredDepthTheme::resolveAccent(
                AllyGUI::EngineeredDepthTheme::AccentTheme::Cyan, { false, false, true, false, true });
            const auto cyanDrag = AllyGUI::EngineeredDepthTheme::resolveAccent(
                AllyGUI::EngineeredDepthTheme::AccentTheme::Cyan, { true, true, true, true, true });
            const auto cyanDisabled = AllyGUI::EngineeredDepthTheme::resolveAccent(
                AllyGUI::EngineeredDepthTheme::AccentTheme::Cyan, { false, false, true, false, false });
            const auto redNormal = AllyGUI::EngineeredDepthTheme::resolveAccent(
                AllyGUI::EngineeredDepthTheme::AccentTheme::Red, { false, false, true, false, true });

            expect(cyanNormal.primary != redNormal.primary, "Cyan and Red themes must differ");
            expect(cyanDrag.primary.getBrightness() >= cyanNormal.primary.getBrightness(),
                   "Dragging palette must stay at least as luminous as normal state");
            expect(cyanDisabled.glow.getAlpha() == 0, "Disabled palette must suppress active glow");
            expect(AllyGUI::EngineeredDepthTheme::getUiFontFamily().isNotEmpty(),
                   "UI font family must resolve");
            expect(AllyGUI::EngineeredDepthTheme::getDataFontFamily().isNotEmpty(),
                   "Data font family must resolve");

            const AllyGUI::EngineeredDepthIcons::IconId icons[] = {
                AllyGUI::EngineeredDepthIcons::IconId::DiepteWater,
                AllyGUI::EngineeredDepthIcons::IconId::PolderPunch,
                AllyGUI::EngineeredDepthIcons::IconId::HavenDirt,
                AllyGUI::EngineeredDepthIcons::IconId::WaterwijkGlide,
                AllyGUI::EngineeredDepthIcons::IconId::OutputBars,
                AllyGUI::EngineeredDepthIcons::IconId::StadsdeelDsp,
                AllyGUI::EngineeredDepthIcons::IconId::ChevronDown
            };
            for (const auto icon : icons)
            {
                juce::Rectangle<float> viewBox;
                const auto p = AllyGUI::EngineeredDepthIcons::buildStrokePath(icon, viewBox);
                expect(!p.isEmpty(), "Built vector icon path must be non-empty");
                expect(viewBox.getWidth() > 0.0f && viewBox.getHeight() > 0.0f,
                       "Icon viewBox must have positive dimensions");
            }
        }

        beginTest("Editor replaces all 5 primary knobs via PrimaryKnobsPanel with proper defaults, formatting, automation, drag, keyboard, and presets");
        {
            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
            expect(editor != nullptr, "Editor must be created");

            if (editor != nullptr)
            {
                auto* panel = findChildOfType<AllyGUI::PrimaryKnobsPanel>(*editor);
                expect(panel != nullptr, "PrimaryKnobsPanel must exist inside editor");

                // Compact Mini Depth Rings Attack & Release remain wired outside PrimaryKnobsPanel
                expect(findSliderByName(*editor, "attack") != nullptr, "Attack Mini Depth Ring must exist");
                expect(findSliderByName(*editor, "release") != nullptr, "Release Mini Depth Ring must exist");

                if (panel != nullptr)
                {
                    struct ExpectedPrimary
                    {
                        const char* paramId;
                        float defaultPlain;
                        const char* defaultText;
                        float altPlain;
                        const char* altText;
                    };

                    const ExpectedPrimary expected[5] = {
                        { "diepte",         0.50f, "0.50 NAP",  0.87f,   "0.87 NAP"  },
                        { "polderPunch",    0.00f, "0 %",       0.64f,   "64 %"      },
                        { "havenDirt",      0.15f, "0.15 DIRT", 0.72f,   "0.72 DIRT" },
                        { "waterwijkGlide", 40.0f, "40.0 ms",   185.5f,  "185.5 ms"  },
                        { "masterVolume",   0.00f, "0.00 dB",   -12.40f, "-12.40 dB" }
                    };

                    const auto source = juce::Desktop::getInstance().getMainMouseSource();
                    const auto now = juce::Time::getCurrentTime();

                    for (int i = 0; i < AllyGUI::PrimaryKnobsPanel::knobCount; ++i)
                    {
                        auto* knob = panel->getKnob(i);
                        expect(knob != nullptr, "Every primary slot must own a DepthRingKnob");
                        if (knob == nullptr)
                            continue;

                        const auto& spec = expected[i];
                        auto& slider = knob->getSlider();
                        expectEquals(slider.getName(), juce::String(spec.paramId));

                        // Exact 270-degree sweep (-135 deg .. +135 deg)
                        const auto rotary = slider.getRotaryParameters();
                        expectWithinAbsoluteError(rotary.startAngleRadians, juce::MathConstants<float>::pi * 1.25f, 1e-4f);
                        expectWithinAbsoluteError(rotary.endAngleRadians, juce::MathConstants<float>::pi * 2.75f, 1e-4f);

                        // Individual default formatting & double-click metadata
                        expectEquals(knob->getFormattedValueText(), juce::String(spec.defaultText));
                        expect(slider.isDoubleClickReturnEnabled(), "Double-click reset must be enabled");
                        expectWithinAbsoluteError(static_cast<float>(slider.getDoubleClickReturnValue()),
                                                  spec.defaultPlain, 1e-2f);

                        // OUTPUT specifics: [-24, +6] dB range, never '%'
                        if (juce::String(spec.paramId) == "masterVolume")
                        {
                            expectWithinAbsoluteError(static_cast<float>(slider.getMinimum()), -24.0f, 1e-4f);
                            expectWithinAbsoluteError(static_cast<float>(slider.getMaximum()), 6.0f, 1e-4f);

                            setParamPlainValue(proc.apvts, "masterVolume", -24.0f);
                            expectEquals(knob->getFormattedValueText(), juce::String("-24.00 dB"));
                            expect(!knob->getFormattedValueText().contains("%"), "OUTPUT must never show %");

                            setParamPlainValue(proc.apvts, "masterVolume", 6.0f);
                            expectEquals(knob->getFormattedValueText(), juce::String("+6.00 dB"));
                            expect(!knob->getFormattedValueText().contains("%"), "OUTPUT positive gain must show dB");
                        }

                        // Host automation -> DepthRingKnob
                        setParamPlainValue(proc.apvts, spec.paramId, spec.altPlain);
                        expectWithinAbsoluteError(static_cast<float>(slider.getValue()), spec.altPlain, 0.51f);
                        expectEquals(knob->getFormattedValueText(), juce::String(spec.altText));

                        // DepthRingKnob -> APVTS
                        slider.setValue(spec.defaultPlain, juce::sendNotificationSync);
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, spec.paramId), spec.defaultPlain, 0.51f);

                        // Move away via mouse drag, then verify individual double-click reset returns to each parameter's own default
                        const auto centre = slider.getLocalBounds().getCentre().toFloat();
                        const float dragDirY = (spec.defaultPlain < 0.01f && slider.getMinimum() >= 0.0) ? -60.0f : 60.0f;

                        // If at 0% (POLDER PUNCH), drag up (+); otherwise set away and verify drag + double-click reset
                        setParamPlainValue(proc.apvts, spec.paramId, spec.altPlain);
                        const float beforeDrag = getParamPlainValue(proc.apvts, spec.paramId);

                        juce::MouseEvent downEv(source, centre, juce::ModifierKeys::leftButtonModifier,
                                                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                &slider, &slider, now, centre, now, 1, false);
                        slider.mouseDown(downEv);

                        const auto dragPos = centre.translated(0.0f, dragDirY > 0.0f ? -45.0f : dragDirY);
                        juce::MouseEvent dragEv(source, dragPos, juce::ModifierKeys::leftButtonModifier,
                                                1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                                &slider, &slider, now + juce::RelativeTime::milliseconds(20),
                                                centre, now, 1, true);
                        slider.mouseDrag(dragEv);

                        juce::MouseEvent upEv(source, dragPos, juce::ModifierKeys(),
                                              0.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                              &slider, &slider, now + juce::RelativeTime::milliseconds(30),
                                              centre, now, 1, true);
                        slider.mouseUp(upEv);

                        const float afterDrag = getParamPlainValue(proc.apvts, spec.paramId);
                        expect(std::abs(afterDrag - beforeDrag) > 1e-4f
                                   || std::abs(spec.altPlain - static_cast<float>(slider.getMaximum())) < 1e-3f,
                               "Mouse drag must adjust parameter");

                        // Arrow key adjustment on slider
                        const float beforeKey = getParamPlainValue(proc.apvts, spec.paramId);
                        slider.keyPressed(juce::KeyPress(juce::KeyPress::downKey));
                        expect(getParamPlainValue(proc.apvts, spec.paramId) <= beforeKey,
                               "Down arrow key must step parameter downward or hold at minimum");

                        // Double click must restore each knob's own APVTS default (not 0.50!)
                        juce::MouseEvent dblEv(source, centre, juce::ModifierKeys(),
                                               1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                                               &slider, &slider, now + juce::RelativeTime::milliseconds(80),
                                               centre, now + juce::RelativeTime::milliseconds(80), 2, false);
                        slider.mouseDoubleClick(dblEv);
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, spec.paramId), spec.defaultPlain, 1e-2f);
                        expectEquals(knob->getFormattedValueText(), juce::String(spec.defaultText));
                    }

                    // QWERTY Z/X octave shortcut on editor stays intact
                    expect(editor->keyPressed(juce::KeyPress('x')), "X octave up key must be handled");
                    expect(editor->keyPressed(juce::KeyPress('z')), "Z octave down key must return cleanly");

                    // Factory presets update all 5 primary knobs properly
                    const auto& presets = AllyPresets::getFactoryPresets();
                    for (size_t i = 0; i < presets.size(); ++i)
                    {
                        AllyPresets::applyPreset(proc.apvts, static_cast<int>(i));
                        expectWithinAbsoluteError(static_cast<float>(panel->getKnob(0)->getSlider().getValue()),
                                                  presets[i].diepte, 1e-2f);
                        expectWithinAbsoluteError(static_cast<float>(panel->getKnob(1)->getSlider().getValue()),
                                                  presets[i].polderPunch, 1e-2f);
                        expectWithinAbsoluteError(static_cast<float>(panel->getKnob(2)->getSlider().getValue()),
                                                  presets[i].havenDirt, 1e-2f);
                        expectWithinAbsoluteError(static_cast<float>(panel->getKnob(3)->getSlider().getValue()),
                                                  presets[i].waterwijkGlide, 0.51f);
                        expectEquals(panel->getKnob(0)->getFormattedValueText(),
                                     juce::String(presets[i].diepte, 2) + " NAP");
                        expectEquals(panel->getKnob(1)->getFormattedValueText(),
                                     juce::String(juce::roundToInt(presets[i].polderPunch * 100.0f)) + " %");
                        expectEquals(panel->getKnob(2)->getFormattedValueText(),
                                     juce::String(presets[i].havenDirt, 2) + " DIRT");
                        expectEquals(panel->getKnob(3)->getFormattedValueText(),
                                     juce::String(presets[i].waterwijkGlide, 1) + " ms");
                    }
                }
            }
        }

        beginTest("PrimaryKnobsPanel aligns all 5 rings symmetrically without clipping across sizes, states, and HiDPI scales");
        {
            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
            expect(editor != nullptr, "Editor must be created");

            if (editor != nullptr)
            {
                auto* panel = findChildOfType<AllyGUI::PrimaryKnobsPanel>(*editor);
                expect(panel != nullptr, "PrimaryKnobsPanel must exist inside editor");

                if (panel != nullptr)
                {
                    const juce::Point<int> sizes[] = { { 680, 500 }, { 780, 580 }, { 1200, 850 } };
                    for (const auto& sz : sizes)
                    {
                        editor->setSize(sz.x, sz.y);

                        const auto chassis = panel->getChassisBounds();
                        expect(!chassis.isEmpty(), "Primary chassis bounds must be non-empty");
                        expect(panel->getLocalBounds().toFloat().contains(chassis),
                               "Chassis bounds must stay inside PrimaryKnobsPanel");

                        auto* k0 = panel->getKnob(0);
                        expect(k0 != nullptr);
                        if (k0 == nullptr)
                            continue;

                        const int refW = k0->getWidth();
                        const int refH = k0->getHeight();
                        const int refY = k0->getY();
                        const float refDialW = k0->getDialBounds().getWidth();
                        const float refTitleY = k0->getTitleBounds().getY();
                        const float refReadoutY = k0->getReadoutBounds().getY();
                        int prevCentreX = k0->getBounds().getCentreX();
                        int spacing = 0;

                        for (int i = 0; i < AllyGUI::PrimaryKnobsPanel::knobCount; ++i)
                        {
                            auto* k = panel->getKnob(i);
                            expect(k != nullptr);
                            if (k == nullptr)
                                continue;

                            const auto local = k->getLocalBounds().toFloat();
                            expect(chassis.contains(k->getBounds().toFloat()),
                                   "Knob bounds must fit inside chamfered chassis");
                            expect(local.contains(k->getTitleBounds()),
                                   "Title bounds must fit inside knob without clipping");
                            expect(local.contains(k->getPillBounds()),
                                   "Pill bounds must fit inside knob");
                            expect(local.contains(k->getDialBounds()),
                                   "Dial bounds must fit inside knob");
                            expect(local.contains(k->getReadoutBounds()),
                                   "Readout bounds must fit inside knob without clipping");
                            expect(k->getTitleBounds().getBottom() <= k->getDialBounds().getY(),
                                   "Title must sit cleanly above dial");
                            expect(k->getDialBounds().getBottom() <= k->getReadoutBounds().getY(),
                                   "Readout must sit cleanly below dial");

                            // Uniform size & horizontal baseline check across all 5 rings
                            expectEquals(k->getWidth(), refW);
                            expectEquals(k->getHeight(), refH);
                            expectEquals(k->getY(), refY);
                            expectWithinAbsoluteError(k->getDialBounds().getWidth(), refDialW, 0.1f);
                            expectWithinAbsoluteError(k->getTitleBounds().getY(), refTitleY, 0.1f);
                            expectWithinAbsoluteError(k->getReadoutBounds().getY(), refReadoutY, 0.1f);

                            if (i > 0)
                            {
                                const int dx = k->getBounds().getCentreX() - prevCentreX;
                                if (i == 1)
                                    spacing = dx;
                                expect(std::abs(dx - spacing) <= 1,
                                       "Horizontal spacing between primary knobs must remain equal");
                                expect(panel->getKnob(i - 1)->getRight() <= k->getX(),
                                       "Adjacent DepthRingKnob components must not overlap");
                                prevCentreX = k->getBounds().getCentreX();
                            }
                        }
                    }

                    editor->setSize(780, 580);

                    // Panel visual variation across parameter extremes + disabled state on Red Haven Dirt
                    AllyPresets::applyPreset(proc.apvts, 0);
                    setParamPlainValue(proc.apvts, "diepte", 0.0f);
                    setParamPlainValue(proc.apvts, "polderPunch", 0.0f);
                    setParamPlainValue(proc.apvts, "havenDirt", 0.0f);
                    setParamPlainValue(proc.apvts, "waterwijkGlide", 0.0f);
                    setParamPlainValue(proc.apvts, "masterVolume", -24.0f);
                    const auto panelMin = renderAtScale(*panel, 1.0f);

                    setParamPlainValue(proc.apvts, "diepte", 1.0f);
                    setParamPlainValue(proc.apvts, "polderPunch", 1.0f);
                    setParamPlainValue(proc.apvts, "havenDirt", 1.0f);
                    setParamPlainValue(proc.apvts, "waterwijkGlide", 500.0f);
                    setParamPlainValue(proc.apvts, "masterVolume", 6.0f);
                    const auto panelMax = renderAtScale(*panel, 1.0f);

                    expect(imageDiffRms(panelMin, panelMax) > 0.02,
                           "Min vs Max 5-knob panel renders must differ clearly across cyan & red arcs");

                    auto* havenKnob = panel->getKnob(2);
                    setParamPlainValue(proc.apvts, "havenDirt", 0.50f);
                    const auto havenEnabled = renderAtScale(*havenKnob, 1.0f);
                    havenKnob->setEnabled(false);
                    const auto havenDisabled = renderAtScale(*havenKnob, 1.0f);
                    havenKnob->setEnabled(true);
                    expect(imageDiffRms(havenEnabled, havenDisabled) > 0.015,
                           "Disabled HAVEN DIRT Depth Ring must differ clearly from enabled state");

                    // Multi-scale HiDPI rendering verification on whole PrimaryKnobsPanel
                    const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
                    for (const float scale : scales)
                    {
                        const auto scaled = renderAtScale(*panel, scale);
                        expect(scaled.isValid() && scaled.getWidth() > 0 && scaled.getHeight() > 0,
                               "HiDPI scaled PrimaryKnobsPanel render must succeed");
                        const auto emptyCheck = juce::Image(juce::Image::ARGB, scaled.getWidth(), scaled.getHeight(), true);
                        expect(imageDiffRms(scaled, emptyCheck) > 0.02,
                               "HiDPI scaled PrimaryKnobsPanel render must draw non-empty content");
                    }
                }
            }
        }

        beginTest("SecondaryDspPanel hosts STADSDEEL DSP, ATTACK/RELEASE Mini Depth Rings, 036 OVERDRIVE, and 1300 MONO LOCK with full APVTS, DSP, state, and responsive layout");
        {
            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
            expect(editor != nullptr, "Editor must be created");

            if (editor != nullptr)
            {
                auto* primaryPanel   = findChildOfType<AllyGUI::PrimaryKnobsPanel>(*editor);
                auto* secondaryPanel = findChildOfType<AllyGUI::SecondaryDspPanel>(*editor);
                expect(primaryPanel != nullptr, "PrimaryKnobsPanel must exist");
                expect(secondaryPanel != nullptr, "SecondaryDspPanel must exist inside editor");
                expect(findChildOfType<AllyGUI::HeavyKnob>(*editor) == nullptr,
                       "All legacy HeavyKnob widgets must be replaced by DepthRingKnob");
                expect(findChildOfType<AllyGUI::BrutalistToggle>(*editor) == nullptr,
                       "Legacy BrutalistToggle widgets must be replaced by EngineeredDepthToggle");

                if (primaryPanel != nullptr && secondaryPanel != nullptr)
                {
                    auto* stadsdeelCtrl = secondaryPanel->getStadsdeelControl();
                    auto* attackKnob    = secondaryPanel->getAttackKnob();
                    auto* releaseKnob   = secondaryPanel->getReleaseKnob();
                    auto* overdriveTog  = secondaryPanel->getOverdriveToggle();
                    auto* monoTog       = secondaryPanel->getMonoLockToggle();

                    expect(stadsdeelCtrl != nullptr, "STADSDEEL DSP selector must exist");
                    expect(attackKnob != nullptr, "ATTACK Mini Depth Ring must exist");
                    expect(releaseKnob != nullptr, "RELEASE Mini Depth Ring must exist");
                    expect(overdriveTog != nullptr, "036 OVERDRIVE toggle must exist");
                    expect(monoTog != nullptr, "1300 MONO LOCK toggle must exist");

                    if (stadsdeelCtrl != nullptr && attackKnob != nullptr && releaseKnob != nullptr
                        && overdriveTog != nullptr && monoTog != nullptr)
                    {
                        // 1. Component names & STADSDEEL modes preserved
                        auto& combo = stadsdeelCtrl->getComboBox();
                        expectEquals(combo.getName(), juce::String("stadsdeel"));
                        expectEquals(combo.getNumItems(), 4);
                        expectEquals(combo.getItemText(0), juce::String("Haven (1350) - Tape/Tube"));
                        expectEquals(combo.getItemText(1), juce::String("Centrum (1315) - Hard Clip"));
                        expectEquals(combo.getItemText(2), juce::String("Poort (1363) - Clean Exciter"));
                        expectEquals(combo.getItemText(3), juce::String("Buiten (1330) - Foldback"));
                        expectEquals(combo.getSelectedId(), 2); // Default index 1 (Centrum 1315)

                        // Bidirectional sync: ComboBox <-> APVTS
                        combo.setSelectedId(4, juce::sendNotificationSync);
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "stadsdeel"), 3.0f, 1e-4f);
                        setParamPlainValue(proc.apvts, "stadsdeel", 0.0f);
                        expectEquals(combo.getSelectedId(), 1);
                        setParamPlainValue(proc.apvts, "stadsdeel", 1.0f);
                        expectEquals(combo.getSelectedId(), 2);

                        // 2. Mini Depth Rings: ATTACK & RELEASE own ranges, formatting, drag, double-click reset
                        expectEquals(attackKnob->getSlider().getName(), juce::String("attack"));
                        expectEquals(releaseKnob->getSlider().getName(), juce::String("release"));
                        expect(attackKnob->getPillBounds().isEmpty(), "Compact Mini Depth Ring must omit top pill");
                        expect(releaseKnob->getPillBounds().isEmpty(), "Compact Mini Depth Ring must omit top pill");

                        expectWithinAbsoluteError(static_cast<float>(attackKnob->getSlider().getMinimum()), 1.0f, 1e-4f);
                        expectWithinAbsoluteError(static_cast<float>(attackKnob->getSlider().getMaximum()), 300.0f, 1e-4f);
                        expectWithinAbsoluteError(static_cast<float>(releaseKnob->getSlider().getMinimum()), 10.0f, 1e-4f);
                        expectWithinAbsoluteError(static_cast<float>(releaseKnob->getSlider().getMaximum()), 3000.0f, 1e-4f);

                        expectEquals(attackKnob->getFormattedValueText(), juce::String("8.0 ms"));
                        expectEquals(releaseKnob->getFormattedValueText(), juce::String("250.0 ms"));
                        expect(!attackKnob->getFormattedValueText().contains("["), "Mini ring readout must omit brackets");
                        expect(!releaseKnob->getFormattedValueText().contains("["), "Mini ring readout must omit brackets");

                        // Automation -> Mini ring -> APVTS & Double-click reset to own defaults
                        setParamPlainValue(proc.apvts, "attack", 42.5f);
                        expectWithinAbsoluteError(static_cast<float>(attackKnob->getSlider().getValue()), 42.5f, 0.15f);
                        expectEquals(attackKnob->getFormattedValueText(), juce::String("42.5 ms"));

                        setParamPlainValue(proc.apvts, "release", 820.0f);
                        expectWithinAbsoluteError(static_cast<float>(releaseKnob->getSlider().getValue()), 820.0f, 1.1f);
                        expectEquals(releaseKnob->getFormattedValueText(), juce::String("820.0 ms"));

                        // Check attack APVTS down to 1 ms still preserves existing DSP behavior without modifying DSP
                        setParamPlainValue(proc.apvts, "attack", 1.0f);
                        expectEquals(attackKnob->getFormattedValueText(), juce::String("1.0 ms"));
                        const auto source = juce::Desktop::getInstance().getMainMouseSource();
                        const auto now = juce::Time::getCurrentTime();
                        const auto atkCentre = attackKnob->getSlider().getLocalBounds().getCentre().toFloat();
                        const auto relCentre = releaseKnob->getSlider().getLocalBounds().getCentre().toFloat();
                        attackKnob->getSlider().mouseDoubleClick(juce::MouseEvent(
                            source, atkCentre, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &attackKnob->getSlider(), &attackKnob->getSlider(), now, atkCentre, now, 2, false));
                        releaseKnob->getSlider().mouseDoubleClick(juce::MouseEvent(
                            source, relCentre, juce::ModifierKeys(), 1.0f, 0.0f, 0.0f, 0.0f, 0.0f,
                            &releaseKnob->getSlider(), &releaseKnob->getSlider(), now, relCentre, now, 2, false));
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "attack"), 8.0f, 1e-2f);
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "release"), 250.0f, 1e-2f);

                        // 3. Hardware LED toggles: 036 OVERDRIVE (red) & 1300 MONO LOCK (cyan)
                        auto& odBtn   = overdriveTog->getButton();
                        auto& monoBtn = monoTog->getButton();
                        expectEquals(odBtn.getName(), juce::String("036_overdrive"));
                        expectEquals(monoBtn.getName(), juce::String("mono_lock"));
                        expect(!odBtn.getToggleState(), "036 OVERDRIVE default must be OFF");
                        expect(monoBtn.getToggleState(), "1300 MONO LOCK default must be ON");

                        // Toggle ON/OFF visual & APVTS checks
                        const auto odOffImg = renderAtScale(*overdriveTog, 1.0f);
                        odBtn.setToggleState(true, juce::sendNotificationSync);
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "overdrive036"), 1.0f, 1e-4f);
                        const auto odOnImg = renderAtScale(*overdriveTog, 1.0f);
                        expect(imageDiffRms(odOffImg, odOnImg) > 0.025,
                               "036 OVERDRIVE ON vs OFF visuals must differ clearly");

                        overdriveTog->setEnabled(false);
                        const auto odDisabledImg = renderAtScale(*overdriveTog, 1.0f);
                        overdriveTog->setEnabled(true);
                        expect(imageDiffRms(odOnImg, odDisabledImg) > 0.02,
                               "036 OVERDRIVE Disabled vs Enabled visuals must differ clearly");

                        const auto monoOnImg = renderAtScale(*monoTog, 1.0f);
                        monoBtn.setToggleState(false, juce::sendNotificationSync);
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "monoLock1300"), 0.0f, 1e-4f);
                        const auto monoOffImg = renderAtScale(*monoTog, 1.0f);
                        expect(imageDiffRms(monoOnImg, monoOffImg) > 0.025,
                               "1300 MONO LOCK ON vs OFF visuals must differ clearly");
                        monoBtn.setToggleState(true, juce::sendNotificationSync);
                        expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "monoLock1300"), 1.0f, 1e-4f);

                        // Color channel verification: Red LED aura/ring on 036 OVERDRIVE ON vs Cyan LED aura/ring on 1300 MONO LOCK ON
                        const auto odLedBox   = overdriveTog->getLedBounds().toNearestInt();
                        const auto monoLedBox = monoTog->getLedBounds().toNearestInt();
                        double odRedSum = 0.0, odBlueSum = 0.0;
                        for (int y = odLedBox.getY(); y < odLedBox.getBottom(); ++y)
                        {
                            for (int x = odLedBox.getX(); x < odLedBox.getRight(); ++x)
                            {
                                const auto px = odOnImg.getPixelAt(x, y);
                                odRedSum += px.getRed();
                                odBlueSum += px.getBlue();
                            }
                        }
                        double monoRedSum = 0.0, monoBlueSum = 0.0;
                        for (int y = monoLedBox.getY(); y < monoLedBox.getBottom(); ++y)
                        {
                            for (int x = monoLedBox.getX(); x < monoLedBox.getRight(); ++x)
                            {
                                const auto px = monoOnImg.getPixelAt(x, y);
                                monoRedSum += px.getRed();
                                monoBlueSum += px.getBlue();
                            }
                        }
                        expect(odRedSum > odBlueSum * 1.4,
                               "036 OVERDRIVE LED area must emphasize crimson red palette");
                        expect(monoBlueSum > monoRedSum * 1.4,
                               "1300 MONO LOCK LED area must emphasize cyan palette");

                        // 4. Factory presets update secondary DSP controls properly
                        const auto& presets = AllyPresets::getFactoryPresets();
                        for (size_t i = 0; i < presets.size(); ++i)
                        {
                            AllyPresets::applyPreset(proc.apvts, static_cast<int>(i));
                            expectEquals(combo.getSelectedId(), presets[i].stadsdeelIndex + 1);
                            expectEquals(odBtn.getToggleState() ? 1 : 0, presets[i].overdrive036 ? 1 : 0);
                            expectEquals(monoBtn.getToggleState() ? 1 : 0, presets[i].monoLock1300 ? 1 : 0);
                            expectWithinAbsoluteError(static_cast<float>(releaseKnob->getSlider().getValue()),
                                                      presets[i].releaseMs, 1.01f);
                        }

                        // 5. Responsive layout & no-overlap/no-clipping across 680x500, 780x580, 1200x850
                        const juce::Point<int> sizes[] = { { 680, 500 }, { 780, 580 }, { 1200, 850 } };
                        for (const auto& sz : sizes)
                        {
                            editor->setSize(sz.x, sz.y);

                            expect(primaryPanel->getBottom() <= secondaryPanel->getY(),
                                   "SecondaryDspPanel must sit cleanly below PrimaryKnobsPanel");

                            const auto chassis = secondaryPanel->getChassisBounds();
                            expect(!chassis.isEmpty(), "Secondary chassis bounds must be non-empty");
                            expect(secondaryPanel->getLocalBounds().toFloat().contains(chassis),
                                   "Secondary chassis must remain inside SecondaryDspPanel");

                            const juce::Component* ordered[5] = {
                                stadsdeelCtrl, attackKnob, releaseKnob, overdriveTog, monoTog
                            };
                            for (int c = 0; c < 5; ++c)
                            {
                                expect(chassis.contains(ordered[c]->getBounds().toFloat()),
                                       "Secondary control must fit inside chamfered secondary chassis");
                                if (c > 0)
                                    expect(ordered[c - 1]->getRight() <= ordered[c]->getX(),
                                           "Secondary controls must sit in left-to-right order without overlap");
                            }

                            expect(stadsdeelCtrl->getLocalBounds().toFloat().contains(stadsdeelCtrl->getIconBounds()),
                                   "Stadsdeel icon must not clip");
                            expect(stadsdeelCtrl->getLocalBounds().toFloat().contains(stadsdeelCtrl->getLabelBounds()),
                                   "Stadsdeel label must not clip");
                            expect(stadsdeelCtrl->getLocalBounds().contains(stadsdeelCtrl->getComboBounds()),
                                   "Stadsdeel ComboBox must not clip");

                            for (const auto* mini : { attackKnob, releaseKnob })
                            {
                                const auto ml = mini->getLocalBounds().toFloat();
                                expect(ml.contains(mini->getTitleBounds()), "Mini title must not clip");
                                expect(ml.contains(mini->getDialBounds()), "Mini dial must not clip");
                                expect(ml.contains(mini->getReadoutBounds()), "Mini readout must not clip");
                            }
                        }

                        editor->setSize(780, 580);
                        // 6. HiDPI render validation on SecondaryDspPanel
                        const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
                        for (const float scale : scales)
                        {
                            const auto scaled = renderAtScale(*secondaryPanel, scale);
                            expect(scaled.isValid() && scaled.getWidth() > 0 && scaled.getHeight() > 0,
                                   "HiDPI SecondaryDspPanel render must succeed");
                            const auto emptyCheck = juce::Image(juce::Image::ARGB, scaled.getWidth(), scaled.getHeight(), true);
                            expect(imageDiffRms(scaled, emptyCheck) > 0.02,
                                   "HiDPI SecondaryDspPanel render must draw non-empty content");
                        }
                    }
                }
            }
        }
    }
};

static ParameterAndStateTests parameterAndStateTests;
static MidiGlideAndAudioTests midiGlideAndAudioTests;
static PolderPunchTests polderPunchTests;
static MonoLockDspTests monoLockDspTests;
static AttackEnvelopeD1BTests attackEnvelopeD1BTests;
static ConcurrencyRealtimeSafetyD1CTests concurrencyRealtimeSafetyD1CTests;
static EngineeredDepthTests engineeredDepthTests;

class HeaderBrandingAndPresetTests final : public juce::UnitTest
{
public:
    HeaderBrandingAndPresetTests() : juce::UnitTest("Header Branding & Preset Management (C.4A)", "AllyBass") {}

    void runTest() override
    {
        beginTest("All 14 factory presets and circular wrap navigation (14 -> 1 and 1 -> 14)");
        {
            AllyBassAudioProcessor proc;
            expectEquals(proc.getNumPrograms(), 14);
            expectEquals(proc.getPresetStateInfo().displayName, juce::String("Init"));

            // Step forward through Init -> 01..14 -> wraps back to 01
            proc.selectRelativeFactoryPreset(+1);
            expectEquals(proc.getCurrentProgram(), 0);
            expectEquals(proc.getPresetStateInfo().displayName, juce::String("01_Kasteel_Drill_Slide"));

            for (int step = 0; step < 13; ++step)
                proc.selectRelativeFactoryPreset(+1);

            expectEquals(proc.getCurrentProgram(), 13);
            expectEquals(proc.getPresetStateInfo().displayName, juce::String("14_42GRAM_Club_Banger"));

            proc.selectRelativeFactoryPreset(+1);
            expectEquals(proc.getCurrentProgram(), 0);
            expectEquals(proc.getPresetStateInfo().displayName, juce::String("01_Kasteel_Drill_Slide"));

            // Step backward 01 -> wraps to 14
            proc.selectRelativeFactoryPreset(-1);
            expectEquals(proc.getCurrentProgram(), 13);
            expectEquals(proc.getPresetStateInfo().displayName, juce::String("14_42GRAM_Club_Banger"));

            // Same circular behavior through UI HeaderPanel Prev/Next buttons
            std::unique_ptr<juce::AudioProcessorEditor> baseEditor(proc.createEditor());
            auto* editor = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEditor.get());
            expect(editor != nullptr);
            if (editor != nullptr && editor->getHeaderPanel() != nullptr)
            {
                auto* hdr = editor->getHeaderPanel();
                expect(hdr->getNextButton().onClick != nullptr, "Next button must wire circular preset handler");
                expect(hdr->getPrevButton().onClick != nullptr, "Prev button must wire circular preset handler");

                hdr->getNextButton().onClick();
                expectEquals(proc.getCurrentProgram(), 0);
                expectEquals(hdr->getDisplayedPresetText(), juce::String("01_Kasteel_Drill_Slide"));

                hdr->getPrevButton().onClick();
                expectEquals(proc.getCurrentProgram(), 13);
                expectEquals(hdr->getDisplayedPresetText(), juce::String("14_42GRAM_Club_Banger"));
            }
        }

        beginTest("Modified indicator on manual parameter edit keeps factory preset immutable");
        {
            const auto& presets = AllyPresets::getFactoryPresets();
            const juce::String preset4Name = presets[3].name;
            const float originalPreset4Diepte = presets[3].diepte;

            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> baseEditor(proc.createEditor());
            auto* editor = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEditor.get());
            expect(editor != nullptr && editor->getHeaderPanel() != nullptr);

            proc.setCurrentProgram(3);
            if (editor != nullptr && editor->getHeaderPanel() != nullptr)
            {
                auto* hdr = editor->getHeaderPanel();
                hdr->refreshFromProcessor();
                expectEquals(proc.getCurrentProgram(), 3);
                expect(!proc.getPresetStateInfo().modified, "Newly loaded preset must not be modified");
                expectEquals(hdr->getDisplayedPresetText(), preset4Name);

                std::vector<AllyPresets::UserPresetEntry> userEntries;
                auto menu = hdr->buildPresetPopupMenu(userEntries);
                expect(menu != nullptr && menu->getNumItems() >= 14, "Popup menu must include all 14 factory presets");

                setParamPlainValue(proc.apvts, "diepte", 0.93f);
                expect(proc.getPresetStateInfo().modified, "Parameter edit must flag preset modified");
                expectEquals(hdr->getDisplayedPresetText(), preset4Name + " *");
                expectWithinAbsoluteError(AllyPresets::getFactoryPresets()[3].diepte, originalPreset4Diepte, 1e-6f);
            }
        }

        beginTest("User .allypreset save, overwrite protection, corrupt rejection, and reload");
        {
            const auto tempDir = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                     .getChildFile("ally1300_c4a_test_" + juce::String(juce::Time::currentTimeMillis()));
            tempDir.createDirectory();

            struct TempDirGuard
            {
                juce::File dir;
                ~TempDirGuard() { dir.deleteRecursively(); }
            } cleanup { tempDir };

            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> baseEditor(proc.createEditor());
            auto* editor = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEditor.get());
            expect(editor != nullptr && editor->getSavePresetOverlay() != nullptr);

            setParamPlainValue(proc.apvts, "diepte", 0.77f);
            setParamPlainValue(proc.apvts, "polderPunch", 0.42f);
            setParamPlainValue(proc.apvts, "havenDirt", 0.61f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 215.0f);
            setParamPlainValue(proc.apvts, "masterVolume", -4.5f);
            setParamPlainValue(proc.apvts, "attack", 16.5f);
            setParamPlainValue(proc.apvts, "release", 680.0f);
            setParamPlainValue(proc.apvts, "stadsdeel", 2.0f);
            setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
            setParamPlainValue(proc.apvts, "monoLock1300", 0.0f);

            // Reject factory preset names & empty names
            juce::String err;
            expect(!proc.saveUserPresetToFile(tempDir.getChildFile("Bad.allypreset"), "", false, err));
            expect(!proc.saveUserPresetToFile(tempDir.getChildFile("01_Kasteel_Drill_Slide.allypreset"),
                                              "01_Kasteel_Drill_Slide", false, err));

            if (editor != nullptr && editor->getSavePresetOverlay() != nullptr)
            {
                auto* saveModal = editor->getSavePresetOverlay();
                saveModal->prepareForSave(proc.getPresetStateInfo());
                saveModal->setTargetDirectory(tempDir);

                // Reject empty name in modal
                saveModal->getNameEditor().setText("   ", juce::dontSendNotification);
                expect(!saveModal->attemptSave(), "Blank name must be rejected");

                // Reject factory name in modal
                saveModal->getNameEditor().setText("01_Kasteel_Drill_Slide", juce::dontSendNotification);
                expect(!saveModal->attemptSave(), "Factory preset name must be protected");

                // Valid save to custom user preset file
                saveModal->getNameEditor().setText("Almere_Test_Sub", juce::dontSendNotification);
                expect(saveModal->attemptSave(), "First user preset save must succeed");

                const auto savedFile = tempDir.getChildFile("Almere_Test_Sub.allypreset");
                expect(savedFile.existsAsFile(), ".allypreset file must be created on disk");

                // Modify a parameter and verify first attempt to save under same name stops for overwrite confirmation
                setParamPlainValue(proc.apvts, "masterVolume", -1.5f);
                saveModal->prepareForSave(proc.getPresetStateInfo());
                saveModal->setTargetDirectory(tempDir);
                saveModal->getNameEditor().setText("Almere_Test_Sub", juce::dontSendNotification);
                expect(!saveModal->attemptSave(), "Existing file must require overwrite confirmation");
                expect(saveModal->isOverwritePending(), "Overwrite pending state must activate");
                expectEquals(saveModal->getSaveButton().getButtonText(), juce::String("OVERSCHRIJVEN"));

                // Second attempt confirms explicit overwrite
                expect(saveModal->attemptSave(), "Confirmed overwrite must succeed");
                expect(!saveModal->isOverwritePending(), "Overwrite pending state must clear after save");

                // Corrupt file & wrong schema rejection leaves processor parameters untouched
                const auto badFile = tempDir.getChildFile("Corrupt.allypreset");
                badFile.replaceWithText("<ALLY1300Preset schemaVersion=\"99\"><Meta name=\"Bad\"/></ALLY1300Preset>");
                expect(!proc.loadUserPresetFromFile(badFile, err), "Unsupported schemaVersion must fail");
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "masterVolume"), -1.5f, 0.11f);

                // Reset processor to different preset then load saved .allypreset back
                proc.setCurrentProgram(0);
                expect(proc.loadUserPresetFromFile(savedFile, err), "Valid .allypreset must load cleanly");

                expectEquals(static_cast<int>(proc.getPresetStateInfo().kind),
                             static_cast<int>(AllyPresets::PresetKind::User));
                expectEquals(proc.getPresetStateInfo().displayName, juce::String("Almere_Test_Sub"));
                expect(!proc.getPresetStateInfo().modified, "Freshly loaded user preset must clear modified flag");
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "diepte"), 0.77f, 0.011f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "waterwijkGlide"), 215.0f, 0.51f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "masterVolume"), -1.5f, 0.11f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "stadsdeel"), 2.0f, 1e-4f);
                expectWithinAbsoluteError(getParamPlainValue(proc.apvts, "monoLock1300"), 0.0f, 1e-4f);
                if (editor->getHeaderPanel() != nullptr)
                    expectEquals(editor->getHeaderPanel()->getDisplayedPresetText(), juce::String("Almere_Test_Sub"));
            }
        }

        beginTest("DAW state and editor close/reopen preserve parameters and preset metadata");
        {
            const juce::String preset7Name = AllyPresets::getFactoryPresets()[6].name;
            juce::MemoryBlock stateBlock;
            {
                AllyBassAudioProcessor procA;
                procA.setCurrentProgram(6);
                setParamPlainValue(procA.apvts, "havenDirt", 0.88f);
                setParamPlainValue(procA.apvts, "masterVolume", 1.8f);

                std::unique_ptr<juce::AudioProcessorEditor> edA(procA.createEditor());
                edA.reset(); // Close editor before DAW state save

                procA.getStateInformation(stateBlock);
            }

            AllyBassAudioProcessor procB;
            procB.setStateInformation(stateBlock.getData(), static_cast<int>(stateBlock.getSize()));

            expectEquals(procB.getCurrentProgram(), 6);
            expectEquals(procB.getPresetStateInfo().displayName, preset7Name);
            expect(procB.getPresetStateInfo().modified, "Modified state must survive DAW save/restore");
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "havenDirt"), 0.88f, 0.011f);
            expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "masterVolume"), 1.8f, 0.11f);

            // Reopening editor repeatedly must NEVER reset parameters to Preset 1
            for (int openIdx = 0; openIdx < 2; ++openIdx)
            {
                std::unique_ptr<juce::AudioProcessorEditor> baseEdB(procB.createEditor());
                auto* edB = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEdB.get());
                expect(edB != nullptr && edB->getHeaderPanel() != nullptr);
                if (edB != nullptr && edB->getHeaderPanel() != nullptr)
                {
                    expectEquals(edB->getHeaderPanel()->getDisplayedPresetText(),
                                 preset7Name + " *");
                }
                expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "havenDirt"), 0.88f, 0.011f);
                expectWithinAbsoluteError(getParamPlainValue(procB.apvts, "masterVolume"), 1.8f, 0.11f);
            }
        }

        beginTest("Settings/Save modals prevent stuck MIDI/QWERTY notes and processBlock stays finite");
        {
            AllyBassAudioProcessor proc;
            proc.setRateAndBufferSizeDetails(48000.0, 256);
            proc.prepareToPlay(48000.0, 256);

            std::unique_ptr<juce::AudioProcessorEditor> baseEd(proc.createEditor());
            auto* ed = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEd.get());
            expect(ed != nullptr);

            if (ed != nullptr)
            {
                proc.getKeyboardState().noteOn(1, 36, 0.9f);
                expect(proc.getKeyboardState().isNoteOn(1, 36), "Note 36 should be active before modal opens");

                ed->openSettingsModal();
                expect(ed->isModalOverlayVisible(), "Settings overlay must become visible");
                expect(!proc.getKeyboardState().isNoteOn(1, 36),
                       "Opening Settings must release pressed notes cleanly");

                // Audio buffer remains finite while presets change cleanly outside audio thread
                juce::AudioBuffer<float> audioBuf(2, 256);
                juce::MidiBuffer midiBuf;
                audioBuf.clear();
                proc.setCurrentProgram(2);
                proc.processBlock(audioBuf, midiBuf);
                expect(bufferPeak(audioBuf) >= 0.0f, "Processed audio samples must remain finite");

                ed->closeModalOverlays();
                expect(!ed->isModalOverlayVisible(), "Closing modal must hide overlays cleanly");

                proc.getKeyboardState().noteOn(1, 40, 0.9f);
                ed->openSavePresetModal();
                expect(!proc.getKeyboardState().isNoteOn(1, 40),
                       "Opening Save modal must release pressed notes cleanly");
                ed->closeModalOverlays();
            }
        }

        beginTest("Emblem wave mirror symmetry, exact toolbar order, responsive fit & HiDPI");
        {
            // Verify strict mathematical X-mirror symmetry of A-emblem & double waves
            juce::Path chevA, upWave, lowL, lowR;
            const juce::Rectangle<float> testBox(10.0f, 10.0f, 60.0f, 52.0f);
            AllyGUI::HeaderPanel::buildEmblemPaths(testBox, chevA, upWave, lowL, lowR);

            const float cx = testBox.getCentreX();
            const float chevLeftDist  = cx - chevA.getBounds().getX();
            const float chevRightDist = chevA.getBounds().getRight() - cx;
            expectWithinAbsoluteError(chevLeftDist, chevRightDist, 0.05f);

            const float upLeftDist  = cx - upWave.getBounds().getX();
            const float upRightDist = upWave.getBounds().getRight() - cx;
            expectWithinAbsoluteError(upLeftDist, upRightDist, 0.05f);

            const float lowLeftDist  = cx - lowL.getBounds().getX();
            const float lowRightDist = lowR.getBounds().getRight() - cx;
            expectWithinAbsoluteError(lowLeftDist, lowRightDist, 0.05f);

            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> baseEd(proc.createEditor());
            auto* ed = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEd.get());
            expect(ed != nullptr && ed->getHeaderPanel() != nullptr);

            if (ed != nullptr && ed->getHeaderPanel() != nullptr)
            {
                auto* hdr = ed->getHeaderPanel();
                const juce::Point<int> sizes[] = { { 680, 500 }, { 780, 580 }, { 1200, 850 } };
                for (const auto& sz : sizes)
                {
                    ed->setSize(sz.x, sz.y);

                    const auto chassis = hdr->getChassisBounds();
                    expect(!chassis.isEmpty(), "Header chassis bounds must not be empty");
                    expect(chassis.contains(hdr->getEmblemBounds()), "Emblem must fit inside header chassis");
                    expect(chassis.contains(hdr->getWordmarkBounds()), "Wordmark must fit inside header chassis");
                    expect(chassis.contains(hdr->getLocationBounds()), "Location stack must fit inside header chassis");
                    expect(chassis.contains(hdr->getToolbarBounds()), "Toolbar must fit inside header chassis");

                    expect(hdr->getEmblemBounds().getRight() < hdr->getWordmarkBounds().getX());
                    expect(hdr->getWordmarkBounds().getRight() < hdr->getLocationBounds().getX());
                    expect(hdr->getLocationBounds().getRight() <= hdr->getMapBounds().getX());
                    expect(hdr->getMapBounds().getRight() <= hdr->getToolbarBounds().getX());
                    expect(hdr->getMapBounds().getWidth() >= 38.0f, "Almere map window must remain visible");

                    // Exact toolbar order: < | > | Preset | Open | Save | Settings
                    const juce::Component* toolbarOrder[6] = {
                        &hdr->getPrevButton(),
                        &hdr->getNextButton(),
                        &hdr->getPresetButton(),
                        &hdr->getOpenButton(),
                        &hdr->getSaveButton(),
                        &hdr->getSettingsButton()
                    };
                    for (int idx = 0; idx < 6; ++idx)
                    {
                        expect(hdr->getLocalBounds().contains(toolbarOrder[idx]->getBounds()),
                               "Toolbar control must not clip");
                        if (idx > 0)
                            expect(toolbarOrder[idx - 1]->getRight() <= toolbarOrder[idx]->getX(),
                                   "Toolbar controls must stay strictly left-to-right without overlap");
                    }

                    // Overlays fit cleanly even at minimum 680x500 window size
                    ed->openSettingsModal();
                    if (auto* so = ed->getSettingsOverlay())
                        expect(ed->getLocalBounds().contains(so->getPanelBounds()), "Settings panel must fit inside editor");
                    ed->openSavePresetModal();
                    if (auto* svo = ed->getSavePresetOverlay())
                        expect(ed->getLocalBounds().contains(svo->getPanelBounds()), "Save modal must fit inside editor");
                    ed->closeModalOverlays();
                }

                ed->setSize(780, 580);
                const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
                for (const float scale : scales)
                {
                    const int rw = juce::roundToInt(static_cast<float>(hdr->getWidth()) * scale);
                    const int rh = juce::roundToInt(static_cast<float>(hdr->getHeight()) * scale);
                    juce::Image img(juce::Image::ARGB, rw, rh, true);
                    {
                        juce::Graphics g(img);
                        g.addTransform(juce::AffineTransform::scale(scale));
                        hdr->paintEntireComponent(g, true);
                    }
                    expect(img.isValid() && img.getWidth() > 0 && img.getHeight() > 0);
                }
            }
        }
    }
};

static HeaderBrandingAndPresetTests headerBrandingAndPresetTests;

// ==============================================================================
// C.4B Regression Suite: Weerwater Vector Scope, Post-Output Peak/RMS Meter & Concurrency
// ==============================================================================
class ScopeAndOutputMeterTests final : public juce::UnitTest
{
public:
    ScopeAndOutputMeterTests() : juce::UnitTest("Scope and Output Meter (C.4B)", "AllyBass") {}

    void runTest() override
    {
        auto renderAtScale = [](juce::Component& comp, float scale) -> juce::Image
        {
            const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(comp.getWidth()) * scale));
            const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(comp.getHeight()) * scale));
            juce::Image img(juce::Image::ARGB, rw, rh, true);
            juce::Graphics g(img);
            g.addTransform(juce::AffineTransform::scale(scale));
            comp.paintEntireComponent(g, true);
            return img;
        };

        auto imageDiffRms = [](const juce::Image& a, const juce::Image& b) -> double
        {
            if (a.getWidth() != b.getWidth() || a.getHeight() != b.getHeight())
                return 1.0;

            const int w = a.getWidth();
            const int h = a.getHeight();
            double sumSq = 0.0;
            const int step = 2;
            int count = 0;

            for (int y = 0; y < h; y += step)
            {
                for (int x = 0; x < w; x += step)
                {
                    const auto ca = a.getPixelAt(x, y);
                    const auto cb = b.getPixelAt(x, y);
                    const double dr = (ca.getFloatRed()   - cb.getFloatRed());
                    const double dg = (ca.getFloatGreen() - cb.getFloatGreen());
                    const double db = (ca.getFloatBlue()  - cb.getFloatBlue());
                    const double da = (ca.getFloatAlpha() - cb.getFloatAlpha());
                    sumSq += 0.25 * (dr * dr + dg * dg + db * db + da * da);
                    ++count;
                }
            }

            return count > 0 ? std::sqrt(sumSq / static_cast<double>(count)) : 0.0;
        };

        beginTest("Lock-free telemetry remains finite and race-free under concurrent audio & GUI reads");
        {
            const double sampleRates[] = { 44100.0, 48000.0, 96000.0 };
            for (const double sr : sampleRates)
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, sr, 256);
                proc.prepareToPlay(sr, 256);

                std::atomic<bool> consumerReady { false };
                std::atomic<bool> stopFlag { false };
                std::atomic<bool> consumerSawBadFloat { false };
                std::atomic<uint64_t> consumerReads { 0 };

                std::thread consumer([&]()
                {
                    std::array<float, 1536> localScope {};
                    consumerReady.store(true, std::memory_order_release);
                    while (!stopFlag.load(std::memory_order_acquire))
                    {
                        proc.getVisualizerData(localScope.data(), 1536);
                        for (float s : localScope)
                        {
                            if (!std::isfinite(s) || std::abs(s) > 10.0f)
                                consumerSawBadFloat.store(true, std::memory_order_relaxed);
                        }
                        const auto peeked = proc.peekOutputMeterMetrics();
                        if (!std::isfinite(peeked.peakLinear) || !std::isfinite(peeked.rmsLinear))
                            consumerSawBadFloat.store(true, std::memory_order_relaxed);
                        consumerReads.fetch_add(1, std::memory_order_release);
                    }
                });

                while (!consumerReady.load(std::memory_order_acquire))
                    std::this_thread::yield();

                juce::AudioBuffer<float> audio(2, 256);
                juce::MidiBuffer midi;
                int blocksExecuted = 0;
                while (blocksExecuted < 120 || consumerReads.load(std::memory_order_acquire) < 8)
                {
                    const int b = blocksExecuted;
                    midi.clear();
                    if (b == 2)
                        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
                    else if (b == 40)
                    {
                        setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
                        setParamPlainValue(proc.apvts, "monoLock1300", 0.0f);
                        midi.addEvent(juce::MidiMessage::noteOn(1, 45, 0.95f), 0);
                    }
                    else if (b == 80)
                    {
                        setParamPlainValue(proc.apvts, "overdrive036", 0.0f);
                        setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);
                        midi.addEvent(juce::MidiMessage::noteOff(1, 45), 0);
                        midi.addEvent(juce::MidiMessage::noteOff(1, 36), 4);
                    }

                    audio.clear();
                    proc.processBlock(audio, midi);
                    ++blocksExecuted;
                }

                stopFlag.store(true, std::memory_order_release);
                consumer.join();

                expect(!consumerSawBadFloat.load(), "Concurrent consumer must never observe NaN, Inf, or torn values");
                expect(consumerReads.load() >= 8, "Concurrent consumer thread must execute reads");
                expectEquals(static_cast<int>(proc.getVisualizerWriteCounter()), blocksExecuted);
                expectWithinAbsoluteError(static_cast<float>(proc.getCurrentSampleRate()),
                                          static_cast<float>(sr), 0.1f);
            }
        }

        beginTest("Post-output Peak/RMS meter and visualizer reflect DSP, masterVolume, mono vs stereo, transients, clip, and silence");
        {
            // 1. Stereo & Mono RMS parity (dual-mono signal must NOT read +3dB louder on stereo bus)
            //    Note: compare with monoLock bypassed because MonoLockFilter intentionally only filters >=2 channels.
            auto measureSteadyRms = [](int numChannels, float masterDb, bool overdrive, bool monoLock,
                                       float& outBlockPeak, float& outBlockRms)
            {
                AllyBassAudioProcessor p;
                p.setPlayConfigDetails(0, numChannels, 44100.0, 256);
                p.prepareToPlay(44100.0, 256);
                AllyPresets::applyPreset(p.apvts, 0);
                setParamPlainValue(p.apvts, "masterVolume", masterDb);
                setParamPlainValue(p.apvts, "overdrive036", overdrive ? 1.0f : 0.0f);
                setParamPlainValue(p.apvts, "monoLock1300", monoLock ? 1.0f : 0.0f);
                setParamPlainValue(p.apvts, "attack", 8.0f);
                setParamPlainValue(p.apvts, "diepte", 0.5f);

                juce::AudioBuffer<float> buf(numChannels, 256);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);
                for (int b = 0; b < 8; ++b)
                {
                    buf.clear();
                    p.processBlock(buf, midi);
                    midi.clear();
                }

                // Consume earlier transients and inspect exact last steady block
                (void) p.consumeOutputMeterMetrics();
                buf.clear();
                p.processBlock(buf, midi);

                double sumSq = 0.0;
                float peak = 0.0f;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const float* ptr = buf.getReadPointer(ch);
                    for (int s = 0; s < buf.getNumSamples(); ++s)
                    {
                        peak = juce::jmax(peak, std::abs(ptr[s]));
                        sumSq += static_cast<double>(ptr[s]) * static_cast<double>(ptr[s]);
                    }
                }
                outBlockPeak = peak;
                outBlockRms = static_cast<float>(std::sqrt(sumSq / static_cast<double>(buf.getNumSamples() * numChannels)));
                return p.peekOutputMeterMetrics();
            };

            float stereoPeak = 0.0f, stereoRms = 0.0f;
            const auto stereoMetrics = measureSteadyRms(2, 0.0f, false, false, stereoPeak, stereoRms);
            expect(stereoPeak > 0.1f, "Stereo active sub note must produce measurable peak");
            expectWithinAbsoluteError(stereoMetrics.peakLinear, stereoPeak, 1e-4f);
            expectWithinAbsoluteError(stereoMetrics.rmsLinear, stereoRms, 1e-4f);
            expectEquals(stereoMetrics.channelCount, 2);

            float monoPeak = 0.0f, monoRms = 0.0f;
            const auto monoMetrics = measureSteadyRms(1, 0.0f, false, false, monoPeak, monoRms);
            expectWithinAbsoluteError(monoMetrics.peakLinear, monoPeak, 1e-4f);
            expectWithinAbsoluteError(monoMetrics.rmsLinear, monoRms, 1e-4f);
            expectWithinAbsoluteError(monoMetrics.rmsLinear, stereoMetrics.rmsLinear, 1e-4f);

            // Stereo with MonoLock active must also match exact post-DSP buffer peak/RMS
            float mlPeak = 0.0f, mlRms = 0.0f;
            const auto mlMetrics = measureSteadyRms(2, 0.0f, false, true, mlPeak, mlRms);
            expectWithinAbsoluteError(mlMetrics.peakLinear, mlPeak, 1e-4f);
            expectWithinAbsoluteError(mlMetrics.rmsLinear, mlRms, 1e-4f);

            // 2. Post-masterVolume scaling must visibly scale both OutputMeterMetrics & Visualizer FIFO
            float attPeak = 0.0f, attRms = 0.0f;
            const auto attMetrics = measureSteadyRms(2, -12.0f, false, false, attPeak, attRms);
            expect(attMetrics.rmsLinear < stereoMetrics.rmsLinear * 0.35f,
                   "masterVolume -12 dB attenuation must scale post-output RMS linearly (~0.25x)");

            // 3. Real clipping threshold detection (> 0 dBFS / >= 1.0 linear) with 036 Overdrive + +6dB
            float hotPeak = 0.0f, hotRms = 0.0f;
            const auto hotMetrics = measureSteadyRms(2, 6.0f, true, true, hotPeak, hotRms);
            expect(hotMetrics.peakLinear >= 1.0f && hotMetrics.clipped,
                   "Hot overdriven +6dB output must exceed 0 dBFS and latch clipped flag");

            // 4. Latched transient capture & meter ballistics recovery on silence
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 44100.0, 256);
                proc.prepareToPlay(44100.0, 256);
                setParamPlainValue(proc.apvts, "polderPunch", 1.0f);
                setParamPlainValue(proc.apvts, "masterVolume", 0.0f);

                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
                buf.clear();
                proc.processBlock(buf, midi);
                midi.clear();

                // Latch transient peak even if 3 lower/silent blocks happen before GUI consumes
                midi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
                for (int b = 0; b < 3; ++b)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                    midi.clear();
                }

                const auto consumed = proc.consumeOutputMeterMetrics();
                expect(consumed.peakLinear > 0.25f, "Latched peak must preserve short Polder Punch transient until consumed");

                // Drain trailing IIR settle to complete silence
                for (int b = 0; b < 35; ++b)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                }
                (void) proc.consumeOutputMeterMetrics(); // clear latched peak from previous blocks
                const auto silentMetrics = proc.peekOutputMeterMetrics();
                expect(silentMetrics.peakLinear < 1e-4f && silentMetrics.rmsLinear < 1e-4f && !silentMetrics.clipped,
                       "Telemetry must settle cleanly to zero on silence");
            }

            // 5. OutputLevelMeter component ballistics, warning/clip threshold & dark idle
            {
                AllyGUI::OutputLevelMeter meter;
                meter.setBounds(0, 0, 14, 130);

                // Silent -> 0 segments lit
                meter.advanceWithSampledLevels(0.0f, 0.0f, false, 0.022f);
                expectEquals(meter.getLitPeakSegments(), 0);
                expect(!meter.isClipLatched());
                const auto imgSilent = renderAtScale(meter, 1.0f);

                // Normal nominal -6 dBFS -> cyan segments only, no clip
                const float nomLin = juce::Decibels::decibelsToGain(-6.0f);
                meter.advanceWithSampledLevels(nomLin, nomLin * 0.707f, false, 0.022f);
                expect(meter.getLitPeakSegments() >= 10 && meter.getLitPeakSegments() < AllyGUI::OutputLevelMeter::numSegments,
                       "Nominal -6 dBFS must illuminate proportional meter segments below top clip bar");
                expect(!meter.isClipLatched());
                const auto imgNominal = renderAtScale(meter, 1.0f);
                expect(imageDiffRms(imgSilent, imgNominal) > 0.03,
                       "Nominal meter level must render clearly different from silent idle");

                // Clip +1 dBFS -> top clip segment latched
                const float clipLin = juce::Decibels::decibelsToGain(1.0f);
                meter.advanceWithSampledLevels(clipLin, 0.85f, true, 0.022f);
                expect(meter.isClipLatched());
                expectEquals(meter.getPeakHoldSegmentIndex(), AllyGUI::OutputLevelMeter::numSegments - 1);
                const auto imgClip = renderAtScale(meter, 1.0f);
                expect(imageDiffRms(imgNominal, imgClip) > 0.015,
                       "Overload/clip meter state must differ visually from nominal level");

                // Decay back to silence after releasing clip hold
                for (int f = 0; f < 100; ++f)
                    meter.advanceWithSampledLevels(0.0f, 0.0f, false, 0.025f);
                expectEquals(meter.getLitPeakSegments(), 0);
                expect(!meter.isClipLatched());
            }
        }

        beginTest("Weerwater Vector Scope layout, zero fake coherence, parameter responsiveness, and non-shrinking meter integration");
        {
            AllyBassAudioProcessor proc;
            std::unique_ptr<juce::AudioProcessorEditor> editor(proc.createEditor());
            expect(editor != nullptr, "Editor must be created");

            if (editor != nullptr)
            {
                auto* scope = findChildOfType<AllyGUI::NapWaveVisualizer>(*editor);
                auto* primaryPanel = findChildOfType<AllyGUI::PrimaryKnobsPanel>(*editor);
                expect(scope != nullptr, "NapWaveVisualizer must exist in editor");
                expect(primaryPanel != nullptr, "PrimaryKnobsPanel must exist in editor");

                if (scope != nullptr && primaryPanel != nullptr)
                {
                    auto* meter = primaryPanel->getOutputMeter();
                    expect(meter != nullptr, "PrimaryKnobsPanel must contain integrated OutputLevelMeter");

                    // 1. Verify responsive bounds across 680x500, 780x580, 1200x850
                    const juce::Point<int> sizes[] = { { 680, 500 }, { 780, 580 }, { 1200, 850 } };
                    for (const auto& sz : sizes)
                    {
                        editor->setSize(sz.x, sz.y);

                        expect(!scope->getPanelBounds().isEmpty());
                        expect(scope->getLocalBounds().toFloat().contains(scope->getPanelBounds()));
                        expect(scope->getPanelBounds().contains(scope->getHeaderBounds()));
                        expect(scope->getPanelBounds().contains(scope->getScaleBounds()));
                        expect(scope->getPanelBounds().contains(scope->getPlotBounds()));
                        expect(scope->getScaleBounds().getRight() < scope->getPlotBounds().getX(),
                               "Left elevation scale must sit strictly left of waveform plot");
                        expect(scope->getHeaderBounds().getBottom() <= scope->getPlotBounds().getY(),
                               "Top scope header must sit strictly above plot");

                        if (meter != nullptr)
                        {
                            expect(primaryPanel->getChassisBounds().contains(meter->getBounds().toFloat()),
                                   "OutputLevelMeter must fit inside PrimaryKnobsPanel chassis");
                            expect(primaryPanel->getKnob(4)->getRight() <= meter->getX(),
                                   "OutputLevelMeter must not overlap OUTPUT DepthRingKnob");
                            expect(meter->getWidth() >= 9 && meter->getHeight() >= 48,
                                   "OutputLevelMeter must remain legible at all window sizes");
                        }
                    }

                    editor->setSize(780, 580);

                    // 2. Verify NO fake 100% coherence percentage and honest MONO LOCK status text
                    setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);
                    std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> silenceBuf {};
                    scope->updateScopeWithSamples(silenceBuf.data(),
                                                  AllyGUI::NapWaveVisualizer::captureSampleCount,
                                                  false, true, 44100.0);
                    expect(!scope->getDisplayedMonoStatusText(false).contains("100%"),
                           "Scope must never display fake 100% phase coherence");
                    expect(scope->getDisplayedMonoStatusText(false).contains("ACTIVE"));
                    expect(!scope->isSignalActive(), "Silent input must keep scope calm on -4.5 m NAP datum");

                    const auto scopeSilentImg = renderAtScale(*scope, 1.0f);

                    setParamPlainValue(proc.apvts, "monoLock1300", 0.0f);
                    scope->updateScopeWithSamples(silenceBuf.data(),
                                                  AllyGUI::NapWaveVisualizer::captureSampleCount,
                                                  false, false, 44100.0);
                    expect(scope->getDisplayedMonoStatusText(false).contains("BYPASSED"));
                    const auto scopeBypassedImg = renderAtScale(*scope, 1.0f);
                    expect(imageDiffRms(scopeSilentImg, scopeBypassedImg) > 0.002,
                           "Toggling 1300 MONO LOCK must update visible status in Scope");

                    // 3. Feed real rendered synth wave (note C2 + Overdrive + Polder Punch) and verify visual change
                    proc.setPlayConfigDetails(0, 2, 44100.0, 256);
                    proc.prepareToPlay(44100.0, 256);
                    AllyPresets::applyPreset(proc.apvts, 0);
                    setParamPlainValue(proc.apvts, "polderPunch", 0.8f);
                    setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
                    setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);

                    juce::AudioBuffer<float> audio(2, 256);
                    juce::MidiBuffer midi;
                    midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.95f), 0);
                    for (int b = 0; b < 8; ++b)
                    {
                        audio.clear();
                        proc.processBlock(audio, midi);
                        midi.clear();
                    }

                    std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> liveBuf {};
                    proc.getVisualizerData(liveBuf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
                    scope->updateScopeWithSamples(liveBuf.data(),
                                                  AllyGUI::NapWaveVisualizer::captureSampleCount,
                                                  proc.isOverdriveActive(),
                                                  proc.isMonoLockActive(),
                                                  proc.getCurrentSampleRate());
                    expect(scope->isSignalActive(), "Live sub audio must activate Weerwater Vector Scope waveform");
                    const auto scopeLiveImg = renderAtScale(*scope, 1.0f);
                    expect(imageDiffRms(scopeSilentImg, scopeLiveImg) > 0.02,
                           "Active sub waveform must render noticeably different from silent datum line");

                    // 4. HiDPI scaling checks + CPU paint micro-benchmark
                    const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
                    for (const float scale : scales)
                    {
                        const auto scaledScope = renderAtScale(*scope, scale);
                        expect(scaledScope.isValid() && scaledScope.getWidth() > 0 && scaledScope.getHeight() > 0);
                    }

                    const double t0 = juce::Time::getMillisecondCounterHiRes();
                    for (int frame = 0; frame < 60; ++frame)
                    {
                        (void) renderAtScale(*scope, 1.0f);
                        if (meter != nullptr)
                            (void) renderAtScale(*meter, 1.0f);
                    }
                    const double elapsedMs = juce::Time::getMillisecondCounterHiRes() - t0;
                    const double avgMsPerFrame = elapsedMs / 60.0;
                    logMessage("[PERFORMANCE-BENCHMARK] Scope + OutputLevelMeter 1x software render avg: "
                               + juce::String(avgMsPerFrame, 3) + " ms/frame");
                    if (gEnforceWallClockRenderBudgets)
                    {
                        expect(avgMsPerFrame < 15.0,
                               "Average software render time for Scope + OutputMeter must remain smooth under 15 ms/frame");
                    }
                }
            }
        }
    }
};

static ScopeAndOutputMeterTests scopeAndOutputMeterTests;

class KeyboardAndFinalIntegrationTests final : public juce::UnitTest
{
public:
    KeyboardAndFinalIntegrationTests() : UnitTest("Keyboard, Octave, Footer and Final UI Integration", "AllyBass") {}

    void runTest() override
    {
        beginTest("Engineered Depth UI removes legacy Brutalist buttons and integrates aligned bays across sizes");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);

            std::unique_ptr<juce::AudioProcessorEditor> baseEd(proc.createEditor());
            auto* ed = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEd.get());
            expect(ed != nullptr, "Editor must instantiate cleanly");

            if (ed != nullptr)
            {
                // Verify legacy widgets & Brutalist octave TextButtons are completely gone
                expect(findChildOfType<AllyGUI::HeavyKnob>(*ed) == nullptr, "No HeavyKnob allowed");
                expect(findChildOfType<AllyGUI::BrutalistToggle>(*ed) == nullptr, "No BrutalistToggle allowed");
                expect(findChildOfType<juce::TextButton>(*ed) == nullptr,
                       "Old Brutalist TextButton octave controls must be replaced by KeyboardPanel");

                auto* header    = ed->getHeaderPanel();
                auto& scope     = ed->getVisualizer();
                auto* primary   = ed->getPrimaryKnobsPanel();
                auto* secondary = ed->getSecondaryDspPanel();
                auto* kbPanel   = ed->getKeyboardPanel();

                expect(header != nullptr && primary != nullptr && secondary != nullptr && kbPanel != nullptr,
                       "All 5 Engineered Depth bays must be present");

                const juce::Point<int> sizes[] = { { 680, 500 }, { 780, 580 }, { 1200, 850 } };
                for (const auto& sz : sizes)
                {
                    ed->setSize(sz.x, sz.y);

                    const auto full   = ed->getLocalBounds();
                    const auto hB     = header->getBounds();
                    const auto sB     = scope.getBounds();
                    const auto pB     = primary->getBounds();
                    const auto secB   = secondary->getBounds();
                    const auto kbB    = kbPanel->getBounds();
                    const auto footB  = ed->getFooterBounds();

                    expect(full.contains(hB) && full.contains(sB) && full.contains(pB)
                           && full.contains(secB) && full.contains(kbB) && full.contains(footB),
                           "All 5 bays and footer must fit inside editor bounds at "
                           + juce::String(sz.x) + "x" + juce::String(sz.y));

                    expect(hB.getBottom() <= sB.getY()
                           && sB.getBottom() <= pB.getY()
                           && pB.getBottom() <= secB.getY()
                           && secB.getBottom() <= kbB.getY()
                           && kbB.getBottom() <= footB.getY(),
                           "Bays must stack top-to-bottom without overlap");

                    expect(std::abs(hB.getX() - sB.getX()) <= 1
                           && std::abs(sB.getX() - pB.getX()) <= 1
                           && std::abs(pB.getX() - secB.getX()) <= 1
                           && std::abs(secB.getX() - kbB.getX()) <= 1,
                           "Left edges of all 5 console bays must align cleanly");

                    expect(std::abs(hB.getRight() - sB.getRight()) <= 1
                           && std::abs(sB.getRight() - pB.getRight()) <= 1
                           && std::abs(pB.getRight() - secB.getRight()) <= 1
                           && std::abs(secB.getRight() - kbB.getRight()) <= 1,
                           "Right edges of all 5 console bays must align cleanly");

                    expect(!kbPanel->getChassisBounds().isEmpty());
                    expect(!kbPanel->getOctaveReadoutBounds().isEmpty());
                    expect(kbPanel->getOctaveBarBounds().getRight() <= kbPanel->getKeyboardBedBounds().getX(),
                           "Octave control must sit left of piano keybed without colliding");
                    expect(kbPanel->getKeyboardBedBounds().getWidth() >= 400
                           && kbPanel->getKeyboardBedBounds().getHeight() >= 38,
                           "Keybed must keep comfortable playable dimensions");
                }

                ed->setSize(780, 580);

                // Truthful footer status + right credits
                const auto footerStatus = ed->getFooterStatusText();
                expect(footerStatus.startsWith("OCTAVE: C2 (Z/X) // "));
                expect(!footerStatus.contains("[A-K]"), "Footer must report accurate [A-P] key range, not [A-K]");
                expect(footerStatus.contains("[A-P]"));
                expectEquals(ed->getFooterCreditsText(), juce::String("JUICED UNICORN / IZHIA ARTS / 42GRAM"));

                // HiDPI full-editor scaling and paint performance budget
                const float scales[] = { 1.0f, 1.25f, 1.5f, 2.0f };
                for (const float scale : scales)
                {
                    const auto img = renderAtScale(*ed, scale);
                    expect(img.isValid() && img.getWidth() > 0 && img.getHeight() > 0);
                }

                const double t0 = juce::Time::getMillisecondCounterHiRes();
                for (int frame = 0; frame < 30; ++frame)
                    (void) renderAtScale(*ed, 1.0f);
                const double avgMs = (juce::Time::getMillisecondCounterHiRes() - t0) / 30.0;
                logMessage("[PERFORMANCE-BENCHMARK] Full editor (780x580) 1x software render avg: " + juce::String(avgMs, 3) + " ms/frame");
                if (gEnforceWallClockRenderBudgets)
                    expect(avgMs < 25.0, "Full Engineered Depth editor paint must stay responsive under 25 ms/frame");
            }
        }

        beginTest("Interactive keyboard mouse/MIDI/QWERTY, octave sync, note safety and active cyan rendering");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);

            std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
            ed->setSize(780, 580);

            auto* kbPanel = ed->getKeyboardPanel();
            expect(kbPanel != nullptr);
            if (kbPanel == nullptr)
                return;

            auto& kb = kbPanel->getKeyboard();
            expectEquals(ed->getCurrentOctave(), 2);
            expectEquals(kbPanel->getDisplayedOctaveText(), juce::String("OCT: C2"));
            expectEquals(kb.getRangeStart(), 36);
            expectEquals(kb.getRangeEnd(), 72);

            // Key geometry checks: C2 white note (36), raised titanium C#2 black note (37), C3 (48)
            const auto rectC2  = kb.getRectangleForKey(36);
            const auto rectCs2 = kb.getRectangleForKey(37);
            const auto rectC3  = kb.getRectangleForKey(48);
            expect(!rectC2.isEmpty() && !rectCs2.isEmpty() && !rectC3.isEmpty());
            expect(rectC2.getWidth() > rectCs2.getWidth(), "White key must be wider than sharp keycap");
            expect(rectC2.getHeight() > rectCs2.getHeight(), "White keybed must extend below sharp keycap");
            expect(rectC3.getX() > rectC2.getX(), "C3 key must sit right of C2 key");

            // Active cyan note glow visual diff vs silent keybed
            const auto kbSilent = renderAtScale(*kbPanel, 1.0f);
            proc.getKeyboardState().noteOn(1, 36, 0.92f);
            proc.getKeyboardState().noteOn(1, 39, 0.88f);
            kb.timerCallback();
            const auto kbActive = renderAtScale(*kbPanel, 1.0f);
            expect(imageDiffRms(kbSilent, kbActive) > 0.008,
                   "Active MIDI/mouse notes must illuminate keys with subtle Engineered Depth cyan glow");

            // KeyboardState note-on automatically feeds processBlock audio output across blocks
            juce::AudioBuffer<float> audioBuf(2, 256);
            juce::MidiBuffer emptyMidi;
            float kbAudioPeak = 0.0f;
            for (int b = 0; b < 6; ++b)
            {
                audioBuf.clear();
                emptyMidi.clear();
                proc.processBlock(audioBuf, emptyMidi);
                kbAudioPeak = juce::jmax(kbAudioPeak, bufferPeak(audioBuf));
            }
            expect(kbAudioPeak > 0.05f, "Note triggered via MidiKeyboardState must produce clear sub audio");

            // Clicking + octave step button releases active notes cleanly, clamps, and syncs SettingsOverlay + Footer
            expect(kbPanel->getOctaveUpButton().onClick != nullptr);
            kbPanel->getOctaveUpButton().onClick();
            expect(!proc.getKeyboardState().isNoteOn(1, 36) && !proc.getKeyboardState().isNoteOn(1, 39),
                   "Octave up button must release held notes immediately (zero stuck notes)");
            expectEquals(ed->getCurrentOctave(), 3);
            expectEquals(kbPanel->getDisplayedOctaveText(), juce::String("OCT: C3"));
            expectEquals(kb.getRangeStart(), 48);
            expect(ed->getSettingsOverlay() != nullptr && ed->getSettingsOverlay()->getCurrentOctave() == 3,
                   "SettingsOverlay octave display must stay synced with keyboard + button");
            expect(ed->getFooterStatusText().startsWith("OCTAVE: C3 (Z/X) // "));

            // Z / X shortcut sync + upper/lower bounds [0..5]
            expect(ed->keyPressed(juce::KeyPress('z')));
            expectEquals(ed->getCurrentOctave(), 2);
            for (int i = 0; i < 6; ++i)
            {
                if (kbPanel->getOctaveUpButton().isEnabled() && kbPanel->getOctaveUpButton().onClick != nullptr)
                    kbPanel->getOctaveUpButton().onClick();
            }
            expectEquals(ed->getCurrentOctave(), 5);
            expect(!kbPanel->getOctaveUpButton().isEnabled(), "+ button must disable at C5 max");
            for (int i = 0; i < 8; ++i)
                ed->keyPressed(juce::KeyPress('z'));
            expectEquals(ed->getCurrentOctave(), 0);
            expect(!kbPanel->getOctaveDownButton().isEnabled(), "- button must disable at C0 min");
            ed->keyPressed(juce::KeyPress('x'));
            ed->keyPressed(juce::KeyPress('x'));
            expectEquals(ed->getCurrentOctave(), 2);

            // Focus loss, modal open/close & editor teardown never leave stuck notes
            proc.getKeyboardState().noteOn(1, 36, 0.9f);
            proc.getKeyboardState().noteOn(1, 41, 0.9f);
            ed->focusLost(juce::Component::focusChangedDirectly);
            expect(!proc.getKeyboardState().isNoteOn(1, 36) && !proc.getKeyboardState().isNoteOn(1, 41),
                   "focusLost must clear all active notes");

            proc.getKeyboardState().noteOn(1, 43, 0.9f);
            ed->openSettingsModal();
            expect(!proc.getKeyboardState().isNoteOn(1, 43));
            expect(ed->getFooterStatusText().contains("QWERTY PAUSED (MODAL)"),
                   "Footer must truthfully indicate QWERTY is paused while modal is open");
            ed->closeModalOverlays();
            expect(!ed->getFooterStatusText().contains("MODAL"));

            // Preset switching, Secondary DSP & primary parameters remain intact while editor is active
            expectEquals(proc.getNumPrograms(), 14);
            for (int pIdx = 0; pIdx < proc.getNumPrograms(); ++pIdx)
            {
                proc.setCurrentProgram(pIdx);
                audioBuf.clear();
                emptyMidi.clear();
                proc.processBlock(audioBuf, emptyMidi);
                expect(bufferPeak(audioBuf) >= 0.0f);
            }

            proc.getKeyboardState().noteOn(1, 36, 0.95f);
            ed.reset();
            expect(!proc.getKeyboardState().isNoteOn(1, 36),
                   "Destroying editor must release all active keyboard notes cleanly");
        }
    }
};

static KeyboardAndFinalIntegrationTests keyboardAndFinalIntegrationTests;

class DspQualityD1DTests final : public juce::UnitTest
{
public:
    DspQualityD1DTests() : juce::UnitTest("ALLY1300 DSP Quality, Overdrive & Output Integrity (D.1D)", "AllyBass") {}

    void runTest() override
    {
        beginTest("StadsdeelSaturation eliminates Poort/Haven DC offset, fixes Haven 96kHz sub drop, and removes drive/mode steps");
        {
            constexpr double srs[] = { 44100.0, 48000.0, 96000.0 };
            std::array<double, 3> havenRms {};

            for (size_t srIdx = 0; srIdx < 3; ++srIdx)
            {
                const double sr = srs[srIdx];
                for (int m = 0; m < 4; ++m)
                {
                    const auto mode = static_cast<AllyDSP::StadsdeelMode>(m);
                    AllyDSP::StadsdeelSaturation sat;
                    sat.prepare(sr);

                    constexpr double testHz = 32.70319566; // C1 deepest sub
                    const int settleSamples = static_cast<int>(std::llround(sr * 1.5));
                    const int cycleLen = static_cast<int>(std::llround(10.0 * sr / testHz));
                    const int activeEnd = settleSamples + cycleLen;
                    const int silenceEnd = activeEnd + static_cast<int>(std::llround(sr * 0.5));

                    double sumSq = 0.0;
                    double dcActiveSum = 0.0;
                    int phase = 0;

                    for (int i = 0; i < silenceEnd; ++i)
                    {
                        const float x = (i < activeEnd)
                            ? (0.75f * std::sin(2.0f * juce::MathConstants<float>::pi
                                                * static_cast<float>(testHz * static_cast<double>(phase++) / sr)))
                            : 0.0f;
                        const float y = sat.processSample(x, 0.60f, mode);
                        expect(std::isfinite(y), "StadsdeelSaturation output must remain finite");
                        if (i >= settleSamples && i < activeEnd)
                        {
                            sumSq += static_cast<double>(y) * static_cast<double>(y);
                            dcActiveSum += y;
                        }
                    }

                    const double activeDc = dcActiveSum / static_cast<double>(cycleLen);
                    expect(std::abs(activeDc) < 0.0025,
                           "Steady-state DC offset must remain near zero in all Stadsdeel modes");

                    if (mode == AllyDSP::StadsdeelMode::Haven)
                        havenRms[srIdx] = std::sqrt(sumSq / static_cast<double>(cycleLen));
                }
            }

            const double havenDb44vs96 = 20.0 * std::log10(havenRms[2] / havenRms[0]);
            expect(std::abs(havenDb44vs96) < 0.25,
                   "Haven C1 sub-bass RMS must remain sample-rate-invariant across 44.1/48/96 kHz");

            // Continuous threshold across drive 0.0005 -> 0.0020 & continuous live mode switches
            for (int m = 0; m < 4; ++m)
            {
                AllyDSP::StadsdeelSaturation sat;
                sat.prepare(48000.0);
                float prev = 0.0f;
                float jump = 0.0f;
                for (int i = 0; i < 4800; ++i)
                {
                    const float x = 0.7f * std::sin(2.0f * juce::MathConstants<float>::pi * 65.4f * static_cast<float>(i) / 48000.0f);
                    const float d = (i < 2400) ? 0.0005f : 0.0020f;
                    const float y = sat.processSample(x, d, static_cast<AllyDSP::StadsdeelMode>(m));
                    if (i == 2400)
                        jump = std::abs(y - prev);
                    prev = y;
                }
                expect(jump < 0.025f, "StadsdeelSaturation drive transition around 0.001 must not click");
            }

            for (int m = 0; m < 3; ++m)
            {
                AllyDSP::StadsdeelSaturation sat;
                sat.prepare(48000.0);
                float prev = 0.0f;
                float jump = 0.0f;
                for (int i = 0; i < 4800; ++i)
                {
                    const float x = 0.7f * std::sin(2.0f * juce::MathConstants<float>::pi * 65.4f * static_cast<float>(i) / 48000.0f);
                    const auto mode = static_cast<AllyDSP::StadsdeelMode>((i < 2400) ? m : (m + 1));
                    const float y = sat.processSample(x, 0.6f, mode);
                    if (i == 2400)
                        jump = std::abs(y - prev);
                    prev = y;
                }
                expect(jump < 0.015f, "Switching Stadsdeel modes live must crossfade cleanly without clicks");
            }
        }

        beginTest("Overdrive036 maintains exact L/R stereo parity, SR-invariant smooth bypass, and oversized-block safety");
        {
            for (double sr : { 44100.0, 48000.0, 96000.0 })
            {
                AllyDSP::Overdrive036 od;
                od.prepare({ sr, 256u, 2u });

                juce::AudioBuffer<float> buf(2, 256);
                float prevL = 0.0f;
                float maxToggleDelta = 0.0f;
                float maxLrMismatch = 0.0f;
                int phaseIdx = 0;

                for (int b = 0; b < 40; ++b)
                {
                    for (int i = 0; i < 256; ++i)
                    {
                        const float s = 0.6f * std::sin(2.0f * juce::MathConstants<float>::pi * 65.4f
                                                        * static_cast<float>(phaseIdx++) / static_cast<float>(sr));
                        buf.setSample(0, i, s);
                        buf.setSample(1, i, s);
                    }

                    const bool en = (b >= 4 && b < 24);
                    od.process(buf, en);

                    for (int i = 0; i < 256; ++i)
                    {
                        const float l = buf.getSample(0, i);
                        const float r = buf.getSample(1, i);
                        expect(std::isfinite(l) && std::isfinite(r), "Overdrive036 output must stay finite");
                        maxLrMismatch = juce::jmax(maxLrMismatch, std::abs(l - r));
                        if (!(b == 0 && i == 0))
                            maxToggleDelta = juce::jmax(maxToggleDelta, std::abs(l - prevL));
                        prevL = l;
                    }
                }

                expect(maxLrMismatch < 1e-6f, "Overdrive036 must keep exact L/R stereo parity on mono input");
                expect(maxToggleDelta < 0.02f, "Overdrive036 OFF->ON->OFF transitions must remain click-free");
            }

            // Oversized block L/R parity check
            AllyDSP::Overdrive036 odBig;
            odBig.prepare({ 48000.0, 128u, 2u });
            juce::AudioBuffer<float> bigBuf(2, 2048);
            for (int i = 0; i < 2048; ++i)
            {
                const float s = 0.6f * std::sin(2.0f * juce::MathConstants<float>::pi * 65.4f * static_cast<float>(i) / 48000.0f);
                bigBuf.setSample(0, i, s);
                bigBuf.setSample(1, i, s);
            }
            odBig.process(bigBuf, true);
            float bigLrDiff = 0.0f;
            for (int i = 0; i < 2048; ++i)
                bigLrDiff = juce::jmax(bigLrDiff, std::abs(bigBuf.getSample(0, i) - bigBuf.getSample(1, i)));
            expect(bigLrDiff < 1e-6f, "Overdrive036 chunked oversized blocks must remain stereo-identical");
        }

        beginTest("Parameter automation smoothing, FFT spectral integrity, and all-preset output integrity across block sizes");
        {
            struct StepSpec { const char* id; float v0; float v1; float maxJump; };
            const StepSpec steps[] = {
                { "masterVolume", -24.0f, 6.0f, 0.025f },
                { "diepte",         0.0f, 1.0f, 0.040f },
                { "havenDirt",      0.0f, 1.0f, 0.025f },
                { "polderPunch",    0.0f, 1.0f, 0.030f }
            };

            for (const auto& st : steps)
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 256);
                proc.prepareToPlay(48000.0, 256);
                setParamPlainValue(proc.apvts, "attack", 1.0f);
                setParamPlainValue(proc.apvts, "stadsdeel", 3.0f);
                setParamPlainValue(proc.apvts, st.id, st.v0);

                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 36, 1.0f), 0);
                for (int b = 0; b < 20; ++b)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                    midi.clear();
                }
                const float lastBefore = buf.getSample(0, 255);

                setParamPlainValue(proc.apvts, st.id, st.v1);
                buf.clear();
                proc.processBlock(buf, midi);
                const float firstAfter = buf.getSample(0, 0);
                expect(std::abs(firstAfter - lastBefore) < st.maxJump,
                       "Block-rate step automation must be smoothed per-sample cleanly");
            }

            // Steady-state DC & headroom verification across all 14 factory presets
            const auto& presets = AllyPresets::getFactoryPresets();
            const int exactLen = static_cast<int>(std::llround(20.0 * 48000.0 / (0.5 * juce::MidiMessage::getMidiNoteInHertz(36))));
            for (int pIdx = 0; pIdx < static_cast<int>(presets.size()); ++pIdx)
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 256);
                proc.prepareToPlay(48000.0, 256);
                proc.setCurrentProgram(pIdx);

                juce::AudioBuffer<float> buf(2, 256);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.9f), 0);

                double steadySum = 0.0;
                int steadyCount = 0;
                float peak0dB = 0.0f;
                for (int b = 0; b < 320; ++b)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                    midi.clear();
                    const float p = bufferPeak(buf);
                    expect(p >= 0.0f && p < 1.0f, "All factory presets at 0.0 dB Output must keep finite sub-0 dBFS peaks");
                    peak0dB = juce::jmax(peak0dB, p);
                    if (b >= 188 && steadyCount < exactLen)
                    {
                        const float* ptr = buf.getReadPointer(0);
                        for (int i = 0; i < 256 && steadyCount < exactLen; ++i)
                        {
                            steadySum += ptr[i];
                            ++steadyCount;
                        }
                    }
                }
                expect(peak0dB > 0.1f, "Active factory preset must produce audible output");
                const double steadyDc = steadySum / static_cast<double>(steadyCount);
                expect(std::abs(steadyDc) < 0.005, "Sustained factory preset steady DC offset must stay < 0.005");
            }

            // FFT fundamental + non-harmonic alias check on C2 (65.4Hz) and C4 (261.6Hz) in Poort + OD
            {
                AllyBassAudioProcessor proc;
                proc.setPlayConfigDetails(0, 2, 48000.0, 512);
                proc.prepareToPlay(48000.0, 512);
                setParamPlainValue(proc.apvts, "diepte", 0.0f);
                setParamPlainValue(proc.apvts, "polderPunch", 0.0f);
                setParamPlainValue(proc.apvts, "havenDirt", 0.70f);
                setParamPlainValue(proc.apvts, "stadsdeel", 2.0f); // Poort
                setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
                setParamPlainValue(proc.apvts, "attack", 1.0f);

                std::vector<float> capture;
                juce::AudioBuffer<float> buf(2, 512);
                juce::MidiBuffer midi;
                midi.addEvent(juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // C4 = 261.63 Hz
                for (int b = 0; b < 96; ++b)
                {
                    buf.clear();
                    proc.processBlock(buf, midi);
                    midi.clear();
                    const float* r = buf.getReadPointer(0);
                    capture.insert(capture.end(), r, r + 512);
                }

                constexpr int fftOrder = 13;
                constexpr int fftSize = 1 << fftOrder;
                juce::dsp::FFT fft(fftOrder);
                std::vector<float> fftBuf(static_cast<size_t>(fftSize * 2), 0.0f);
                double winSum = 0.0;
                const int startOff = static_cast<int>(capture.size()) - fftSize;
                for (int i = 0; i < fftSize; ++i)
                {
                    const double a = 2.0 * juce::MathConstants<double>::pi * static_cast<double>(i) / static_cast<double>(fftSize - 1);
                    const double w = 0.35875 - 0.48829 * std::cos(a) + 0.14128 * std::cos(2.0 * a) - 0.01168 * std::cos(3.0 * a);
                    fftBuf[static_cast<size_t>(i)] = capture[static_cast<size_t>(startOff + i)] * static_cast<float>(w);
                    winSum += w;
                }
                fft.performFrequencyOnlyForwardTransform(fftBuf.data());
                const double binHz = 48000.0 / static_cast<double>(fftSize);
                const double scale = 2.0 / winSum;
                const double fundHz = juce::MidiMessage::getMidiNoteInHertz(60);

                double fundMag = 0.0;
                double worstNonHarmMag = 0.0;
                for (int bin = 3; bin < fftSize / 2 - 1; ++bin)
                {
                    const double mag = static_cast<double>(fftBuf[static_cast<size_t>(bin)]) * scale;
                    const double freq = static_cast<double>(bin) * binHz;
                    const double ratio = freq / fundHz;
                    const double nearestInt = std::round(ratio);
                    const bool isHarm = (nearestInt >= 1.0 && std::abs(freq - nearestInt * fundHz) <= 4.5 * binHz);
                    if (std::abs(freq - fundHz) <= 4.5 * binHz)
                        fundMag = std::max(fundMag, mag);
                    else if (!isHarm)
                        worstNonHarmMag = std::max(worstNonHarmMag, mag);
                }
                const double aliasDb = 20.0 * std::log10(std::max(worstNonHarmMag, 1e-8));
                expect(fundMag > 0.5, "Fundamental energy at C4 must be strong");
                expect(aliasDb < -80.0, "Poort + Overdrive036 C4 non-harmonic/DC leakage floor must be < -80 dBFS");
            }
        }
    }
};

static DspQualityD1DTests dspQualityD1DTests;

class MidiSourceIsolationD1E1Tests final : public juce::UnitTest
{
public:
    MidiSourceIsolationD1E1Tests() : juce::UnitTest("ALLY1300 Same-Note MIDI Source Isolation (D.1E.1)", "AllyBass") {}

    void runTest() override
    {
        beginTest("Primary collision: GUI NoteOff(41) must not silence or darken DAW-held Note 41");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 48000.0, 256);
            proc.prepareToPlay(48000.0, 256);
            setParamPlainValue(proc.apvts, "attack", 1.0f);
            setParamPlainValue(proc.apvts, "release", 20.0f);

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer dawMidi;
            uint64_t extVer = 0;
            std::array<uint64_t, 2> extMask {};

            // 1. DAW holds note 41
            dawMidi.addEvent(juce::MidiMessage::noteOn(1, 41, 0.90f), 0);
            for (int b = 0; b < 6; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
                dawMidi.clear();
            }
            proc.syncExternalMidiToKeyboardState(extVer, extMask);
            expectEquals(proc.getEnvelopeStateInt(), 2); // Sustain
            expect(proc.getKeyboardState().isNoteOn(1, 41), "DAW Note 41 must light keyboardState");

            // 2. GUI presses same note 41
            proc.getKeyboardState().noteOn(1, 41, 0.85f);
            buf.clear();
            proc.processBlock(buf, dawMidi);
            expectEquals(proc.getEnvelopeStateInt(), 2);

            // 3. GUI releases note 41 while DAW STILL holds 41
            proc.getKeyboardState().noteOff(1, 41, 0.0f);
            for (int b = 0; b < 15; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
            }
            proc.syncExternalMidiToKeyboardState(extVer, extMask);

            expectEquals(proc.getEnvelopeStateInt(), 2,
                         "GUI NoteOff(41) must NOT push DAW-held Note 41 into Release/Idle");
            expect(bufferPeak(buf) > 0.1f,
                   "Audio output for DAW-held Note 41 must remain active after GUI NoteOff(41)");
            expect(proc.getKeyboardState().isNoteOn(1, 41),
                   "Visual key 41 must remain lit while DAW still holds Note 41");

            // 4. Finally DAW releases note 41 -> voice must release cleanly to Idle and visual must clear
            dawMidi.addEvent(juce::MidiMessage::noteOff(1, 41), 0);
            for (int b = 0; b < 25; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
                dawMidi.clear();
            }
            proc.syncExternalMidiToKeyboardState(extVer, extMask);
            expectEquals(proc.getEnvelopeStateInt(), 0, "Voice must return to Idle once both DAW & GUI released Note 41");
            expect(bufferPeak(buf) < 1e-4f, "Audio must decay to silence after final DAW NoteOff(41)");
            expect(!proc.getKeyboardState().isNoteOn(1, 41), "Visual key 41 must clear after final DAW NoteOff(41)");
        }

        beginTest("Reverse collision: DAW NoteOff(41) must not silence or desync GUI-held Note 41");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 48000.0, 256);
            proc.prepareToPlay(48000.0, 256);
            setParamPlainValue(proc.apvts, "attack", 1.0f);
            setParamPlainValue(proc.apvts, "release", 20.0f);

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer dawMidi;
            uint64_t extVer = 0;
            std::array<uint64_t, 2> extMask {};

            // 1. GUI holds note 41
            proc.getKeyboardState().noteOn(1, 41, 0.88f);
            for (int b = 0; b < 6; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 2);

            // 2. DAW presses same note 41, then DAW releases 41 while GUI STILL holds 41
            dawMidi.addEvent(juce::MidiMessage::noteOn(1, 41, 0.90f), 0);
            buf.clear();
            proc.processBlock(buf, dawMidi);
            dawMidi.clear();
            proc.syncExternalMidiToKeyboardState(extVer, extMask);

            dawMidi.addEvent(juce::MidiMessage::noteOff(1, 41), 0);
            for (int b = 0; b < 15; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
                dawMidi.clear();
            }
            proc.syncExternalMidiToKeyboardState(extVer, extMask);

            expectEquals(proc.getEnvelopeStateInt(), 2,
                         "DAW NoteOff(41) must NOT terminate GUI-held Note 41");
            expect(bufferPeak(buf) > 0.1f,
                   "Audio for GUI-held Note 41 must stay active after DAW NoteOff(41)");
            expect(proc.getKeyboardState().isNoteOn(1, 41),
                   "KeyboardState must keep GUI-held Note 41 lit after DAW NoteOff(41)");

            // 3. GUI releases note 41 -> must actually reach audio thread and stop cleanly (no stuck note!)
            proc.getKeyboardState().noteOff(1, 41, 0.0f);
            for (int b = 0; b < 25; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 0, "GUI NoteOff(41) must stop voice once GUI finally releases");
            expect(bufferPeak(buf) < 1e-4f, "Audio must decay to silence after GUI NoteOff(41)");
            expect(!proc.getKeyboardState().isNoteOn(1, 41));
        }

        beginTest("Overlapping LIFO priority, repeated NoteOn, preset/octave/focus/editor reopen & overflow isolation");
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 48000.0, 256);
            proc.prepareToPlay(48000.0, 256);
            setParamPlainValue(proc.apvts, "attack", 1.0f);
            setParamPlainValue(proc.apvts, "release", 20.0f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 0.0f);

            juce::AudioBuffer<float> buf(2, 256);
            juce::MidiBuffer dawMidi;
            uint64_t extVer = 0;
            std::array<uint64_t, 2> extMask {};

            // DAW holds 36, GUI adds 41, DAW also adds 41 & repeats 41
            dawMidi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.85f), 0);
            dawMidi.addEvent(juce::MidiMessage::noteOn(1, 41, 0.90f), 64);
            buf.clear();
            proc.processBlock(buf, dawMidi);
            dawMidi.clear();

            proc.getKeyboardState().noteOn(1, 41, 0.90f);
            buf.clear();
            proc.processBlock(buf, dawMidi);

            auto dftMagAtHz = [](const std::vector<float>& samples, double hz, double sampleRate)
            {
                double re = 0.0;
                double im = 0.0;
                const double w = 2.0 * juce::MathConstants<double>::pi * hz / sampleRate;
                for (size_t i = 0; i < samples.size(); ++i)
                {
                    const double theta = w * static_cast<double>(i);
                    re += static_cast<double>(samples[i]) * std::cos(theta);
                    im -= static_cast<double>(samples[i]) * std::sin(theta);
                }
                return std::sqrt(re * re + im * im);
            };

            // Repeated DAW NoteOn(41) + GUI NoteOff(41) -> 41 remains active in stack (~87.31 Hz, NOT 65.41 Hz)
            dawMidi.addEvent(juce::MidiMessage::noteOn(1, 41, 0.95f), 0);
            buf.clear();
            proc.processBlock(buf, dawMidi);
            dawMidi.clear();
            proc.getKeyboardState().noteOff(1, 41, 0.0f);
            std::vector<float> capture41;
            capture41.reserve(8 * 256);
            for (int b = 0; b < 8; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
                for (int s = 0; s < buf.getNumSamples(); ++s)
                    capture41.push_back(buf.getSample(0, s));
            }
            expectEquals(proc.getEnvelopeStateInt(), 2);
            expect(dftMagAtHz(capture41, 87.307, 48000.0) > 3.0 * dftMagAtHz(capture41, 65.406, 48000.0),
                   "After GUI NoteOff(41) with DAW Note 41 held, pitch must stay at Note 41 (~87.3 Hz)");

            // Test editor focusLost, octave switch, preset change & destroy while DAW holds 41 AND GUI holds 41
            {
                std::unique_ptr<juce::AudioProcessorEditor> baseEd(proc.createEditor());
                auto* ed = dynamic_cast<AllyBassAudioProcessorEditor*>(baseEd.get());
                expect(ed != nullptr);
                if (ed != nullptr)
                {
                    proc.getKeyboardState().noteOn(1, 41, 0.9f);
                    buf.clear();
                    proc.processBlock(buf, dawMidi);

                    ed->setCurrentOctaveSafe(3);
                    ed->focusLost(juce::Component::focusChangedDirectly);
                    proc.setCurrentProgram(2);
                    setParamPlainValue(proc.apvts, "release", 20.0f);

                    for (int b = 0; b < 10; ++b)
                    {
                        buf.clear();
                        proc.processBlock(buf, dawMidi);
                    }
                    proc.syncExternalMidiToKeyboardState(extVer, extMask);
                    expectEquals(proc.getEnvelopeStateInt(), 2,
                                 "Octave switch, focusLost & preset swap releasing GUI Note 41 must not kill DAW Note 41");
                    expect(proc.getKeyboardState().isNoteOn(1, 41),
                           "Visual key 41 must remain lit after GUI release while DAW holds 41");

                    // Queue overflow while DAW holds 41 + GUI taps 41 & 48 then releases GUI keys
                    for (int k = 0; k < 280; ++k)
                    {
                        const int n = (k % 2 == 0) ? 41 : 48;
                        proc.getKeyboardState().noteOn(1, n, 0.85f);
                        proc.getKeyboardState().noteOff(1, n, 0.0f);
                    }
                    for (int b = 0; b < 10; ++b)
                    {
                        buf.clear();
                        proc.processBlock(buf, dawMidi);
                    }
                    expectEquals(proc.getEnvelopeStateInt(), 2,
                                 "GUI queue overflow reconcile must not kill DAW-held Note 41");

                    baseEd.reset();
                    std::unique_ptr<juce::AudioProcessorEditor> reopened(proc.createEditor());
                    expect(reopened != nullptr);
                    expect(proc.getKeyboardState().isNoteOn(1, 41),
                           "Reopened editor must keep DAW-held Note 41 lit");
                }
            }

            // Release DAW 41 (falls back to DAW 36 ~65.41 Hz), then release DAW 36 -> Idle
            dawMidi.addEvent(juce::MidiMessage::noteOff(1, 41), 0);
            std::vector<float> capture36;
            capture36.reserve(8 * 256);
            for (int b = 0; b < 8; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
                dawMidi.clear();
                for (int s = 0; s < buf.getNumSamples(); ++s)
                    capture36.push_back(buf.getSample(0, s));
            }
            expectEquals(proc.getEnvelopeStateInt(), 2, "Releasing 41 must return to held DAW 36");
            expect(dftMagAtHz(capture36, 65.406, 48000.0) > 3.0 * dftMagAtHz(capture36, 87.307, 48000.0),
                   "Releasing DAW Note 41 must fall back in LIFO stack to DAW Note 36 (~65.4 Hz)");

            dawMidi.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
            for (int b = 0; b < 25; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
                dawMidi.clear();
            }
            expectEquals(proc.getEnvelopeStateInt(), 0, "Final DAW NoteOff(36) must release cleanly to Idle");

            // External CC 123 (AllNotesOff) while GUI holds Note 41 must NOT silence GUI-held Note 41
            proc.getKeyboardState().noteOn(1, 41, 0.90f);
            for (int b = 0; b < 6; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 2);
            dawMidi.addEvent(juce::MidiMessage::allNotesOff(1), 0);
            for (int b = 0; b < 8; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
                dawMidi.clear();
            }
            expectEquals(proc.getEnvelopeStateInt(), 2, "DAW AllNotesOff (CC 123) must not kill GUI-held Note 41");
            proc.getKeyboardState().noteOff(1, 41, 0.0f);
            for (int b = 0; b < 25; ++b)
            {
                buf.clear();
                proc.processBlock(buf, dawMidi);
            }
            expectEquals(proc.getEnvelopeStateInt(), 0, "Releasing GUI Note 41 after DAW AllNotesOff must stop cleanly");
        }
    }
};

static MidiSourceIsolationD1E1Tests midiSourceIsolationD1E1Tests;

} // namespace

int main(int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::String runMode = "all";
    for (int i = 1; i < argc; ++i)
    {
        const juce::String arg(argv[i]);
        if (arg == "--mode" && i + 1 < argc)
            runMode = juce::String(argv[++i]).toLowerCase();
        else if (arg.startsWith("--mode="))
            runMode = arg.fromFirstOccurrenceOf("=", false, false).toLowerCase();
    }

    const auto previewOutDir = juce::SystemStats::getEnvironmentVariable("ALLY_C4B_PREVIEW_DIR", {});
    if (previewOutDir.isNotEmpty())
    {
        juce::File dir(previewOutDir);
        dir.createDirectory();

        AllyBassAudioProcessor proc;
        proc.setPlayConfigDetails(0, 2, 44100.0, 256);
        proc.prepareToPlay(44100.0, 256);
        AllyPresets::applyPreset(proc.apvts, 0);
        setParamPlainValue(proc.apvts, "polderPunch", 0.35f);
        setParamPlainValue(proc.apvts, "masterVolume", 0.0f);

        juce::AudioBuffer<float> audio(2, 256);
        juce::MidiBuffer midi;
        midi.addEvent(juce::MidiMessage::noteOn(1, 36, 0.92f), 0);
        for (int b = 0; b < 8; ++b)
        {
            audio.clear();
            proc.processBlock(audio, midi);
            midi.clear();
        }

        std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
        ed->setSize(780, 580);

        auto* scope = findChildOfType<AllyGUI::NapWaveVisualizer>(*ed);
        auto* primary = findChildOfType<AllyGUI::PrimaryKnobsPanel>(*ed);
        auto* meter = primary != nullptr ? primary->getOutputMeter() : nullptr;

        if (scope != nullptr)
        {
            std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> liveBuf {};
            proc.getVisualizerData(liveBuf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
            scope->updateScopeWithSamples(liveBuf.data(),
                                          AllyGUI::NapWaveVisualizer::captureSampleCount,
                                          proc.isOverdriveActive(),
                                          proc.isMonoLockActive(),
                                          proc.getCurrentSampleRate());
        }
        if (meter != nullptr)
        {
            const auto metrics = proc.peekOutputMeterMetrics();
            for (int i = 0; i < 6; ++i)
                meter->advanceWithSampledLevels(metrics.peakLinear, metrics.rmsLinear, metrics.clipped, 0.022f);
        }

        auto save2x = [&](juce::Component& c, const juce::String& fileName)
        {
            const int rw = c.getWidth() * 2;
            const int rh = c.getHeight() * 2;
            juce::Image img(juce::Image::ARGB, rw, rh, true);
            {
                juce::Graphics g(img);
                g.addTransform(juce::AffineTransform::scale(2.0f));
                c.paintEntireComponent(g, true);
            }
            juce::PNGImageFormat png;
            auto targetFile = dir.getChildFile(fileName);
            targetFile.deleteFile();
            if (auto out = targetFile.createOutputStream())
                png.writeImageToStream(img, *out);
        };

        // 1. Full 780x580 Editor @ 2x with active C2 sub wave and live output meter
        save2x(*ed, "c4b_editor_preview_2x.png");

        // 2. Scope states comparison (Active Cyan vs Overdrive vs Silence/Bypassed) stacked cleanly @ 2x
        if (scope != nullptr)
        {
            save2x(*scope, "c4b_scope_closeup_2x.png");

            // Render Overdrive + Bypassed + Silent comparison sheet
            const int sw = scope->getWidth() * 2;
            const int sh = scope->getHeight() * 2;
            juce::Image statesSheet(juce::Image::ARGB, sw, sh * 3, true);
            juce::Graphics gs(statesSheet);

            // Row 0: Nominal active C2 + Mono Lock ON
            {
                gs.saveState();
                gs.addTransform(juce::AffineTransform::scale(2.0f));
                scope->paintEntireComponent(gs, true);
                gs.restoreState();
            }
            // Row 1: Overdrive ON + Mono Lock Bypassed
            {
                std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> odBuf {};
                proc.setPlayConfigDetails(0, 2, 44100.0, 256);
                setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
                setParamPlainValue(proc.apvts, "monoLock1300", 0.0f);
                midi.addEvent(juce::MidiMessage::noteOn(1, 41, 1.0f), 0);
                for (int b = 0; b < 6; ++b) { audio.clear(); proc.processBlock(audio, midi); midi.clear(); }
                proc.getVisualizerData(odBuf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
                scope->updateScopeWithSamples(odBuf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount, true, false, 44100.0);

                gs.saveState();
                gs.addTransform(juce::AffineTransform::translation(0.0f, static_cast<float>(sh))
                                    .scaled(1.0f));
                gs.addTransform(juce::AffineTransform::scale(2.0f));
                scope->paintEntireComponent(gs, true);
                gs.restoreState();
            }
            // Row 2: Complete Silence (resting calmly on -4.5m NAP datum)
            {
                std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> sil {};
                scope->updateScopeWithSamples(sil.data(), AllyGUI::NapWaveVisualizer::captureSampleCount, false, true, 44100.0);

                gs.saveState();
                gs.addTransform(juce::AffineTransform::translation(0.0f, static_cast<float>(sh * 2)));
                gs.addTransform(juce::AffineTransform::scale(2.0f));
                scope->paintEntireComponent(gs, true);
                gs.restoreState();
            }

            juce::PNGImageFormat png;
            auto sheetFile = dir.getChildFile("c4b_scope_states_2x.png");
            sheetFile.deleteFile();
            if (auto out = sheetFile.createOutputStream())
                png.writeImageToStream(statesSheet, *out);
        }

        // 3. Output knob + OutputLevelMeter closeup states (Silent / Low -24dB / Nominal -3dB / Clip +1dB) @ 2x
        if (primary != nullptr && meter != nullptr)
        {
            const int cw = 180;
            const int ch = primary->getHeight();
            juce::Image meterSheet(juce::Image::ARGB, cw * 4 * 2, ch * 2, true);
            juce::Graphics gm(meterSheet);
            gm.fillAll(AllyGUI::EngineeredDepthTheme::Colors::bg);

            const struct { float peak; float rms; bool clip; } states[4] = {
                { 0.0f, 0.0f, false },
                { juce::Decibels::decibelsToGain(-24.0f), juce::Decibels::decibelsToGain(-27.0f), false },
                { juce::Decibels::decibelsToGain(-3.0f),  juce::Decibels::decibelsToGain(-6.0f),  false },
                { juce::Decibels::decibelsToGain(1.2f),   0.86f, true }
            };

            const int cropX = juce::jmax(0, primary->getWidth() - cw);
            for (int col = 0; col < 4; ++col)
            {
                meter->reset();
                for (int step = 0; step < 6; ++step)
                    meter->advanceWithSampledLevels(states[col].peak, states[col].rms, states[col].clip, 0.022f);

                juce::Image strip(juce::Image::ARGB, primary->getWidth() * 2, ch * 2, true);
                {
                    juce::Graphics gs(strip);
                    gs.addTransform(juce::AffineTransform::scale(2.0f));
                    primary->paintEntireComponent(gs, true);
                }
                gm.drawImage(strip,
                             col * cw * 2, 0, cw * 2, ch * 2,
                             cropX * 2, 0, cw * 2, ch * 2);
            }

            juce::PNGImageFormat png;
            auto meterFile = dir.getChildFile("c4b_meter_states_2x.png");
            meterFile.deleteFile();
            if (auto out = meterFile.createOutputStream())
                png.writeImageToStream(meterSheet, *out);
        }

        return 0;
    }

    const auto c4cPreviewDir = juce::SystemStats::getEnvironmentVariable("ALLY_C4C_PREVIEW_DIR", {});
    if (c4cPreviewDir.isNotEmpty())
    {
        juce::File dir(c4cPreviewDir);
        dir.createDirectory();

        auto writePng = [](const juce::File& target, const juce::Image& image)
        {
            juce::PNGImageFormat png;
            target.deleteFile();
            if (auto out = target.createOutputStream())
                png.writeImageToStream(image, *out);
        };

        auto renderComponentImage = [](juce::Component& c, float scale)
        {
            const int rw = juce::roundToInt(static_cast<float>(c.getWidth()) * scale);
            const int rh = juce::roundToInt(static_cast<float>(c.getHeight()) * scale);
            juce::Image img(juce::Image::ARGB, rw, rh, true);
            {
                juce::Graphics g(img);
                g.addTransform(juce::AffineTransform::scale(scale));
                c.paintEntireComponent(g, true);
            }
            return img;
        };

        auto feedScopeAndMeter = [](AllyBassAudioProcessor& proc, AllyBassAudioProcessorEditor& ed, int midiNote)
        {
            juce::AudioBuffer<float> audio(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.92f), 0);
            for (int b = 0; b < 8; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                midi.clear();
            }

            std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> buf {};
            proc.getVisualizerData(buf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
            ed.getVisualizer().updateScopeWithSamples(buf.data(),
                                                      AllyGUI::NapWaveVisualizer::captureSampleCount,
                                                      proc.isOverdriveActive(),
                                                      proc.isMonoLockActive(),
                                                      proc.getCurrentSampleRate());

            if (auto* primary = ed.getPrimaryKnobsPanel())
            {
                if (auto* meter = primary->getOutputMeter())
                {
                    meter->reset();
                    const auto metrics = proc.peekOutputMeterMetrics();
                    for (int step = 0; step < 6; ++step)
                        meter->advanceWithSampledLevels(metrics.peakLinear, metrics.rmsLinear, metrics.clipped, 0.022f);
                }
            }
        };

        // --- A. Clean Nominal State (780x580 @ 2x + 1200x850 + Keyboard Closeup) ---
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);
            AllyPresets::applyPreset(proc.apvts, 0);
            setParamPlainValue(proc.apvts, "diepte", 0.65f);
            setParamPlainValue(proc.apvts, "havenDirt", 0.38f);
            setParamPlainValue(proc.apvts, "polderPunch", 0.42f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 40.0f);
            setParamPlainValue(proc.apvts, "masterVolume", -1.2f);
            setParamPlainValue(proc.apvts, "attack", 12.0f);
            setParamPlainValue(proc.apvts, "release", 280.0f);
            setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);

            std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
            ed->setSize(780, 580);
            feedScopeAndMeter(proc, *ed, 36);

            // Highlight C2 & F#2 actively on Engineered Depth keyboard (syncs MidiKeyboardComponent before paint)
            proc.getKeyboardState().noteOn(1, 36, 0.94f);
            proc.getKeyboardState().noteOn(1, 42, 0.85f);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();

            const auto img780_2x = renderComponentImage(*ed, 2.0f);
            writePng(dir.getChildFile("01_ALLY1300_UI_780x580_2x.png"), img780_2x);

            // 3. Keyboard + Octave + Footer closeup @ 2x (add D#2 key too for multi-note cyan test)
            proc.getKeyboardState().noteOn(1, 39, 0.90f);
            if (auto* kbPanel = ed->getKeyboardPanel())
            {
                kbPanel->getKeyboard().timerCallback();
                const auto fullWithDs2 = renderComponentImage(*ed, 2.0f);
                const int cropY = juce::jmax(0, (kbPanel->getY() - 8) * 2);
                const auto closeup = fullWithDs2.getClippedImage({ 0, cropY, fullWithDs2.getWidth(), fullWithDs2.getHeight() - cropY });
                writePng(dir.getChildFile("03_ALLY1300_Keyboard_Octave_Closeup_2x.png"), closeup);
            }

            // 2. Large scaled window (1200x850 @ 1x native)
            ed->setSize(1200, 850);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();
            writePng(dir.getChildFile("02_ALLY1300_UI_1200x850.png"), renderComponentImage(*ed, 1.0f));

            // 5. Master vs Live Comparison sheet if reference path provided
            const auto masterPath = juce::SystemStats::getEnvironmentVariable("ALLY_MASTER_CROP_PATH", {});
            if (masterPath.isNotEmpty() && juce::File(masterPath).existsAsFile())
            {
                const auto masterImg = juce::ImageFileFormat::loadFrom(juce::File(masterPath));
                if (masterImg.isValid() && masterImg.getHeight() > 0)
                {
                    ed->setSize(780, 580);
                    proc.getKeyboardState().allNotesOff(1);
                    proc.getKeyboardState().noteOn(1, 36, 0.94f);
                    if (auto* kbPanel = ed->getKeyboardPanel())
                        kbPanel->getKeyboard().timerCallback();
                    const auto liveImg = renderComponentImage(*ed, 2.0f);

                    const int targetH = 920;
                    const int pad = 28;
                    const int labelH = 44;
                    const int leftW  = juce::roundToInt(static_cast<double>(masterImg.getWidth())
                                                        * (static_cast<double>(targetH) / static_cast<double>(masterImg.getHeight())));
                    const int rightW = juce::roundToInt(static_cast<double>(liveImg.getWidth())
                                                        * (static_cast<double>(targetH) / static_cast<double>(liveImg.getHeight())));
                    const int sheetW = pad * 3 + leftW + rightW;
                    const int sheetH = pad * 2 + labelH + targetH;

                    juce::Image compSheet(juce::Image::ARGB, sheetW, sheetH, true);
                    juce::Graphics gc(compSheet);
                    gc.fillAll(AllyGUI::EngineeredDepthTheme::Colors::bg);

                    gc.setFont(AllyGUI::EngineeredDepthTheme::makeUiFont(18.0f, true));
                    gc.setColour(AllyGUI::EngineeredDepthTheme::Colors::textSecondary);
                    gc.drawText("APPROVED MASTER UI CROP (REFERENCE)",
                                pad, pad, leftW, labelH - 8, juce::Justification::centredLeft);

                    gc.setColour(AllyGUI::EngineeredDepthTheme::Colors::cyan);
                    gc.drawText("LIVE COMPILED C++ / JUCE UI (STAP C.4C)",
                                pad * 2 + leftW, pad, rightW, labelH - 8, juce::Justification::centredLeft);

                    gc.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                    gc.drawImage(masterImg, pad, pad + labelH, leftW, targetH,
                                 0, 0, masterImg.getWidth(), masterImg.getHeight());
                    gc.drawImage(liveImg, pad * 2 + leftW, pad + labelH, rightW, targetH,
                                 0, 0, liveImg.getWidth(), liveImg.getHeight());

                    writePng(dir.getChildFile("05_ALLY1300_Master_vs_Live_Comparison.png"), compSheet);
                }
            }
        }

        // --- B. Active Overdrive & Alternate Octave State (780x580 @ 2x) ---
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);
            AllyPresets::applyPreset(proc.apvts, 3); // distinct dark/aggressive preset
            setParamPlainValue(proc.apvts, "stadsdeel", 2.0f); // Poort 1363
            setParamPlainValue(proc.apvts, "diepte", 0.78f);
            setParamPlainValue(proc.apvts, "havenDirt", 0.72f);
            setParamPlainValue(proc.apvts, "polderPunch", 0.68f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 85.0f);
            setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
            setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);
            setParamPlainValue(proc.apvts, "masterVolume", -0.5f);

            std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
            ed->setSize(780, 580);
            ed->setCurrentOctaveSafe(1); // C1
            feedScopeAndMeter(proc, *ed, 38);

            proc.getKeyboardState().allNotesOff(1);
            proc.getKeyboardState().noteOn(1, 24, 0.95f); // C1
            proc.getKeyboardState().noteOn(1, 27, 0.90f); // D#1
            proc.getKeyboardState().noteOn(1, 31, 0.88f); // G1
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();

            writePng(dir.getChildFile("04_ALLY1300_Active_State_780x580_2x.png"),
                     renderComponentImage(*ed, 2.0f));
        }

        return 0;
    }

    const auto d2PreviewDir = juce::SystemStats::getEnvironmentVariable("ALLY_D2_PREVIEW_DIR", {});
    if (d2PreviewDir.isNotEmpty())
    {
        juce::File dir(d2PreviewDir);
        dir.createDirectory();

        auto writePng = [](const juce::File& target, const juce::Image& image)
        {
            juce::PNGImageFormat png;
            target.deleteFile();
            if (auto out = target.createOutputStream())
                png.writeImageToStream(image, *out);
        };

        auto renderComponentImage = [](juce::Component& c, float scale)
        {
            const int rw = juce::roundToInt(static_cast<float>(c.getWidth()) * scale);
            const int rh = juce::roundToInt(static_cast<float>(c.getHeight()) * scale);
            juce::Image img(juce::Image::ARGB, rw, rh, true);
            {
                juce::Graphics g(img);
                g.addTransform(juce::AffineTransform::scale(scale));
                c.paintEntireComponent(g, true);
            }
            return img;
        };

        auto feedScopeAndMeter = [](AllyBassAudioProcessor& proc, AllyBassAudioProcessorEditor& ed, int midiNote)
        {
            juce::AudioBuffer<float> audio(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.92f), 0);
            for (int b = 0; b < 8; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                midi.clear();
            }

            std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> buf {};
            proc.getVisualizerData(buf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
            ed.getVisualizer().updateScopeWithSamples(buf.data(),
                                                      AllyGUI::NapWaveVisualizer::captureSampleCount,
                                                      proc.isOverdriveActive(),
                                                      proc.isMonoLockActive(),
                                                      proc.getCurrentSampleRate());

            if (auto* primary = ed.getPrimaryKnobsPanel())
            {
                if (auto* meter = primary->getOutputMeter())
                {
                    meter->reset();
                    const auto metrics = proc.peekOutputMeterMetrics();
                    for (int step = 0; step < 6; ++step)
                        meter->advanceWithSampledLevels(metrics.peakLinear, metrics.rmsLinear, metrics.clipped, 0.022f);
                }
            }
        };

        // 1. Nominal Master-calibrated state (matches APPROVED_MASTER angles & resting white keys + C2 lit closeup)
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);
            AllyPresets::applyPreset(proc.apvts, 0);
            setParamPlainValue(proc.apvts, "diepte", 0.50f);
            setParamPlainValue(proc.apvts, "polderPunch", 0.0f);
            setParamPlainValue(proc.apvts, "havenDirt", 0.15f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 40.0f);
            setParamPlainValue(proc.apvts, "masterVolume", 0.0f);
            setParamPlainValue(proc.apvts, "attack", 8.0f);
            setParamPlainValue(proc.apvts, "release", 250.0f);
            setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
            setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);

            std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
            ed->setSize(780, 580);
            feedScopeAndMeter(proc, *ed, 36);

            // Clean unpressed steel-grey/titanium keys for Full UI (matching reference crop comparison)
            proc.getKeyboardState().allNotesOff(1);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();

            const auto full780_2x = renderComponentImage(*ed, 2.0f);
            writePng(dir.getChildFile("D2_01_ALLY1300_Full_UI_780x580_2x.png"), full780_2x);

            // Component closeups @ 2x
            if (auto* hdr = ed->getHeaderPanel())
                writePng(dir.getChildFile("D2_04_ALLY1300_Header_Map_Closeup_2x.png"), renderComponentImage(*hdr, 2.0f));
            if (auto* primary = ed->getPrimaryKnobsPanel())
                writePng(dir.getChildFile("D2_03_ALLY1300_Knobs_Chassis_Closeup_2x.png"), renderComponentImage(*primary, 2.0f));

            // Keyboard closeup with active C2 + D#2 cyan key lighting so both resting keys and active keys are verified
            proc.getKeyboardState().noteOn(1, 36, 0.94f);
            proc.getKeyboardState().noteOn(1, 39, 0.90f);
            if (auto* kbPanel = ed->getKeyboardPanel())
            {
                kbPanel->getKeyboard().timerCallback();
                const auto fullActiveKb = renderComponentImage(*ed, 2.0f);
                const int cropY = juce::jmax(0, (kbPanel->getY() - 8) * 2);
                const auto kbCloseup = fullActiveKb.getClippedImage({ 0, cropY, fullActiveKb.getWidth(), fullActiveKb.getHeight() - cropY });
                writePng(dir.getChildFile("D2_02_ALLY1300_Keyboard_Closeup_2x.png"), kbCloseup);
            }
            proc.getKeyboardState().allNotesOff(1);

            // 1200x850 native scale
            ed->setSize(1200, 850);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();
            writePng(dir.getChildFile("D2_06_ALLY1300_UI_1200x850.png"), renderComponentImage(*ed, 1.0f));

            // Side-by-side Master vs D.2 Comparison sheet
            const auto masterPath = juce::SystemStats::getEnvironmentVariable("ALLY_MASTER_CROP_PATH", {});
            if (masterPath.isNotEmpty() && juce::File(masterPath).existsAsFile())
            {
                const auto masterImg = juce::ImageFileFormat::loadFrom(juce::File(masterPath));
                if (masterImg.isValid() && masterImg.getHeight() > 0)
                {
                    const int targetH = 920;
                    const int pad = 28;
                    const int labelH = 44;
                    const int leftW  = juce::roundToInt(static_cast<double>(masterImg.getWidth())
                                                        * (static_cast<double>(targetH) / static_cast<double>(masterImg.getHeight())));
                    const int rightW = juce::roundToInt(static_cast<double>(full780_2x.getWidth())
                                                        * (static_cast<double>(targetH) / static_cast<double>(full780_2x.getHeight())));
                    const int sheetW = pad * 3 + leftW + rightW;
                    const int sheetH = pad * 2 + labelH + targetH;

                    juce::Image compSheet(juce::Image::ARGB, sheetW, sheetH, true);
                    juce::Graphics gc(compSheet);
                    gc.fillAll(AllyGUI::EngineeredDepthTheme::Colors::bg);

                    gc.setFont(AllyGUI::EngineeredDepthTheme::makeUiFont(18.0f, true));
                    gc.setColour(AllyGUI::EngineeredDepthTheme::Colors::textSecondary);
                    gc.drawText("APPROVED MASTER UI CROP (REFERENCE)",
                                pad, pad, leftW, labelH - 8, juce::Justification::centredLeft);

                    gc.setColour(AllyGUI::EngineeredDepthTheme::Colors::cyan);
                    gc.drawText("LIVE COMPILED C++ / JUCE UI (CORRECTIERONDE D.2)",
                                pad * 2 + leftW, pad, rightW, labelH - 8, juce::Justification::centredLeft);

                    gc.setImageResamplingQuality(juce::Graphics::highResamplingQuality);
                    gc.drawImage(masterImg, pad, pad + labelH, leftW, targetH,
                                 0, 0, masterImg.getWidth(), masterImg.getHeight());
                    gc.drawImage(full780_2x, pad * 2 + leftW, pad + labelH, rightW, targetH,
                                 0, 0, full780_2x.getWidth(), full780_2x.getHeight());

                    writePng(dir.getChildFile("D2_05_ALLY1300_Master_vs_D2_Comparison.png"), compSheet);
                }
            }
        }

        // 2. Alternate Active High-Dirt State (780x580 @ 2x)
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);
            AllyPresets::applyPreset(proc.apvts, 3);
            setParamPlainValue(proc.apvts, "stadsdeel", 2.0f);
            setParamPlainValue(proc.apvts, "diepte", 0.78f);
            setParamPlainValue(proc.apvts, "havenDirt", 0.72f);
            setParamPlainValue(proc.apvts, "polderPunch", 0.68f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 85.0f);
            setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
            setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);
            setParamPlainValue(proc.apvts, "masterVolume", -0.5f);

            std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
            ed->setSize(780, 580);
            ed->setCurrentOctaveSafe(1);
            feedScopeAndMeter(proc, *ed, 38);

            proc.getKeyboardState().allNotesOff(1);
            proc.getKeyboardState().noteOn(1, 24, 0.95f);
            proc.getKeyboardState().noteOn(1, 27, 0.90f);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();

            writePng(dir.getChildFile("D2_07_ALLY1300_Active_State_780x580_2x.png"),
                     renderComponentImage(*ed, 2.0f));
        }

        return 0;
    }

    const auto d1e2ShotDir = juce::SystemStats::getEnvironmentVariable("ALLY_D1E2_SCREENSHOT_DIR", {});
    if (d1e2ShotDir.isNotEmpty())
    {
        juce::File dir(d1e2ShotDir);
        dir.createDirectory();

        auto writePng = [](const juce::File& target, const juce::Image& image)
        {
            juce::PNGImageFormat png;
            target.deleteFile();
            if (auto out = target.createOutputStream())
                png.writeImageToStream(image, *out);
        };

        auto renderImg = [](juce::Component& c, float scale)
        {
            const int rw = juce::roundToInt(static_cast<float>(c.getWidth()) * scale);
            const int rh = juce::roundToInt(static_cast<float>(c.getHeight()) * scale);
            juce::Image img(juce::Image::ARGB, rw, rh, true);
            {
                juce::Graphics g(img);
                g.addTransform(juce::AffineTransform::scale(scale));
                c.paintEntireComponent(g, true);
            }
            return img;
        };

        auto feedActiveSignal = [](AllyBassAudioProcessor& proc, AllyBassAudioProcessorEditor& ed, int midiNote, int blocks)
        {
            juce::AudioBuffer<float> audio(2, 256);
            juce::MidiBuffer midi;
            midi.addEvent(juce::MidiMessage::noteOn(1, midiNote, 0.92f), 0);
            for (int b = 0; b < blocks; ++b)
            {
                audio.clear();
                proc.processBlock(audio, midi);
                midi.clear();
            }

            if (auto* scope = findChildOfType<AllyGUI::NapWaveVisualizer>(ed))
            {
                std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> liveBuf {};
                proc.getVisualizerData(liveBuf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
                scope->updateScopeWithSamples(liveBuf.data(),
                                              AllyGUI::NapWaveVisualizer::captureSampleCount,
                                              proc.isOverdriveActive(),
                                              proc.isMonoLockActive(),
                                              proc.getCurrentSampleRate());
            }
            if (auto* primary = ed.getPrimaryKnobsPanel())
            {
                if (auto* meter = primary->getOutputMeter())
                {
                    const auto metrics = proc.peekOutputMeterMetrics();
                    for (int i = 0; i < 6; ++i)
                        meter->advanceWithSampledLevels(metrics.peakLinear, metrics.rmsLinear, metrics.clipped, 0.022f);
                }
            }
        };

        // A. Standard active state (Preset 0, 780x580 & 1200x850, plus component closeups)
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);
            AllyPresets::applyPreset(proc.apvts, 0);
            setParamPlainValue(proc.apvts, "polderPunch", 0.35f);
            setParamPlainValue(proc.apvts, "masterVolume", 0.0f);

            std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
            ed->setSize(780, 580);
            feedActiveSignal(proc, *ed, 36, 40);

            proc.getKeyboardState().noteOn(1, 36, 0.94f);
            proc.getKeyboardState().noteOn(1, 39, 0.90f);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();

            writePng(dir.getChildFile("01_full_ui_780x580_1x.png"), renderImg(*ed, 1.0f));
            writePng(dir.getChildFile("02_full_ui_780x580_2x.png"), renderImg(*ed, 2.0f));

            if (auto* primary = ed->getPrimaryKnobsPanel())
            {
                writePng(dir.getChildFile("05_knobs_all_5_2x.png"), renderImg(*primary, 2.0f));
                if (auto* meter = primary->getOutputMeter())
                    writePng(dir.getChildFile("09_output_meter_2x.png"), renderImg(*meter, 2.0f));
            }
            if (auto* header = ed->getHeaderPanel())
                writePng(dir.getChildFile("07_header_almere_2x.png"), renderImg(*header, 2.0f));
            if (auto* scope = findChildOfType<AllyGUI::NapWaveVisualizer>(*ed))
                writePng(dir.getChildFile("08_scope_active_2x.png"), renderImg(*scope, 2.0f));
            if (auto* kbPanel = ed->getKeyboardPanel())
                writePng(dir.getChildFile("10_keyboard_active_2x.png"), renderImg(*kbPanel, 2.0f));

            ed->setSize(1200, 850);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();
            writePng(dir.getChildFile("03_full_ui_1200x850_1x.png"), renderImg(*ed, 1.0f));
            writePng(dir.getChildFile("04_full_ui_1200x850_2x.png"), renderImg(*ed, 2.0f));
        }

        // B. Haven Dirt active state (780x580 @ 2x)
        {
            AllyBassAudioProcessor proc;
            proc.setPlayConfigDetails(0, 2, 44100.0, 256);
            proc.prepareToPlay(44100.0, 256);
            AllyPresets::applyPreset(proc.apvts, 3);
            setParamPlainValue(proc.apvts, "stadsdeel", 2.0f);
            setParamPlainValue(proc.apvts, "diepte", 0.78f);
            setParamPlainValue(proc.apvts, "havenDirt", 0.72f);
            setParamPlainValue(proc.apvts, "polderPunch", 0.68f);
            setParamPlainValue(proc.apvts, "waterwijkGlide", 85.0f);
            setParamPlainValue(proc.apvts, "overdrive036", 1.0f);
            setParamPlainValue(proc.apvts, "monoLock1300", 1.0f);
            setParamPlainValue(proc.apvts, "masterVolume", -0.5f);

            std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
            ed->setSize(780, 580);
            ed->setCurrentOctaveSafe(1);
            feedActiveSignal(proc, *ed, 38, 38);

            proc.getKeyboardState().allNotesOff(1);
            proc.getKeyboardState().noteOn(1, 24, 0.95f);
            proc.getKeyboardState().noteOn(1, 27, 0.90f);
            if (auto* kbPanel = ed->getKeyboardPanel())
                kbPanel->getKeyboard().timerCallback();

            writePng(dir.getChildFile("06_haven_dirt_active_780x580_2x.png"), renderImg(*ed, 2.0f));
        }

        return 0;
    }

    if (runMode == "perf")
    {
        auto threadCpuMs = []() -> double
        {
           #if JUCE_MAC || JUCE_LINUX
            struct timespec ts {};
            if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
                return static_cast<double>(ts.tv_sec) * 1000.0 + static_cast<double>(ts.tv_nsec) * 1.0e-6;
           #endif
            return -1.0;
        };

        auto measureScenario = [&](const juce::String& name,
                                   int warmupFrames,
                                   int measuredFrames,
                                   auto&& frameFn)
        {
            std::vector<double> times;
            times.reserve(static_cast<size_t>(measuredFrames));

            for (int i = 0; i < warmupFrames; ++i)
                frameFn(i);

            const double cpuStart = threadCpuMs();
            for (int i = 0; i < measuredFrames; ++i)
            {
                const double t0 = juce::Time::getMillisecondCounterHiRes();
                frameFn(warmupFrames + i);
                const double t1 = juce::Time::getMillisecondCounterHiRes();
                times.push_back(t1 - t0);
            }
            const double cpuEnd = threadCpuMs();

            std::sort(times.begin(), times.end());
            const double median = times[times.size() / 2];
            const size_t p95Idx = static_cast<size_t>(std::min(times.size() - 1,
                static_cast<size_t>(std::ceil(static_cast<double>(times.size()) * 0.95)) - 1));
            const double p95 = times[p95Idx];
            double sum = 0.0;
            for (double v : times)
                sum += v;
            const double mean = sum / static_cast<double>(times.size());
            const double cpuPerFrame = (cpuStart >= 0.0 && cpuEnd >= cpuStart)
                ? (cpuEnd - cpuStart) / static_cast<double>(measuredFrames)
                : -1.0;

            std::cout << "[PERF] " << name
                      << " | median=" << juce::String(median, 3) << " ms"
                      << " | p95=" << juce::String(p95, 3) << " ms"
                      << " | mean=" << juce::String(mean, 3) << " ms"
                      << " | cpu/frame=" << juce::String(cpuPerFrame, 3) << " ms"
                      << std::endl;
            return median;
        };

        auto renderIntoImage = [](juce::Component& comp, juce::Image& backing, float scale)
        {
            juce::Graphics g(backing);
            g.addTransform(juce::AffineTransform::scale(scale));
            comp.paintEntireComponent(g, true);
        };

        std::cout << "=== ALLY1300 D.1E.2 PERFORMANCE SUITE (--mode perf) ===" << std::endl;

        AllyBassAudioProcessor proc;
        proc.setPlayConfigDetails(0, 2, 48000.0, 256);
        proc.prepareToPlay(48000.0, 256);
        AllyPresets::applyPreset(proc.apvts, 3);
        setParamPlainValue(proc.apvts, "havenDirt", 0.65f);
        setParamPlainValue(proc.apvts, "overdrive036", 1.0f);

        juce::AudioBuffer<float> audioBuf(2, 256);
        juce::MidiBuffer midiBuf;
        midiBuf.addEvent(juce::MidiMessage::noteOn(1, 36, 0.92f), 0);
        for (int b = 0; b < 40; ++b)
        {
            audioBuf.clear();
            proc.processBlock(audioBuf, midiBuf);
            midiBuf.clear();
        }

        std::unique_ptr<AllyBassAudioProcessorEditor> ed = std::make_unique<AllyBassAudioProcessorEditor>(proc);
        auto* header    = ed->getHeaderPanel();
        auto& scope     = ed->getVisualizer();
        auto* primary   = ed->getPrimaryKnobsPanel();
        auto* meter     = (primary != nullptr) ? primary->getOutputMeter() : nullptr;
        auto* secondary = ed->getSecondaryDspPanel();
        auto* kbPanel   = ed->getKeyboardPanel();

        std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> liveScopeBuf {};
        auto pump45HzFrame = [&](int tickIdx)
        {
            audioBuf.clear();
            proc.processBlock(audioBuf, midiBuf);
            if ((tickIdx & 1) == 0)
            {
                proc.getVisualizerData(liveScopeBuf.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
                scope.updateScopeWithSamples(liveScopeBuf.data(),
                                             AllyGUI::NapWaveVisualizer::captureSampleCount,
                                             proc.isOverdriveActive(),
                                             proc.isMonoLockActive(),
                                             proc.getCurrentSampleRate());
                if (meter != nullptr)
                {
                    const auto m = proc.consumeOutputMeterMetrics();
                    meter->advanceWithSampledLevels(m.peakLinear, m.rmsLinear, m.clipped, 0.022f);
                }
            }
            if (kbPanel != nullptr)
                kbPanel->getKeyboard().timerCallback();
        };
        pump45HzFrame(0);

        for (int run = 1; run <= 2; ++run)
        {
            std::cout << "--- Run #" << run << " ---" << std::endl;

            struct SizeScale { int w; int h; float s; const char* tag; };
            const SizeScale matrix[] = {
                {  780,  580, 1.0f, "780x580 @ 1x" },
                {  780,  580, 2.0f, "780x580 @ 2x" },
                { 1200,  850, 1.0f, "1200x850 @ 1x" },
                { 1200,  850, 2.0f, "1200x850 @ 2x" }
            };

            for (const auto& cfg : matrix)
            {
                ed->setSize(cfg.w, cfg.h);
                pump45HzFrame(0);
                juce::Image backing(juce::Image::ARGB,
                                    juce::roundToInt(static_cast<float>(cfg.w) * cfg.s),
                                    juce::roundToInt(static_cast<float>(cfg.h) * cfg.s),
                                    true);

                // Idle full editor
                measureScenario(juce::String("FullEditor Idle ") + cfg.tag, 12, 60, [&](int)
                {
                    renderIntoImage(*ed, backing, cfg.s);
                });

                // Active full editor with 45Hz telemetry updates
                measureScenario(juce::String("FullEditor Active 45Hz ") + cfg.tag, 12, 60, [&](int f)
                {
                    pump45HzFrame(f);
                    renderIntoImage(*ed, backing, cfg.s);
                });

                if (run == 1)
                {
                    const size_t cacheBytes = ed->getEstimatedGraphicCacheBytes();
                    std::cout << "[CACHE] " << cfg.tag
                              << " estimated_cache_kb=" << juce::String(static_cast<double>(cacheBytes) / 1024.0, 1)
                              << " KB (" << cacheBytes << " B)" << std::endl;
                }

                // Scope + Meter active @ 45Hz
                juce::Image scopeImg(juce::Image::ARGB,
                                     juce::jmax(1, juce::roundToInt(static_cast<float>(scope.getWidth()) * cfg.s)),
                                     juce::jmax(1, juce::roundToInt(static_cast<float>(scope.getHeight()) * cfg.s)),
                                     true);
                juce::Image meterImg(juce::Image::ARGB,
                                     meter != nullptr ? juce::jmax(1, juce::roundToInt(static_cast<float>(meter->getWidth()) * cfg.s)) : 16,
                                     meter != nullptr ? juce::jmax(1, juce::roundToInt(static_cast<float>(meter->getHeight()) * cfg.s)) : 100,
                                     true);
                measureScenario(juce::String("Scope+Meter Active ") + cfg.tag, 12, 60, [&](int f)
                {
                    pump45HzFrame(f);
                    renderIntoImage(scope, scopeImg, cfg.s);
                    if (meter != nullptr)
                        renderIntoImage(*meter, meterImg, cfg.s);
                });
            }

            // Per-component hotspot breakdown on primary 780x580 @ 1x
            ed->setSize(780, 580);
            pump45HzFrame(0);
            auto profileComp = [&](const juce::String& label, juce::Component& c)
            {
                juce::Image cImg(juce::Image::ARGB, juce::jmax(1, c.getWidth()), juce::jmax(1, c.getHeight()), true);
                measureScenario(juce::String("Hotspot[780x580 1x] ") + label, 12, 60, [&](int)
                {
                    renderIntoImage(c, cImg, 1.0f);
                });
            };

            if (header != nullptr)    profileComp("HeaderPanel", *header);
            profileComp("NapWaveVisualizer (Scope)", scope);
            if (primary != nullptr)
            {
                profileComp("PrimaryKnobsPanel (All 5 knobs + chassis + meter)", *primary);
                for (int k = 0; k < AllyGUI::PrimaryKnobsPanel::knobCount; ++k)
                    if (auto* knob = primary->getKnob(k))
                        profileComp(juce::String("  Single DepthRingKnob[") + juce::String(k) + "]", *knob);
                if (meter != nullptr)
                    profileComp("  OutputLevelMeter", *meter);
            }
            if (secondary != nullptr) profileComp("SecondaryDspPanel (with children)", *secondary);
            if (kbPanel != nullptr)   profileComp("KeyboardPanel (with 37 keys)", *kbPanel);

            // Knob drag simulation (Diepte, Haven Dirt, Output across 60 steps)
            if (primary != nullptr)
            {
                juce::Image primImg(juce::Image::ARGB, primary->getWidth(), primary->getHeight(), true);
                measureScenario("KnobDrag(Diepte+HavenDirt+Output) PrimaryPanel paint", 12, 60, [&](int f)
                {
                    const float norm = 0.1f + 0.8f * (static_cast<float>(f % 30) / 29.0f);
                    setParamPlainValue(proc.apvts, "diepte", norm);
                    setParamPlainValue(proc.apvts, "havenDirt", norm);
                    setParamPlainValue(proc.apvts, "masterVolume", -24.0f + norm * 30.0f);
                    renderIntoImage(*primary, primImg, 1.0f);
                });
            }

            // Preset switching + full editor repaint
            {
                juce::Image edImg(juce::Image::ARGB, 780, 580, true);
                measureScenario("PresetSwitch + FullEditor 780x580 1x", 12, 60, [&](int f)
                {
                    proc.setCurrentProgram(f % proc.getNumPrograms());
                    renderIntoImage(*ed, edImg, 1.0f);
                });
            }

            // Window resize + repaint across 680x500 / 780x580 / 1200x850
            {
                const juce::Point<int> resizes[] = { { 680, 500 }, { 780, 580 }, { 1200, 850 } };
                juce::Image resizeBacking(juce::Image::ARGB, 1200, 850, true);
                measureScenario("WindowResize + FullEditor 1x", 12, 60, [&](int f)
                {
                    const auto sz = resizes[f % 3];
                    ed->setSize(sz.x, sz.y);
                    juce::Graphics g(resizeBacking);
                    ed->paintEntireComponent(g, true);
                });
                ed->setSize(780, 580);
            }

            // Audio thread CPU measurement with active GUI open vs headless
            {
                proc.prepareToPlay(48000.0, 256);
                std::atomic<bool> stopGui { false };
                std::thread fakeGuiReader([&]()
                {
                    std::array<float, AllyGUI::NapWaveVisualizer::captureSampleCount> tmp {};
                    while (!stopGui.load(std::memory_order_acquire))
                    {
                        proc.getVisualizerData(tmp.data(), AllyGUI::NapWaveVisualizer::captureSampleCount);
                        (void) proc.consumeOutputMeterMetrics();
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    }
                });

                midiBuf.addEvent(juce::MidiMessage::noteOn(1, 36, 0.92f), 0);
                measureScenario("AudioThread processBlock(256 @ 48kHz) w/ GUI telemetry", 20, 240, [&](int)
                {
                    audioBuf.clear();
                    proc.processBlock(audioBuf, midiBuf);
                    midiBuf.clear();
                });
                midiBuf.addEvent(juce::MidiMessage::noteOff(1, 36), 0);
                audioBuf.clear();
                proc.processBlock(audioBuf, midiBuf);
                midiBuf.clear();

                stopGui.store(true, std::memory_order_release);
                fakeGuiReader.join();
            }
        }

        return 0;
    }

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);

    if (runMode == "concurrency")
    {
        gEnforceWallClockRenderBudgets = false;
        juce::Array<juce::UnitTest*> concurrencyUnits;
        concurrencyUnits.add(&concurrencyRealtimeSafetyD1CTests);
        concurrencyUnits.add(&scopeAndOutputMeterTests);
        concurrencyUnits.add(&midiSourceIsolationD1E1Tests);
        runner.runTests(concurrencyUnits);
    }
    else if (runMode == "functional")
    {
        gEnforceWallClockRenderBudgets = false;
        runner.runTestsInCategory("AllyBass");
    }
    else
    {
        gEnforceWallClockRenderBudgets = true;
        runner.runTestsInCategory("AllyBass");
    }

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult(i))
            totalFailures += result->failures;
    }

    return totalFailures == 0 ? 0 : 1;
}
