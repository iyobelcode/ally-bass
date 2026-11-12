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
    resetOutputTelemetry();
    apvts.state.addListener(this);
    for (const auto& id : AllyPresets::getAllParameterIds())
        apvts.addParameterListener(id, this);
}

AllyBassAudioProcessor::~AllyBassAudioProcessor()
{
    for (const auto& id : AllyPresets::getAllParameterIds())
        apvts.removeParameterListener(id, this);
    apvts.state.removeListener(this);
}

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

    // 3. HAVEN DIRT (Drive / Harmonics)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"havenDirt", 1},
        "HAVEN DIRT",
        juce::NormalisableRange<float>(0.0f, 1.0f, 0.01f),
        0.15f,
        juce::AudioParameterFloatAttributes().withLabel("Drive")));

    // 4. WATERWIJK GLIDE (Portamento Time)
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"waterwijkGlide", 1},
        "WATERWIJK GLIDE",
        juce::NormalisableRange<float>(0.0f, 500.0f, 0.5f, 0.45f),
        40.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // 5. MASTER OUTPUT LEVEL
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"masterVolume", 1},
        "OUTPUT",
        juce::NormalisableRange<float>(-24.0f, 6.0f, 0.1f),
        0.0f,
        juce::AudioParameterFloatAttributes().withLabel("dB")));

    // Mini Controls: ATTACK & RELEASE
    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"attack", 1},
        "ATTACK",
        juce::NormalisableRange<float>(1.0f, 300.0f, 0.1f, 0.35f),
        8.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    params.push_back(std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID{"release", 1},
        "RELEASE",
        juce::NormalisableRange<float>(10.0f, 3000.0f, 1.0f, 0.35f),
        250.0f,
        juce::AudioParameterFloatAttributes().withLabel("ms")));

    // 4-Way DSP Mode: Haven 1350, Centrum 1315, Poort 1363, Buiten 1330
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

int AllyBassAudioProcessor::getCurrentProgram()
{
    return juce::jlimit(0, juce::jmax(1, getNumPrograms()) - 1, currentProgramAtomic.load(std::memory_order_acquire));
}

void AllyBassAudioProcessor::setCurrentProgram(int index)
{
    const int numPrograms = getNumPrograms();
    if (index < 0 || index >= numPrograms)
        return;

    suppressParamModifiedAtomic.store(true, std::memory_order_release);
    AllyPresets::applyPreset(apvts, index);
    currentProgramAtomic.store(index, std::memory_order_release);
    presetModifiedAtomic.store(false, std::memory_order_release);
    suppressParamModifiedAtomic.store(false, std::memory_order_release);
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
    currentSampleRateHz.store(sampleRate > 1.0 ? sampleRate : 44100.0, std::memory_order_relaxed);
    waterwijkGlide.prepare(sampleRate);
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

    const double validSr = sampleRate > 1.0 ? sampleRate : 44100.0;
    velocitySmoothCoeff = 1.0f - std::exp(-1.0f / juce::jmax(1.0f, 0.002f * static_cast<float>(validSr)));

    envState = EnvelopeState::Idle;
    envLevel = 0.0f;
    targetVelocity = 1.0f;
    currentVelocityGain = 1.0f;
    updateEnvelopeRates(apvts.getRawParameterValue("attack")->load(),
                        apvts.getRawParameterValue("release")->load());
    resetOutputTelemetry();
}

void AllyBassAudioProcessor::releaseResources()
{
    waterwijkGlide.reset();
    napSubOsc.reset();
    polderTransient.reset();
    stadsdeelSaturation.reset();
    monoLockFilter.reset();
    overdrive036.reset();
    keyboardState.reset();
    resetOutputTelemetry();
}

bool AllyBassAudioProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
    if (layouts.getMainOutputChannelSet() != juce::AudioChannelSet::mono()
     && layouts.getMainOutputChannelSet() != juce::AudioChannelSet::stereo())
        return false;

    return true;
}

