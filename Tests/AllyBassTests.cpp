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

#include <atomic>
#include <cmath>
#include <thread>
#include <vector>

namespace
{

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

                // Attack parameter clamps below 1.0 ms (e.g. preset 5 requests 0.8 ms -> 1.0 ms)
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

                std::atomic<bool> stopFlag { false };
                std::atomic<bool> consumerSawBadFloat { false };
                std::atomic<uint64_t> consumerReads { 0 };

                std::thread consumer([&]()
                {
                    std::array<float, 1536> localScope {};
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
                        consumerReads.fetch_add(1, std::memory_order_relaxed);
                    }
                });

                juce::AudioBuffer<float> audio(2, 256);
                juce::MidiBuffer midi;
                for (int b = 0; b < 120; ++b)
                {
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
                }

                stopFlag.store(true, std::memory_order_release);
                consumer.join();

                expect(!consumerSawBadFloat.load(), "Concurrent consumer must never observe NaN, Inf, or torn values");
                expect(consumerReads.load() > 0, "Concurrent consumer thread must execute reads");
                expectEquals(static_cast<int>(proc.getVisualizerWriteCounter()), 120);
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
                    logMessage("Scope + OutputLevelMeter 1x software render avg: "
                               + juce::String(avgMsPerFrame, 3) + " ms/frame");
                    expect(avgMsPerFrame < 15.0,
                           "Average software render time for Scope + OutputMeter must remain smooth under 15 ms/frame");
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
                logMessage("Full editor (780x580) 1x software render avg: " + juce::String(avgMs, 3) + " ms/frame");
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

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

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

    juce::UnitTestRunner runner;
    runner.setAssertOnFailure(false);
    runner.runTestsInCategory("AllyBass");

    int totalFailures = 0;
    for (int i = 0; i < runner.getNumResults(); ++i)
    {
        if (const auto* result = runner.getResult(i))
            totalFailures += result->failures;
    }

    return totalFailures == 0 ? 0 : 1;
}
