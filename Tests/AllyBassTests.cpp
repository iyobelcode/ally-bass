#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PresetManager.h"
#include "DSP/WaterwijkGlide.h"
#include "DSP/PolderTransient.h"
#include "GUI/DepthRingKnob.h"
#include "GUI/EngineeredDepthIcons.h"
#include "GUI/EngineeredDepthTheme.h"
#include "GUI/HeavyKnob.h"
#include "GUI/PrimaryKnobsPanel.h"

#include <cmath>
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
                AllyGUI::EngineeredDepthIcons::IconId::OutputBars
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

                // Legacy compact knobs Attack & Release must remain intact outside the primary panel
                expect(findSliderByName(*editor, "attack") != nullptr, "Attack knob must remain");
                expect(findSliderByName(*editor, "release") != nullptr, "Release knob must remain");

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
    }
};

static ParameterAndStateTests parameterAndStateTests;
static MidiGlideAndAudioTests midiGlideAndAudioTests;
static PolderPunchTests polderPunchTests;
static EngineeredDepthTests engineeredDepthTests;

} // namespace

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;
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
