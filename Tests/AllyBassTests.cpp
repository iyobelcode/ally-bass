#include <juce_core/juce_core.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PresetManager.h"
#include "DSP/WaterwijkGlide.h"

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

static ParameterAndStateTests parameterAndStateTests;
static MidiGlideAndAudioTests midiGlideAndAudioTests;

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
