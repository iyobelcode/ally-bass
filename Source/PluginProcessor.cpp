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

    // 2. POLDER PUNCH (Transient Shaper Knock)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"polderPunch", 1},
        "POLDER PUNCH",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.40f,
        juce::AudioParameterFloatAttributes().withLabel("Attack")));

    // 3. VERVAL (Decay / Sustain)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"verval", 1},
        "VERVAL",
        juce::NormalisableRange<float>(0.05f, 5.0f, 0.01f, 0.40f),
        1.20f,
        juce::AudioParameterFloatAttributes().withLabel("s")));

    // 4. HAVEN DIRT (Drive / Harmonics)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"havenDirt", 1},
        "HAVEN DIRT",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.30f,
        juce::AudioParameterFloatAttributes().withLabel("Drive")));

    // 5. WEERWATER GLIDE (Portamento Time)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"weerwaterGlide", 1},
        "WEERWATER GLIDE",
        juce::NormalisableRange<float>(0.0f, 500.0f, 0.5f, 0.45f),
        50.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // 6. STADSDEEL (4-Way DSP Mode: Haven 1350, Centrum 1315, Poort 1363, Buiten 1330)
    params.push_back(std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID{"stadsdeel", 1},
        "STADSDEEL",
        juce::StringArray{"Haven (1350)", "Centrum (1315)", "Poort (1363)", "Buiten (1330)"},
        1)); // Default Centrum (1315) for immediate 808 banger

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

    ampEnvelope = 0.0f;
    isNoteHeld = false;

    // Default release coeff (~60ms release to prevent click on note-off)
    releaseCoeff = std::exp(-1.0f / (0.060f * static_cast<float>(sampleRate)));
}

void AllyBassAudioProcessor::releaseResources()
{
    weerwaterGlide.reset();
    napSubOsc.reset();
    polderTransient.reset();
    stadsdeelSaturation.reset();
    monoLockFilter.reset();
    overdrive036.reset();
}

bool AllyBassAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void AllyBassAudioProcessor::updateEnvelopeDecay(float vervalSeconds)
{
    double sr = getSampleRate();
    if (sr <= 0.0) return;

    // Decay to -60 dB (0.001) in vervalSeconds: exp(ln(0.001) / (T * sr))
    float totalSamples = vervalSeconds * static_cast<float>(sr);
    decayCoeff = std::exp(-6.9077f / totalSamples);
}

void AllyBassAudioProcessor::handleMidiEvent(const juce::MidiMessage& message)
{
    if (message.isNoteOn())
    {
        float glideMs = apvts.getRawParameterValue("weerwaterGlide")->load();
        float vel = message.getFloatVelocity();
        weerwaterGlide.noteOn(message.getNoteNumber(), vel, glideMs);

        isNoteHeld = true;
        ampEnvelope = vel; // Trigger attack envelope directly to velocity
    }
    else if (message.isNoteOff())
    {
        weerwaterGlide.noteOff(message.getNoteNumber());
        if (!weerwaterGlide.getIsNoteActive())
        {
            isNoteHeld = false;
        }
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        weerwaterGlide.reset();
        ampEnvelope = 0.0f;
        isNoteHeld = false;
    }
}

void AllyBassAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;
    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();

    // Fetch parameter values
    const float diepte = apvts.getRawParameterValue("diepte")->load();
    const float polderPunch = apvts.getRawParameterValue("polderPunch")->load();
    const float verval = apvts.getRawParameterValue("verval")->load();
    const float havenDirt = apvts.getRawParameterValue("havenDirt")->load();
    const float weerwaterGlideMs = apvts.getRawParameterValue("weerwaterGlide")->load();
    const int stadsdeelIndex = static_cast<int>(apvts.getRawParameterValue("stadsdeel")->load());
    const bool isOverdrive = apvts.getRawParameterValue("overdrive036")->load() > 0.5f;
    const bool isMonoLock = apvts.getRawParameterValue("monoLock1300")->load() > 0.5f;
    const float masterVolDb = apvts.getRawParameterValue("masterVolume")->load();
    const float masterGain = juce::Decibels::decibelsToGain(masterVolDb);

    updateEnvelopeDecay(verval);
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
            handleMidiEvent((*midiIterator).getMessage());
            midiIterator = midiMessages.findNextSamplePosition(sample + 1);
        }

        // Advance synth voice
        float instantFreq = weerwaterGlide.processNextFrequency();

        if (weerwaterGlide.popPhaseReset())
            napSubOsc.resetNapLock();

        if (weerwaterGlide.popTransientTrigger())
            polderTransient.trigger(weerwaterGlide.getCurrentVelocity());

        float synthSample = 0.0f;

        if (ampEnvelope > 0.0001f || polderTransient.getIsActive())
        {
            // 1. NAP Sub-Oscillator (Hybrid Sine/Triangle with -4.5m NAP resonance)
            float subWave = napSubOsc.processSample(instantFreq, diepte);

            // 2. Polder Transient (Synchronous attack kick/click)
            float punchClick = polderTransient.processSample(polderPunch);

            // Combine sub core with transient knock
            synthSample = (subWave * ampEnvelope) + punchClick;

            // 3. Stadsdeel Saturation
            synthSample = stadsdeelSaturation.processSample(
                synthSample, havenDirt, static_cast<AllyDSP::StadsdeelMode>(stadsdeelIndex));

            // Envelope progression
            if (isNoteHeld)
            {
                ampEnvelope *= decayCoeff;
            }
            else
            {
                ampEnvelope *= releaseCoeff;
            }
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

// Juce plugin entry point
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AllyBassAudioProcessor();
}