void AllyBassAudioProcessor::updateEnvelopeRates(float attackMs, float releaseMs)
{
    double sr = getSampleRate();
    if (sr <= 0.0)
        sr = currentSampleRateHz.load(std::memory_order_relaxed);
    if (sr <= 0.0)
        return;

    const float sampleRateFloat = static_cast<float>(sr);

    // 1. Normalized [0 -> 1] linear attack rate honoring full APVTS range (1.0 - 300.0 ms)
    const float attackSec = juce::jmax(0.001f, attackMs * 0.001f);
    const float attackSamples = juce::jmax(1.0f, attackSec * sampleRateFloat);
    attackRate = 1.0f / attackSamples;

    // 2. Release rate: drops to ~1% (-40 dB) in releaseMs
    const float releaseSec = juce::jmax(0.005f, releaseMs * 0.001f);
    const float releaseSamples = juce::jmax(1.0f, releaseSec * sampleRateFloat);
    releaseRate = std::exp(-4.605f / releaseSamples);
}

void AllyBassAudioProcessor::handleMidiEvent(const juce::MidiMessage& message, float attackMs, float punchAmount)
{
    juce::ignoreUnused(attackMs);

    if (message.isNoteOn())
    {
        const float glideMs = apvts.getRawParameterValue("waterwijkGlide")->load();
        const float vel = juce::jlimit(0.0f, 1.0f, message.getFloatVelocity());
        waterwijkGlide.noteOn(message.getNoteNumber(), vel, glideMs);

        const bool isFreshNote = waterwijkGlide.popTransientTrigger();
        const bool startingFromSilence = (envState == EnvelopeState::Idle || envLevel <= 0.0001f);

        targetVelocity = vel;
        if (startingFromSilence)
        {
            // Snap velocity gain while silent so Attack ramps strictly from 0 toward exact velocity
            currentVelocityGain = vel;
            envLevel = 0.0f;
        }

        if (isFreshNote)
        {
            // Fresh strike or retrigger during Release: run normalized Attack [envLevel -> 1.0]
            envState = EnvelopeState::Attack;
            polderTransient.trigger(vel, punchAmount);
        }
    }
    else if (message.isNoteOff())
    {
        float glideMs = apvts.getRawParameterValue("waterwijkGlide")->load();
        waterwijkGlide.noteOff(message.getNoteNumber(), glideMs);
        if (!waterwijkGlide.getIsNoteActive())
        {
            envState = EnvelopeState::Release;
        }
    }
    else if (message.isAllNotesOff() || message.isAllSoundOff())
    {
        waterwijkGlide.reset();
        polderTransient.reset();
        envState = EnvelopeState::Idle;
        envLevel = 0.0f;
        targetVelocity = 0.0f;
        currentVelocityGain = 0.0f;
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
    if (numSamples <= 0 || totalNumOutputChannels <= 0 || buffer.getNumChannels() <= 0)
        return;

    // Fetch parameter values
    const float diepte = apvts.getRawParameterValue("diepte")->load();
    const float polderPunch = apvts.getRawParameterValue("polderPunch")->load();
    const float havenDirt = apvts.getRawParameterValue("havenDirt")->load();
    const float waterwijkGlideMs = apvts.getRawParameterValue("waterwijkGlide")->load();
    const float masterVolDb = apvts.getRawParameterValue("masterVolume")->load();
    const float masterGain = juce::Decibels::decibelsToGain(masterVolDb);

    const float attackMs = apvts.getRawParameterValue("attack")->load();
    const float releaseMs = apvts.getRawParameterValue("release")->load();
    const int stadsdeelIndex = static_cast<int>(apvts.getRawParameterValue("stadsdeel")->load());
    const bool isOverdrive = apvts.getRawParameterValue("overdrive036")->load() > 0.5f;
    const bool isMonoLock = apvts.getRawParameterValue("monoLock1300")->load() > 0.5f;

    updateEnvelopeRates(attackMs, releaseMs);
    waterwijkGlide.updateGlideCoeff(waterwijkGlideMs, waterwijkGlide.getCurrentVelocity());

    auto* leftOut = buffer.getWritePointer(0);
    auto* rightOut = totalNumOutputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    float sumSubRms = 0.0f;

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Process all MIDI events at (or before) this exact sample position sequentially
        while (midiIterator != midiEnd && (*midiIterator).samplePosition <= sample)
        {
            handleMidiEvent((*midiIterator).getMessage(), attackMs, polderPunch);
            ++midiIterator;
        }

        // Advance synth voice
        float instantFreq = waterwijkGlide.processNextFrequency();

        if (waterwijkGlide.popPhaseReset())
            napSubOsc.resetNapLock();

        // Advance Normalized Envelope State Machine (Attack [0->1] -> Sustain [1] -> Release [->0])
        switch (envState)
        {
            case EnvelopeState::Idle:
                envLevel = 0.0f;
                break;

            case EnvelopeState::Attack:
                envLevel += attackRate;
                if (envLevel >= 1.0f)
                {
                    envLevel = 1.0f;
                    envState = EnvelopeState::Sustain;
                }
                break;

            case EnvelopeState::Sustain:
                envLevel = 1.0f;
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

        // Smooth velocity gain transitions cleanly without altering Attack/Release duration
        if (envState != EnvelopeState::Idle)
        {
            if (std::abs(targetVelocity - currentVelocityGain) > 1e-6f)
                currentVelocityGain += velocitySmoothCoeff * (targetVelocity - currentVelocityGain);
            else
                currentVelocityGain = targetVelocity;
        }

        const float ampEnv = envLevel * currentVelocityGain;
        float synthSample = 0.0f;

        if (ampEnv > 0.0f || polderTransient.getIsActive())
        {
            // 1. NAP Sub-Oscillator (Clean low Sine/Warm hybrid)
            float subSample = (ampEnv > 0.0f)
                ? (napSubOsc.processSample(instantFreq, diepte) * ampEnv)
                : 0.0f;

            // 2. Click-free Polder Punch transient
            float punchSample = polderTransient.processSample(polderPunch);
            synthSample = subSample + punchSample;

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
    }

    // Drain any MIDI events stamped past the block end so noteOffs are never lost
    while (midiIterator != midiEnd)
    {
        handleMidiEvent((*midiIterator).getMessage(), attackMs, polderPunch);
        ++midiIterator;
    }

    // Update real-time pre-postfilter sub RMS telemetry
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

    // 8. Lock-free, allocation-free post-output telemetry capture (Scope FIFO + Peak/RMS/Clip meter)
    {
        const int activeChannels = juce::jmin(buffer.getNumChannels(), juce::jlimit(1, 2, totalNumOutputChannels));
        const float* ch0 = buffer.getReadPointer(0);
        const float* ch1 = (activeChannels > 1) ? buffer.getReadPointer(1) : nullptr;

        int writeIdx = visualizerWriteIndex.load(std::memory_order_relaxed);
        double sumSquares = 0.0;
        float blockPeakL = 0.0f;
        float blockPeakR = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            const float rawL = (ch0 != nullptr) ? ch0[s] : 0.0f;
            const float l = std::isfinite(rawL) ? rawL : 0.0f;
            const float rawR = (ch1 != nullptr) ? ch1[s] : rawL;
            const float r = std::isfinite(rawR) ? rawR : 0.0f;

            blockPeakL = juce::jmax(blockPeakL, std::abs(l));
            sumSquares += static_cast<double>(l) * static_cast<double>(l);

            if (ch1 != nullptr)
            {
                blockPeakR = juce::jmax(blockPeakR, std::abs(r));
                sumSquares += static_cast<double>(r) * static_cast<double>(r);
            }
            else
            {
                blockPeakR = blockPeakL;
            }

            const float scopeSample = (ch1 != nullptr) ? (0.5f * (l + r)) : l;
            visualizerBuffer[static_cast<size_t>(writeIdx)].store(scopeSample, std::memory_order_relaxed);
            writeIdx = (writeIdx + 1) & (visualizerBufferSize - 1);
        }

        visualizerWriteIndex.store(writeIdx, std::memory_order_release);
        visualizerWriteCounter.fetch_add(1, std::memory_order_release);

        const float blockPeak = juce::jmax(blockPeakL, blockPeakR);
        const float blockRms = static_cast<float>(
            std::sqrt(sumSquares / static_cast<double>(numSamples * juce::jmax(1, activeChannels))));

        outputLastBlockPeak.store(blockPeak, std::memory_order_relaxed);
        outputLastBlockRms.store(blockRms, std::memory_order_relaxed);
        outputLeftPeak.store(blockPeakL, std::memory_order_relaxed);
        outputRightPeak.store(blockPeakR, std::memory_order_relaxed);
        outputChannelCount.store(activeChannels, std::memory_order_relaxed);

        // Lock-free atomic fetch-max so brief transients between GUI refreshes are captured reliably
        float expectedPeak = outputPeakLatched.load(std::memory_order_relaxed);
        while (blockPeak > expectedPeak
               && !outputPeakLatched.compare_exchange_weak(expectedPeak, blockPeak,
                                                           std::memory_order_relaxed,
                                                           std::memory_order_relaxed))
        {
        }

        float expectedRms = outputRmsLatched.load(std::memory_order_relaxed);
        while (blockRms > expectedRms
               && !outputRmsLatched.compare_exchange_weak(expectedRms, blockRms,
                                                          std::memory_order_relaxed,
                                                          std::memory_order_relaxed))
        {
        }

        if (blockPeak >= 1.0f)
            outputClipLatched.store(true, std::memory_order_relaxed);

        outputMeterCounter.fetch_add(1, std::memory_order_release);
    }
}

void AllyBassAudioProcessor::getVisualizerData(float* destination, int numSamples)
{
    if (destination == nullptr || numSamples <= 0)
        return;

    const int count = juce::jmin(numSamples, visualizerBufferSize);
    for (int i = 0; i < numSamples - count; ++i)
        destination[i] = 0.0f;

    const int currentWrite = visualizerWriteIndex.load(std::memory_order_acquire);
    const int readStart = (currentWrite - count + visualizerBufferSize) & (visualizerBufferSize - 1);

    float* dst = destination + (numSamples - count);
    for (int i = 0; i < count; ++i)
    {
        const int index = (readStart + i) & (visualizerBufferSize - 1);
        const float val = visualizerBuffer[static_cast<size_t>(index)].load(std::memory_order_relaxed);
        dst[i] = std::isfinite(val) ? val : 0.0f;
    }
}

uint64_t AllyBassAudioProcessor::getVisualizerWriteCounter() const noexcept
{
    return visualizerWriteCounter.load(std::memory_order_acquire);
}

bool AllyBassAudioProcessor::isOverdriveActive() const noexcept
{
    return apvts.getRawParameterValue("overdrive036")->load() > 0.5f;
}

bool AllyBassAudioProcessor::isMonoLockActive() const noexcept
{
    return apvts.getRawParameterValue("monoLock1300")->load() > 0.5f;
}

float AllyBassAudioProcessor::getCurrentSubLevel() const noexcept
{
    return currentSubLevel.load(std::memory_order_relaxed);
}

double AllyBassAudioProcessor::getCurrentSampleRate() const noexcept
{
    return currentSampleRateHz.load(std::memory_order_relaxed);
}

AllyBassAudioProcessor::OutputMeterMetrics AllyBassAudioProcessor::peekOutputMeterMetrics() const noexcept
{
    OutputMeterMetrics m;
    const float lastPeak = outputLastBlockPeak.load(std::memory_order_relaxed);
    const float latchedP = outputPeakLatched.load(std::memory_order_relaxed);
    const float lastRms  = outputLastBlockRms.load(std::memory_order_relaxed);
    const float latchedR = outputRmsLatched.load(std::memory_order_relaxed);

    m.peakLinear      = juce::jmax(lastPeak, latchedP);
    m.rmsLinear       = juce::jmax(lastRms, latchedR);
    m.leftPeakLinear  = outputLeftPeak.load(std::memory_order_relaxed);
    m.rightPeakLinear = outputRightPeak.load(std::memory_order_relaxed);
    m.channelCount    = outputChannelCount.load(std::memory_order_relaxed);
    m.clipped         = outputClipLatched.load(std::memory_order_relaxed) || (m.peakLinear >= 1.0f);
    m.updateCounter   = outputMeterCounter.load(std::memory_order_acquire);
    return m;
}

AllyBassAudioProcessor::OutputMeterMetrics AllyBassAudioProcessor::consumeOutputMeterMetrics() noexcept
{
    OutputMeterMetrics m;
    const float lastPeak = outputLastBlockPeak.load(std::memory_order_relaxed);
    const float latchedP = outputPeakLatched.exchange(0.0f, std::memory_order_relaxed);
    const float lastRms  = outputLastBlockRms.load(std::memory_order_relaxed);
    const float latchedR = outputRmsLatched.exchange(0.0f, std::memory_order_relaxed);

    m.peakLinear      = juce::jmax(lastPeak, latchedP);
    m.rmsLinear       = juce::jmax(lastRms, latchedR);
    m.leftPeakLinear  = outputLeftPeak.load(std::memory_order_relaxed);
    m.rightPeakLinear = outputRightPeak.load(std::memory_order_relaxed);
    m.channelCount    = outputChannelCount.load(std::memory_order_relaxed);
    m.clipped         = outputClipLatched.exchange(false, std::memory_order_relaxed) || (m.peakLinear >= 1.0f);
    m.updateCounter   = outputMeterCounter.load(std::memory_order_acquire);
    return m;
}

void AllyBassAudioProcessor::resetOutputTelemetry() noexcept
{
    for (auto& slot : visualizerBuffer)
        slot.store(0.0f, std::memory_order_relaxed);

    visualizerWriteIndex.store(0, std::memory_order_release);
    visualizerWriteCounter.store(0, std::memory_order_release);
    currentSubLevel.store(0.0f, std::memory_order_relaxed);

    outputLastBlockPeak.store(0.0f, std::memory_order_relaxed);
    outputLastBlockRms.store(0.0f, std::memory_order_relaxed);
    outputPeakLatched.store(0.0f, std::memory_order_relaxed);
    outputRmsLatched.store(0.0f, std::memory_order_relaxed);
    outputLeftPeak.store(0.0f, std::memory_order_relaxed);
    outputRightPeak.store(0.0f, std::memory_order_relaxed);
    outputChannelCount.store(2, std::memory_order_relaxed);
    outputClipLatched.store(false, std::memory_order_relaxed);
    outputMeterCounter.store(0, std::memory_order_release);
}

juce::AudioProcessorEditor* AllyBassAudioProcessor::createEditor()
{
    return new AllyBassAudioProcessorEditor(*this);
}

bool AllyBassAudioProcessor::hasEditor() const { return true; }

void AllyBassAudioProcessor::getStateInformation(juce::MemoryBlock& destData)
{
    auto state = apvts.copyState();
    const auto info = getPresetStateInfo();
    AllyPresets::writePresetMetadata(state,
                                     info.kind,
                                     info.factoryIndex,
                                     info.displayName,
                                     info.filePath,
                                     info.modified);
    state.removeProperty(AllyPresets::propPresetApplying, nullptr);
    state.removeProperty(AllyPresets::propPresetRevision, nullptr);

    if (std::unique_ptr<juce::XmlElement> xml = state.createXml())
        copyXmlToBinary(*xml, destData);
}

void AllyBassAudioProcessor::setStateInformation(const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xmlState(getXmlFromBinary(data, sizeInBytes));
    if (xmlState != nullptr && xmlState->hasTagName(apvts.state.getType()))
    {
        auto restoredTree = juce::ValueTree::fromXml(*xmlState);
        if (!restoredTree.isValid())
            return;

        const bool hadMetadata = restoredTree.hasProperty(AllyPresets::propPresetKind);
        const auto info = hadMetadata
            ? AllyPresets::readPresetMetadata(restoredTree)
            : AllyPresets::PresetStateInfo { AllyPresets::PresetKind::Init, -1, "Custom", {}, false };

        suppressParamModifiedAtomic.store(true, std::memory_order_release);
        apvts.state.setProperty(AllyPresets::propPresetApplying, true, nullptr);
        apvts.replaceState(restoredTree);
        AllyPresets::writePresetMetadata(apvts.state,
                                         info.kind,
                                         info.factoryIndex,
                                         info.displayName,
                                         info.filePath,
                                         info.modified);
        AllyPresets::bumpPresetRevision(apvts.state);
        apvts.state.setProperty(AllyPresets::propPresetApplying, false, nullptr);

        presetModifiedAtomic.store(info.modified, std::memory_order_release);
        if (info.kind == AllyPresets::PresetKind::Factory
            && info.factoryIndex >= 0
            && info.factoryIndex < getNumPrograms())
        {
            currentProgramAtomic.store(info.factoryIndex, std::memory_order_release);
        }
        else
        {
            currentProgramAtomic.store(0, std::memory_order_release);
        }
        suppressParamModifiedAtomic.store(false, std::memory_order_release);
    }
}

AllyPresets::PresetStateInfo AllyBassAudioProcessor::getPresetStateInfo() const
{
    return AllyPresets::readPresetMetadata(apvts.state,
                                           presetModifiedAtomic.load(std::memory_order_acquire),
                                           true);
}

void AllyBassAudioProcessor::selectRelativeFactoryPreset(int delta)
{
    const int count = getNumPrograms();
    if (count <= 0)
        return;

    const auto info = getPresetStateInfo();
    int nextIndex = 0;
    if (info.kind == AllyPresets::PresetKind::Factory && info.factoryIndex >= 0)
    {
        nextIndex = AllyPresets::wrapPresetIndex(info.factoryIndex + delta, count);
    }
    else
    {
        nextIndex = (delta >= 0)
            ? AllyPresets::wrapPresetIndex(delta - 1, count)
            : AllyPresets::wrapPresetIndex(delta, count);
    }

    setCurrentProgram(nextIndex);
    updateHostDisplay();
}

bool AllyBassAudioProcessor::loadUserPresetFromFile(const juce::File& file, juce::String& outError)
{
    juce::String loadedName;
    suppressParamModifiedAtomic.store(true, std::memory_order_release);
    const bool ok = AllyPresets::loadPresetFromFile(apvts, file, loadedName, outError);
    if (ok)
    {
        presetModifiedAtomic.store(false, std::memory_order_release);
        currentProgramAtomic.store(0, std::memory_order_release);
        updateHostDisplay();
    }
    suppressParamModifiedAtomic.store(false, std::memory_order_release);
    return ok;
}

bool AllyBassAudioProcessor::saveUserPresetToFile(const juce::File& file,
                                                  const juce::String& presetName,
                                                  bool allowOverwrite,
                                                  juce::String& outError)
{
#ifdef JucePlugin_VersionString
    const juce::String pluginVer(JucePlugin_VersionString);
#else
    const juce::String pluginVer("1.0.0");
#endif

    suppressParamModifiedAtomic.store(true, std::memory_order_release);
    const bool ok = AllyPresets::savePresetToFile(apvts,
                                                  file,
                                                  presetName,
                                                  allowOverwrite,
                                                  outError,
                                                  pluginVer);
    if (ok)
    {
        presetModifiedAtomic.store(false, std::memory_order_release);
        currentProgramAtomic.store(0, std::memory_order_release);
        updateHostDisplay();
    }
    suppressParamModifiedAtomic.store(false, std::memory_order_release);
    return ok;
}

void AllyBassAudioProcessor::parameterChanged(const juce::String&, float)
{
    if (!suppressParamModifiedAtomic.load(std::memory_order_acquire))
        presetModifiedAtomic.store(true, std::memory_order_release);
}

void AllyBassAudioProcessor::valueTreePropertyChanged(juce::ValueTree& treeWhosePropertyHasChanged,
                                                      const juce::Identifier& property)
{
    if (treeWhosePropertyHasChanged != apvts.state)
        return;

    if (property == AllyPresets::propPresetApplying)
    {
        const bool applying = static_cast<bool>(treeWhosePropertyHasChanged.getProperty(AllyPresets::propPresetApplying, false));
        suppressParamModifiedAtomic.store(applying, std::memory_order_release);
        if (!applying)
        {
            const auto info = AllyPresets::readPresetMetadata(treeWhosePropertyHasChanged);
            presetModifiedAtomic.store(info.modified, std::memory_order_release);
            if (info.kind == AllyPresets::PresetKind::Factory
                && info.factoryIndex >= 0
                && info.factoryIndex < getNumPrograms())
            {
                currentProgramAtomic.store(info.factoryIndex, std::memory_order_release);
            }
        }
    }
    else if (property == AllyPresets::propPresetModified)
    {
        presetModifiedAtomic.store(static_cast<bool>(treeWhosePropertyHasChanged.getProperty(AllyPresets::propPresetModified, false)),
                                   std::memory_order_release);
    }
    else if (property == AllyPresets::propPresetIndex)
    {
        const auto info = AllyPresets::readPresetMetadata(treeWhosePropertyHasChanged);
        if (info.kind == AllyPresets::PresetKind::Factory
            && info.factoryIndex >= 0
            && info.factoryIndex < getNumPrograms())
        {
            currentProgramAtomic.store(info.factoryIndex, std::memory_order_release);
        }
    }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new AllyBassAudioProcessor();
}
