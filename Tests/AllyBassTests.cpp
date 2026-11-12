#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PresetManager.h"
#include "DSP/WaterwijkGlide.h"
#include "DSP/PolderTransient.h"

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

static ParameterAndStateTests parameterAndStateTests;
static MidiGlideAndAudioTests midiGlideAndAudioTests;
static PolderPunchTests polderPunchTests;

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
