#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PresetManager.h"

AllyBassAudioProcessor::AllyBassAudioProcessor()
#ifndef JucePlugin_PreferredChannelConfigurations
     : AudioProcessor(BusesProperties()
                      #if ! JucePlugin_IsMidiEffect
                       #if ! JucePlugin_IsSynth
                        .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
                       #endif
                        .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
                      #endif
                       ),
       apvts(*this, nullptr, "Parameters", createParameterLayout())
#else
     : apvts(*this, nullptr, "Parameters", createParameterLayout())
#endif
{
    visualizerBuffer.fill(0.0f);
}

AllyBassAudioProcessor::~AllyBassAudioProcessor() = default;

juce::AudioProcessorValueTreeState::ParameterLayout AllyBassAudioProcessor::createParameterLayout()
{
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> params;

    // 1. DIEPTE (Sub/Pitch Offset & -4.5m NAP focus)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"diepte", 1},
        "DIEPTE",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.50f,
        juce::AudioParameterFloatAttributes().withLabel("NAP Focus")));

    // 2. POLDER PUNCH (Attack Knock - Default 0.0 to eliminate unwanted kick)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"polderPunch", 1},
        "POLDER PUNCH",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.00f,
        juce::AudioParameterFloatAttributes().withLabel("Knock")));

    // 3. ATTACK (Aanslag / Fade-in)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1},
        "ATTACK",
        juce::NormalisableRange<float>(0.2f, 300.0f, 0.1f, 0.35f),
        5.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // 4. VERVAL (Decay / Sustain)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"verval", 1},
        "VERVAL",
        juce::NormalisableRange<float>(0.05f, 5.0f, 0.01f, 0.40f),
        1.40f,
        juce::AudioParameterFloatAttributes().withLabel("s")));

    // 5. RELEASE (Loslaat / Tail Fade)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"release", 1},
        "RELEASE",
        juce::NormalisableRange<float>(10.0f, 2000.0f, 1.0f, 0.35f),
        150.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // 6. HAVEN DIRT (Drive / Harmonics)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"havenDirt", 1},
        "HAVEN DIRT",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.15f,
        juce::AudioParameterFloatAttributes().withLabel("Drive")));

    // 7. WEERWATER GLIDE (Portamento Time)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"weerwaterGlide", 1},
        "WEERWATER GLIDE",
        juce::NormalisableRange<float>(0.0f, 500.0f, 0.5f, 0.45f),
        40.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // 8. STADSDEEL (4-Way DSP Mode: Haven 1350, Centrum 1315, Poort 1363, Buiten 1330)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"stadsdeel", 1},
        "STADSDEEL",
        juce::StringArray{"Haven (1350)", "Centrum (1315)", "Poort (1363)", "Buiten (1330)"},
        1)); // Default Centrum (1315)

    // Master Switch 1: 036 OVERDRIVE
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"overdrive036", 1},
        "036 OVERDRIVE",
        false));

    // Master Switch 2: 1300 MONO LOCK
    params.push_back(std::make_unique<juce::AudioParameterBool>(
        juce::ParameterID{"monoLock1300", 1},
        "1300 MONO LOCK",
        true));

    // Master Output Level
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterVolume", 1},
        "MASTER VOL",
        juce::NormalisableRange<float>(-24.0f, 6.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    return { params.begin(), params.end() };
}

const juce::String AllyBassAudioProcessor::getName() const
{
    return JucePlugin_Name;
}

bool AllyBassAudioProcessor::acceptsMidi() const { return true; }
bool AllyBassAudioProcessor::producesMidi() const { return false; }
bool AllyBassAudioProcessor::isMidiEffect() const { return false; }
double AllyBassAudioProcessor::getTailLengthSeconds() const { return 0.0; }

int AllyBassAudioProcessor::getNumPrograms()
{
    return static_cast<int>(AllyPresets::getFactoryPresets().size());
}

int AllyBassAudioProcessor::getCurrentProgram() { return 0; }
void AllyBassAudioProcessor::setCurrentProgram(int index)
{
    AllyPresets::applyPreset(apvts, index);
}

const juce::String AllyBassAudioProcessor::getProgramName(int index)
{
    const auto& presets = AllyPresets::getFactoryPresets();
    if (index >= 0 && index < static_cast<int>(presets.size()))
        return presets[static_cast<size_t>(index)].name;
    return {};
}

void AllyBassAudioProcessor::changeProgramName(int, const juce::String&) {}

void AllyBassAudioProcessor::prepareToPlay(double sampleRate, int samplesPerBlock)
{
    weerwaterGlide.prepare(sampleRate);
    napSubOsc.prepare(sampleRate);
    polderTransient.prepare(sampleRate);
    stadsdeelSaturation.prepare(sampleRate);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = sampleRate;
    spec.maximumBlockSize = static_cast<juce::uint32>(samplesPerBlock);
    spec.numChannels = static_cast<juce::uint32>(getTotalNumOutputChannels());

    monoLockFilter.prepare(spec);
    overdrive036.prepare(spec);

    masterLowPass.prepare(spec);
    *masterLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(sampleRate, 14000.0f, 0.7071f);

    envState = EnvelopeState::Idle;
    envLevel = 0.0f;
}

void AllyBassAudioProcessor::releaseResources()
{
    weerwaterGlide.reset();
    napSubOsc.reset();
    polderTransient.reset();
    stadsdeelSaturation.reset();
    monoLockFilter.reset();
    overdrive036.reset();
    keyboardState.reset();
}

bool AllyBassAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void AllyBassAudioProcessor::updateEnvelopeRates(float attackMs, float vervalSeconds, float releaseMs)
{
    double sr = getSampleRate();
    if (sr <= 0.0) return;

    float sampleRateFloat = static_cast<float>(sr);

    // Attack rate: linear increment per sample from 0 to targetVelocity
    float attackSec = juce::jmax(0.0002f, attackMs * 0.001f);
    float attackSamples = attackSec * sampleRateFloat;
    attackRate = 1.0f / attackSamples;

    // Decay rate: drops to ~1% (-40 dB) in vervalSeconds
    float decaySamples = juce::jmax(1.0f, vervalSeconds * sampleRateFloat);
    decayRate = std::exp(-4.605f / decaySamples);

    // Release rate: drops to ~1% in releaseMs
    float releaseSec = juce::jmax(0.005f, releaseMs * 0.001f);
    float releaseSamples = releaseSec * sampleRateFloat;
    releaseRate = std::exp(-4.605f / releaseSamples);
}

void AllyBassAudioProcessor::handleMidiEvent(const juce::MidiMessage& message, float attackMs, float punchAmount)
{
    if (message.isNoteOn())
    {
        float glideMs = apvts.getRawParameterValue("weerwaterGlide")->load();
        float vel = message.getFloatVelocity();
        weerwaterGlide.noteOn(message.getNoteNumber(), vel, glideMs);

        targetVelocity = vel;

        // Start attack ramp from current level towards velocity (zero audio jump = zero click)
        envState = EnvelopeState::Attack;
        double sr = getSampleRate();
        float attackSec = juce::jmax(0.0002f, attackMs * 0.001f);
        float attackSamples = attackSec * static_cast<float>(sr);
        attackRate = juce::jmax(0.0001f, (targetVelocity - envLevel) / attackSamples);

        // Only trigger transient if punchAmount is dialed in
        if (punchAmount > 0.01f)
            polderTransient.trigger(vel, punchAmount);
    }
    else if (message.isNoteOff())
    {
        weerwaterGlide.noteOff(message.getNoteNumber());
        if (!weerwaterGlide.getIsNoteActive())
        {
            envState = EnvelopeState::Release;
        }
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        weerwaterGlide.reset();
        envState = EnvelopeState::Idle;
        envLevel = 0.0f;
    }
}

void AllyBassAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Process on-screen and computer keyboard events into MIDI stream
    keyboardState.processNextMidiBuffer(midiMessages, 0, buffer.getNumSamples(), true);

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();

    // Fetch parameter values
    const float diepte = apvts.getRawParameterValue("diepte")->load();
    const float polderPunch = apvts.getRawParameterValue("polderPunch")->load();
    const float attackMs = apvts.getRawParameterValue("attack")->load();
    const float verval = apvts.getRawParameterValue("verval")->load();
    const float releaseMs = apvts.getRawParameterValue("release")->load();
    const float havenDirt = apvts.getRawParameterValue("havenDirt")->load();
    const float weerwaterGlideMs = apvts.getRawParameterValue("weerwaterGlide")->load();
    const int stadsdeelIndex = static_cast<int>(apvts.getRawParameterValue("stadsdeel")->load());
    const bool isOverdrive = apvts.getRawParameterValue("overdrive036")->load() > 0.5f;
    const bool isMonoLock = apvts.getRawParameterValue("monoLock1300")->load() > 0.5f;
    const float masterVolDb = apvts.getRawParameterValue("masterVolume")->load();
    const float masterGain = juce::Decibels::decibelsToGain(masterVolDb);

    updateEnvelopeRates(attackMs, verval, releaseMs);
    weerwaterGlide.updateGlideCoeff(weerwaterGlideMs, weerwaterGlide.getCurrentVelocity());

    auto* leftOut = buffer.getWritePointer(0);
    auto* rightOut = totalNumOutputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    auto midiIterator = midiMessages.findNextSamplePosition(0);
    float sumSubRms = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Process MIDI events at exact sample positions
        while (midiIterator != midiMessages.end() && (*midiIterator).samplePosition == sample)
        {
            handleMidiEvent((*midiIterator).getMessage(), attackMs, polderPunch);
            midiIterator = midiMessages.findNextSamplePosition(sample + 1);
        }

        // Advance synth voice
        float instantFreq = weerwaterGlide.processNextFrequency();

        if (weerwaterGlide.popPhaseReset())
            napSubOsc.resetNapLock();

        // Advance Envelope State Machine
        switch (envState)
        {
            case EnvelopeState::Idle:
                envLevel = 0.0f;
                break;

            case EnvelopeState::Attack:
                envLevel += attackRate;
                if (envLevel >= targetVelocity)
                {
                    envLevel = targetVelocity;
                    envState = EnvelopeState::Decay;
                }
                break;

            case EnvelopeState::Decay:
                envLevel *= decayRate;
                if (envLevel < 0.0001f)
                {
                    envLevel = 0.0f;
                    envState = EnvelopeState::Idle;
                }
                break;

            case EnvelopeState::Release:
                envLevel *= releaseRate;
                if (envLevel < 0.0001f)
                {
                    envLevel = 0.0f;
                    envState = EnvelopeState::Idle;
                }
                break;
        }

        float synthSample = 0.0f;

        if (envLevel > 0.0001f || polderTransient.getIsActive())
        {
            // 1. NAP Sub-Oscillator (Clean low Sine/Warm hybrid)
            float subWave = napSubOsc.processSample(instantFreq, diepte);

            // 2. Optional Polder Knock (zero if Punch = 0)
            float punchClick = polderTransient.processSample(polderPunch);

            // Combine sub core with transient knock
            synthSample = (subWave * envLevel) + punchClick;

            // 3. Stadsdeel Saturation
            synthSample = stadsdeelSaturation.processSample(
                synthSample, havenDirt, static_cast<AllyDSP::StadsdeelMode>(stadsdeelIndex));
        }
        else
        {
            synthSample = 0.0f;
        }

        leftOut[sample] = synthSample;
        if (rightOut != nullptr)
            rightOut[sample] = synthSample;

        sumSubRms += synthSample * synthSample;

        // Push into visualizer FIFO
        int writeIdx = visualizerWriteIndex.load(std::memory_order_relaxed);
        visualizerBuffer[static_cast<size_t>(writeIdx)] = synthSample;
        visualizerWriteIndex.store((writeIdx + 1) % visualizerBufferSize, std::memory_order_release);
    }

    // Update real-time RMS for visualizer
    float rms = std::sqrt(sumSubRms / static_cast<float>(numSamples));
    currentSubLevel.store(rms, std::memory_order_relaxed);

    // 4. Master Filter
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    masterLowPass.process(context);

    // 5. 036 OVERDRIVE (Parallel compression + 6dB push)
    overdrive036.process(buffer, isOverdrive);

    // 6. 1300 MONO LOCK (Strict mono sub < 90Hz)
    monoLockFilter.process(buffer, isMonoLock);

    // 7. Master Output Volume
    if (std::abs(masterGain - 1.0f) > 0.001f)
    {
        buffer.applyGain(masterGain);
    }
}

void AllyBassAudioProcessor::getVisualizerData(float* destination, int numSamples)
{
    int currentWrite = visualizerWriteIndex.load(std::memory_order_acquire);
    int readStart = (currentWrite - numSamples + visualizerBufferSize) % visualizerBufferSize;

    for (int i = 0; i < numSamples; ++i)
    {
        int index = (readStart + i) % visualizerBufferSize;
        destination[i] = visualizerBuffer[static_cast<size_t>(index)];
    }
}

bool AllyBassAudioProcessor::isOverdriveActive() const noexcept
{
    return apvts.getRawParameterValue("overdrive036")->load() > 0.5f;
}

float AllyBassAudioProcessor::getCurrentSubLevel() const noexcept
{
    return currentSubLevel.load(std::memory_order_relaxed);
}

juce::AudioProcessorEditor* AllyBassAudioProcessor::createEditor()
{
    return new AllyBassAudioProcessorEditor(*this);
}

bool AllyBassAudioProcessor::hasEditor() const { return true; }

void AllyBassAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    std::unique_ptr<juce::XmlElement> xml(state.createXml());
    copyXmlToBinary(*xml, destData);
}

void AllyBassAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xmlState));
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AllyBassAudioProcessor();
}
