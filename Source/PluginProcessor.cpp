#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Presets/PresetManager.h"

#include <algorithm>
#include <cmath>

namespace
{
inline float loadFiniteParam(const std::atomic<float>* ptr, float fallback, float minVal, float maxVal) noexcept
{
    if (ptr == nullptr)
        return fallback;
    const float v = ptr->load(std::memory_order_relaxed);
    if (!std::isfinite(v))
        return fallback;
    return juce::jlimit(minVal, maxVal, v);
}
} // namespace

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
    rawDiepte         = apvts.getRawParameterValue("diepte");
    rawPolderPunch    = apvts.getRawParameterValue("polderPunch");
    rawHavenDirt      = apvts.getRawParameterValue("havenDirt");
    rawWaterwijkGlide = apvts.getRawParameterValue("waterwijkGlide");
    rawMasterVolume   = apvts.getRawParameterValue("masterVolume");
    rawAttack         = apvts.getRawParameterValue("attack");
    rawRelease        = apvts.getRawParameterValue("release");
    rawStadsdeel      = apvts.getRawParameterValue("stadsdeel");
    rawOverdrive036   = apvts.getRawParameterValue("overdrive036");
    rawMonoLock1300   = apvts.getRawParameterValue("monoLock1300");

    for (auto& velSlot : guiDesiredVelocities)
        velSlot.store(0.9f, std::memory_order_relaxed);

    resetOutputTelemetry();
    keyboardState.addListener(this);
    apvts.state.addListener(this);
    for (const auto& id : AllyPresets::getAllParameterIds())
        apvts.addParameterListener(id, this);
}

AllyBassAudioProcessor::~AllyBassAudioProcessor()
{
    keyboardState.removeListener(this);
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
double AllyBassAudioProcessor::getTailLengthSeconds() const
{
    // The monophonic bass engine features a configurable RELEASE envelope up to 3000 ms.
    // While the primary envelope reaches -80 dBFS at 6.00 seconds (2.0x releaseMs),
    // post-processing IIR stages (specifically the 5 Hz DC-blocker in 036 Overdrive)
    // require up to 8.46 seconds under worst-case excitation to settle completely to
    // bit-identical IEEE 754 zero.
    // Returning a static, conservative 9.0 seconds guarantees DAWs (VST3/AUv2) never
    // truncate offline bounces, region exports, or track freezes across any preset,
    // providing a 538 ms digital silence margin while eliminating host caching bugs.
    return 9.0;
}

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
    const double validSr = (std::isfinite(sampleRate) && sampleRate > 1.0) ? sampleRate : 44100.0;
    const int safeBlockSize = juce::jmax(1, samplesPerBlock);
    currentSampleRateHz.store(validSr, std::memory_order_relaxed);

    waterwijkGlide.prepare(validSr);
    napSubOsc.prepare(validSr);
    polderTransient.prepare(validSr);
    stadsdeelSaturation.prepare(validSr);

    juce::dsp::ProcessSpec spec;
    spec.sampleRate = validSr;
    spec.maximumBlockSize = static_cast<juce::uint32>(safeBlockSize);
    spec.numChannels = static_cast<juce::uint32>(juce::jmax(1, getTotalNumOutputChannels()));

    monoLockFilter.prepare(spec);
    overdrive036.prepare(spec);

    masterLowPass.prepare(spec);
    const float safeLowPassHz = juce::jmin(14000.0f, 0.45f * static_cast<float>(validSr));
    *masterLowPass.state = *juce::dsp::IIR::Coefficients<float>::makeLowPass(validSr, safeLowPassHz, 0.7071f);

    velocitySmoothCoeff = 1.0f - std::exp(-1.0f / juce::jmax(1.0f, 0.002f * static_cast<float>(validSr)));

    constexpr double paramRampSeconds = 0.008;
    smoothedDiepte.reset(validSr, paramRampSeconds);
    smoothedHavenDirt.reset(validSr, paramRampSeconds);
    smoothedMasterGain.reset(validSr, paramRampSeconds);

    smoothedDiepte.setCurrentAndTargetValue(loadFiniteParam(rawDiepte, 0.50f, 0.0f, 1.0f));
    smoothedHavenDirt.setCurrentAndTargetValue(loadFiniteParam(rawHavenDirt, 0.15f, 0.0f, 1.0f));
    const float initMasterVolDb = loadFiniteParam(rawMasterVolume, 0.0f, -24.0f, 6.0f);
    smoothedMasterGain.setCurrentAndTargetValue(juce::Decibels::decibelsToGain(initMasterVolDb));

    envState = EnvelopeState::Idle;
    envLevel = 0.0f;
    targetVelocity = 1.0f;
    currentVelocityGain = 1.0f;
    updateEnvelopeRates(loadFiniteParam(rawAttack, 8.0f, 1.0f, 300.0f),
                        loadFiniteParam(rawRelease, 250.0f, 10.0f, 3000.0f));

    audioGuiHeldNotesMask = { 0u, 0u };
    audioExternalNotesMask = { 0u, 0u };
    guiMidiReadIdx.store(guiMidiWriteIdx.load(std::memory_order_acquire), std::memory_order_release);
    const bool anyGuiHeld = (guiDesiredNotesMask[0].load(std::memory_order_acquire) != 0u)
                         || (guiDesiredNotesMask[1].load(std::memory_order_acquire) != 0u);
    guiMidiOverflowPending.store(anyGuiHeld, std::memory_order_release);
    externalMidiNotesMask[0].store(0u, std::memory_order_release);
    externalMidiNotesMask[1].store(0u, std::memory_order_release);
    externalMidiVersion.fetch_add(1, std::memory_order_release);

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
    guiDesiredNotesMask[0].store(0u, std::memory_order_release);
    guiDesiredNotesMask[1].store(0u, std::memory_order_release);
    guiLastPressedNote.store(-1, std::memory_order_release);
    guiMidiReadIdx.store(guiMidiWriteIdx.load(std::memory_order_acquire), std::memory_order_release);
    guiMidiOverflowPending.store(false, std::memory_order_release);
    audioGuiHeldNotesMask = { 0u, 0u };
    audioExternalNotesMask = { 0u, 0u };
    externalMidiNotesMask[0].store(0u, std::memory_order_release);
    externalMidiNotesMask[1].store(0u, std::memory_order_release);
    externalMidiVersion.fetch_add(1, std::memory_order_release);

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
        const float glideMs = loadFiniteParam(rawWaterwijkGlide, 40.0f, 0.0f, 500.0f);
        const float rawVel = message.getFloatVelocity();
        const float vel = std::isfinite(rawVel) ? juce::jlimit(0.0f, 1.0f, rawVel) : 0.0f;
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
        const float glideMs = loadFiniteParam(rawWaterwijkGlide, 40.0f, 0.0f, 500.0f);
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

void AllyBassAudioProcessor::dispatchSourcedMidiMessage(MidiOrigin origin,
                                                        const juce::MidiMessage& message,
                                                        float attackMs,
                                                        float punchAmount,
                                                        bool& outExternalMaskChanged)
{
    auto& ownMask   = (origin == MidiOrigin::External) ? audioExternalNotesMask : audioGuiHeldNotesMask;
    const auto& otherMask = (origin == MidiOrigin::External) ? audioGuiHeldNotesMask : audioExternalNotesMask;

    if (message.isNoteOn())
    {
        const int n = message.getNoteNumber();
        if (n >= 0 && n < 128)
        {
            const size_t w = static_cast<size_t>(n >> 6);
            const uint64_t bit = 1ULL << (n & 63);
            if ((ownMask[w] & bit) == 0u)
            {
                ownMask[w] |= bit;
                if (origin == MidiOrigin::External)
                    outExternalMaskChanged = true;
            }
        }
        handleMidiEvent(message, attackMs, punchAmount);
        return;
    }

    if (message.isNoteOff())
    {
        const int n = message.getNoteNumber();
        bool heldByOther = false;
        if (n >= 0 && n < 128)
        {
            const size_t w = static_cast<size_t>(n >> 6);
            const uint64_t bit = 1ULL << (n & 63);
            if ((ownMask[w] & bit) != 0u)
            {
                ownMask[w] &= ~bit;
                if (origin == MidiOrigin::External)
                    outExternalMaskChanged = true;
            }
            heldByOther = ((otherMask[w] & bit) != 0u);
        }

        if (!heldByOther)
            handleMidiEvent(message, attackMs, punchAmount);
        return;
    }

    // CC 123 (All Notes Off) from DAW/External must release external notes without silencing GUI-held notes
    if (message.isAllNotesOff() && origin == MidiOrigin::External)
    {
        const std::array<uint64_t, 2> prevExt = audioExternalNotesMask;
        if (audioExternalNotesMask[0] != 0u || audioExternalNotesMask[1] != 0u)
        {
            audioExternalNotesMask = { 0u, 0u };
            outExternalMaskChanged = true;
        }

        const bool guiHasHeld = (audioGuiHeldNotesMask[0] != 0u || audioGuiHeldNotesMask[1] != 0u);
        if (!guiHasHeld)
        {
            handleMidiEvent(message, attackMs, punchAmount);
        }
        else
        {
            for (int w = 0; w < 2; ++w)
            {
                const uint64_t onlyExternal = prevExt[static_cast<size_t>(w)]
                                            & ~audioGuiHeldNotesMask[static_cast<size_t>(w)];
                if (onlyExternal == 0u)
                    continue;

                for (int b = 0; b < 64; ++b)
                {
                    if ((onlyExternal & (1ULL << b)) != 0u)
                        handleMidiEvent(juce::MidiMessage::noteOff(1, w * 64 + b), attackMs, punchAmount);
                }
            }
        }
        return;
    }

    if (message.isAllSoundOff() || message.isAllNotesOff())
    {
        if (origin == MidiOrigin::External && (audioExternalNotesMask[0] != 0u || audioExternalNotesMask[1] != 0u))
        {
            audioExternalNotesMask = { 0u, 0u };
            outExternalMaskChanged = true;
        }
        else if (origin == MidiOrigin::Gui)
        {
            audioGuiHeldNotesMask = { 0u, 0u };
        }
        handleMidiEvent(message, attackMs, punchAmount);
        return;
    }

    handleMidiEvent(message, attackMs, punchAmount);
}

void AllyBassAudioProcessor::dispatchGuiQueueAndOverflow(const GuiMidiEvent* events,
                                                         int numEvents,
                                                         bool hadOverflow,
                                                         float attackMs,
                                                         float punchAmount)
{
    bool unusedExternalChanged = false;
    for (int i = 0; i < numEvents; ++i)
    {
        const auto& ev = events[static_cast<size_t>(i)];
        const auto msg = ev.isNoteOn
            ? juce::MidiMessage::noteOn(ev.channel, ev.note, ev.velocity)
            : juce::MidiMessage::noteOff(ev.channel, ev.note);
        dispatchSourcedMidiMessage(MidiOrigin::Gui, msg, attackMs, punchAmount, unusedExternalChanged);
    }

    if (hadOverflow)
        reconcileGuiOverflowNotes(attackMs, punchAmount);
}

void AllyBassAudioProcessor::handleNoteOn(juce::MidiKeyboardState*, int midiChannel, int midiNoteNumber, float velocity)
{
    if (suppressGuiMidiForwarding || midiNoteNumber < 0 || midiNoteNumber >= 128)
        return;

    const float clampedVel = std::isfinite(velocity) ? juce::jlimit(0.0f, 1.0f, velocity) : 0.9f;
    guiDesiredVelocities[static_cast<size_t>(midiNoteNumber)].store(clampedVel, std::memory_order_relaxed);
    guiLastPressedNote.store(midiNoteNumber, std::memory_order_relaxed);

    const size_t word = static_cast<size_t>(midiNoteNumber >> 6);
    const uint64_t bit = 1ULL << (midiNoteNumber & 63);
    guiDesiredNotesMask[word].fetch_or(bit, std::memory_order_release);

    pushGuiMidiEvent({
        true,
        static_cast<uint8_t>(juce::jlimit(1, 16, midiChannel)),
        static_cast<uint8_t>(midiNoteNumber),
        clampedVel
    });
}

void AllyBassAudioProcessor::handleNoteOff(juce::MidiKeyboardState* source, int midiChannel, int midiNoteNumber, float velocity)
{
    if (suppressGuiMidiForwarding || midiNoteNumber < 0 || midiNoteNumber >= 128)
        return;

    const size_t word = static_cast<size_t>(midiNoteNumber >> 6);
    const uint64_t bit = 1ULL << (midiNoteNumber & 63);
    guiDesiredNotesMask[word].fetch_and(~bit, std::memory_order_release);

    // Keep key illuminated on shared keyboardState if DAW/external MIDI still holds this pitch
    if ((externalMidiNotesMask[word].load(std::memory_order_acquire) & bit) != 0u && source != nullptr)
    {
        suppressGuiMidiForwarding = true;
        source->noteOn(1, midiNoteNumber, 0.9f);
        suppressGuiMidiForwarding = false;
    }

    const float clampedVel = std::isfinite(velocity) ? juce::jlimit(0.0f, 1.0f, velocity) : 0.0f;
    pushGuiMidiEvent({
        false,
        static_cast<uint8_t>(juce::jlimit(1, 16, midiChannel)),
        static_cast<uint8_t>(midiNoteNumber),
        clampedVel
    });
}

void AllyBassAudioProcessor::pushGuiMidiEvent(const GuiMidiEvent& ev) noexcept
{
    const uint32_t write = guiMidiWriteIdx.load(std::memory_order_relaxed);
    const uint32_t read  = guiMidiReadIdx.load(std::memory_order_acquire);

    if ((write - read) < static_cast<uint32_t>(guiMidiQueueCapacity))
    {
        guiToAudioMidiQueue[write & (guiMidiQueueCapacity - 1)] = ev;
        guiMidiWriteIdx.store(write + 1u, std::memory_order_release);
    }
    else
    {
        // Bounded overflow safety: record overflow so audio reconciles authoritative 128-note mask (zero stuck notes)
        guiQueueOverflowCount.fetch_add(1, std::memory_order_relaxed);
        guiMidiOverflowPending.store(true, std::memory_order_release);
    }
}

void AllyBassAudioProcessor::syncExternalMidiToKeyboardState(uint64_t& inOutLastVersion,
                                                             std::array<uint64_t, 2>& inOutAppliedExternalMask)
{
    const uint64_t version = externalMidiVersion.load(std::memory_order_acquire);
    if (version == inOutLastVersion)
        return;

    inOutLastVersion = version;
    const std::array<uint64_t, 2> targetMask = {
        externalMidiNotesMask[0].load(std::memory_order_acquire),
        externalMidiNotesMask[1].load(std::memory_order_acquire)
    };
    const std::array<uint64_t, 2> guiHeld = {
        guiDesiredNotesMask[0].load(std::memory_order_acquire),
        guiDesiredNotesMask[1].load(std::memory_order_acquire)
    };

    suppressGuiMidiForwarding = true;
    for (int w = 0; w < 2; ++w)
    {
        const uint64_t diff = inOutAppliedExternalMask[static_cast<size_t>(w)] ^ targetMask[static_cast<size_t>(w)];
        if (diff == 0u)
            continue;

        for (int b = 0; b < 64; ++b)
        {
            const uint64_t bit = 1ULL << b;
            if ((diff & bit) != 0u)
            {
                const int note = w * 64 + b;
                if ((targetMask[static_cast<size_t>(w)] & bit) != 0u)
                    keyboardState.noteOn(1, note, 0.9f);
                else if ((guiHeld[static_cast<size_t>(w)] & bit) == 0u)
                    keyboardState.noteOff(1, note, 0.0f);
            }
        }

        inOutAppliedExternalMask[static_cast<size_t>(w)] = targetMask[static_cast<size_t>(w)];
    }
    suppressGuiMidiForwarding = false;
}

void AllyBassAudioProcessor::releaseGuiHeldKeysOnKeyboardState()
{
    const std::array<uint64_t, 2> guiHeld = {
        guiDesiredNotesMask[0].load(std::memory_order_acquire),
        guiDesiredNotesMask[1].load(std::memory_order_acquire)
    };

    for (int w = 0; w < 2; ++w)
    {
        const uint64_t mask = guiHeld[static_cast<size_t>(w)];
        if (mask == 0u)
            continue;

        for (int b = 0; b < 64; ++b)
        {
            if ((mask & (1ULL << b)) != 0u)
            {
                const int note = w * 64 + b;
                keyboardState.noteOff(1, note, 0.0f);
            }
        }
    }
}

void AllyBassAudioProcessor::reconcileGuiOverflowNotes(float attackMs, float punchAmount)
{
    // Reconcile against authoritative 128-note atomic mask & velocities (happens-before via acquire)
    const std::array<uint64_t, 2> desired = {
        guiDesiredNotesMask[0].load(std::memory_order_acquire),
        guiDesiredNotesMask[1].load(std::memory_order_acquire)
    };
    const int lastPressed = guiLastPressedNote.load(std::memory_order_acquire);

    bool unusedExternalChanged = false;

    // 1. Release all stale held notes first so overflow never leaves stuck sound
    for (int w = 0; w < 2; ++w)
    {
        const uint64_t staleOn = audioGuiHeldNotesMask[static_cast<size_t>(w)]
                               & ~desired[static_cast<size_t>(w)];
        if (staleOn != 0u)
        {
            for (int b = 0; b < 64; ++b)
            {
                const uint64_t bit = 1ULL << b;
                if ((staleOn & bit) != 0u)
                {
                    const int note = w * 64 + b;
                    dispatchSourcedMidiMessage(MidiOrigin::Gui, juce::MidiMessage::noteOff(1, note),
                                               attackMs, punchAmount, unusedExternalChanged);
                }
            }
        }
    }

    // 2. Recover any still-held NoteOn dropped during overflow (preserving velocity and last-note priority)
    bool triggerLastPressed = false;
    for (int w = 0; w < 2; ++w)
    {
        const uint64_t missingOn = desired[static_cast<size_t>(w)]
                                 & ~audioGuiHeldNotesMask[static_cast<size_t>(w)];
        if (missingOn != 0u)
        {
            for (int b = 0; b < 64; ++b)
            {
                const uint64_t bit = 1ULL << b;
                if ((missingOn & bit) != 0u)
                {
                    const int note = w * 64 + b;
                    if (note == lastPressed)
                    {
                        triggerLastPressed = true;
                        continue;
                    }
                    const float vel = juce::jlimit(
                        0.0f, 1.0f, guiDesiredVelocities[static_cast<size_t>(note)].load(std::memory_order_relaxed));
                    dispatchSourcedMidiMessage(MidiOrigin::Gui, juce::MidiMessage::noteOn(1, note, vel),
                                               attackMs, punchAmount, unusedExternalChanged);
                }
            }
        }
    }

    if (triggerLastPressed && lastPressed >= 0 && lastPressed < 128)
    {
        const float vel = juce::jlimit(
            0.0f, 1.0f, guiDesiredVelocities[static_cast<size_t>(lastPressed)].load(std::memory_order_relaxed));
        dispatchSourcedMidiMessage(MidiOrigin::Gui, juce::MidiMessage::noteOn(1, lastPressed, vel),
                                   attackMs, punchAmount, unusedExternalChanged);
    }
}

void AllyBassAudioProcessor::publishScopeHistoryToTripleBuffer() noexcept
{
    auto& dst = scopeTripleSlots[scopeProducerSlot];
    const int tailLen = visualizerBufferSize - scopeHistoryWritePos;
    std::copy_n(scopeHistory.data() + scopeHistoryWritePos, static_cast<size_t>(tailLen), dst.data());
    if (scopeHistoryWritePos > 0)
        std::copy_n(scopeHistory.data(), static_cast<size_t>(scopeHistoryWritePos), dst.data() + tailLen);

    const uint8_t prev = scopeTripleMiddle.exchange(
        static_cast<uint8_t>(scopeProducerSlot | kTripleDirtyBit), std::memory_order_acq_rel);
    scopeProducerSlot = static_cast<uint8_t>(prev & kTripleSlotMask);
}

void AllyBassAudioProcessor::processBlock(juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midiMessages)
{
    juce::ScopedNoDenormals noDenormals;

    // Drain bounded lock-free GUI MIDI queue without touching keyboardState mutex or allocating heap memory
    std::array<GuiMidiEvent, guiMidiQueueCapacity> localGuiEvents {};
    int numGuiEvents = 0;
    {
        uint32_t read = guiMidiReadIdx.load(std::memory_order_relaxed);
        const uint32_t write = guiMidiWriteIdx.load(std::memory_order_acquire);
        while (read != write && numGuiEvents < guiMidiQueueCapacity)
        {
            localGuiEvents[static_cast<size_t>(numGuiEvents++)] = guiToAudioMidiQueue[read & (guiMidiQueueCapacity - 1)];
            ++read;
        }
        if (numGuiEvents > 0)
            guiMidiReadIdx.store(read, std::memory_order_release);
    }
    const bool hadGuiOverflow = guiMidiOverflowPending.exchange(false, std::memory_order_acq_rel);

    const int totalNumInputChannels  = getTotalNumInputChannels();
    const int totalNumOutputChannels = getTotalNumOutputChannels();

    for (int i = totalNumInputChannels; i < totalNumOutputChannels; ++i)
        buffer.clear(i, 0, buffer.getNumSamples());

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0 || totalNumOutputChannels <= 0 || buffer.getNumChannels() <= 0)
    {
        // Even on 0-sample host ping, drain GUI events so noteOff/overflow never leaves hung state
        const float idleAttack = loadFiniteParam(rawAttack, 8.0f, 1.0f, 300.0f);
        const float idlePunch = loadFiniteParam(rawPolderPunch, 0.0f, 0.0f, 1.0f);
        dispatchGuiQueueAndOverflow(localGuiEvents.data(), numGuiEvents, hadGuiOverflow, idleAttack, idlePunch);
        return;
    }

    // Fetch cached parameter pointers directly with finite-float clamping (zero map lookup, zero NaN propagation)
    const float diepte           = loadFiniteParam(rawDiepte, 0.50f, 0.0f, 1.0f);
    const float polderPunch      = loadFiniteParam(rawPolderPunch, 0.0f, 0.0f, 1.0f);
    const float havenDirt        = loadFiniteParam(rawHavenDirt, 0.15f, 0.0f, 1.0f);
    const float waterwijkGlideMs = loadFiniteParam(rawWaterwijkGlide, 40.0f, 0.0f, 500.0f);
    const float masterVolDb      = loadFiniteParam(rawMasterVolume, 0.0f, -24.0f, 6.0f);
    const float masterGain       = juce::Decibels::decibelsToGain(masterVolDb);

    const float attackMs       = loadFiniteParam(rawAttack, 8.0f, 1.0f, 300.0f);
    const float releaseMs      = loadFiniteParam(rawRelease, 250.0f, 10.0f, 3000.0f);
    const int stadsdeelIndex   = juce::jlimit(0, 3, juce::roundToInt(loadFiniteParam(rawStadsdeel, 1.0f, 0.0f, 3.0f)));
    const bool isOverdrive     = loadFiniteParam(rawOverdrive036, 0.0f, 0.0f, 1.0f) > 0.5f;
    const bool isMonoLock      = loadFiniteParam(rawMonoLock1300, 1.0f, 0.0f, 1.0f) > 0.5f;

    updateEnvelopeRates(attackMs, releaseMs);
    waterwijkGlide.updateGlideCoeff(waterwijkGlideMs, waterwijkGlide.getCurrentVelocity());

    const bool isVoiceSilentBeforeBlock =
        (envState == EnvelopeState::Idle && envLevel <= 1e-6f && !polderTransient.getIsActive());

    if (isVoiceSilentBeforeBlock)
    {
        smoothedDiepte.setCurrentAndTargetValue(diepte);
        smoothedHavenDirt.setCurrentAndTargetValue(havenDirt);
        smoothedMasterGain.setCurrentAndTargetValue(masterGain);
    }
    else
    {
        smoothedDiepte.setTargetValue(diepte);
        smoothedHavenDirt.setTargetValue(havenDirt);
        smoothedMasterGain.setTargetValue(masterGain);
    }

    bool externalMidiMaskChanged = false;

    auto* leftOut = buffer.getWritePointer(0);
    auto* rightOut = totalNumOutputChannels > 1 ? buffer.getWritePointer(1) : nullptr;

    auto midiIterator = midiMessages.cbegin();
    const auto midiEnd = midiMessages.cend();
    float sumSubRms = 0.0f;

    dispatchGuiQueueAndOverflow(localGuiEvents.data(), numGuiEvents, hadGuiOverflow, attackMs, polderPunch);

    for (int sample = 0; sample < numSamples; ++sample)
    {
        // Process all MIDI events at (or before) this exact sample position sequentially
        while (midiIterator != midiEnd && (*midiIterator).samplePosition <= sample)
        {
            const auto msg = (*midiIterator).getMessage();
            dispatchSourcedMidiMessage(MidiOrigin::External, msg, attackMs, polderPunch, externalMidiMaskChanged);
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
        const float sampleDiepte = smoothedDiepte.getNextValue();
        const float sampleDirt   = smoothedHavenDirt.getNextValue();
        float synthSample = 0.0f;

        if (ampEnv > 0.0f || polderTransient.getIsActive())
        {
            // 1. NAP Sub-Oscillator (Clean low Sine/Warm hybrid)
            float subSample = (ampEnv > 0.0f)
                ? (napSubOsc.processSample(instantFreq, sampleDiepte) * ampEnv)
                : 0.0f;

            // 2. Click-free Polder Punch transient
            float punchSample = polderTransient.processSample(polderPunch);
            synthSample = subSample + punchSample;

            // 3. Stadsdeel Saturation
            synthSample = stadsdeelSaturation.processSample(
                synthSample, sampleDirt, static_cast<AllyDSP::StadsdeelMode>(stadsdeelIndex));

            if (!std::isfinite(synthSample))
                synthSample = 0.0f;
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
        const auto msg = (*midiIterator).getMessage();
        dispatchSourcedMidiMessage(MidiOrigin::External, msg, attackMs, polderPunch, externalMidiMaskChanged);
        ++midiIterator;
    }

    if (externalMidiMaskChanged)
    {
        externalMidiNotesMask[0].store(audioExternalNotesMask[0], std::memory_order_release);
        externalMidiNotesMask[1].store(audioExternalNotesMask[1], std::memory_order_release);
        externalMidiVersion.fetch_add(1, std::memory_order_release);
    }

    // Update real-time pre-postfilter sub RMS telemetry
    float rms = std::sqrt(sumSubRms / static_cast<float>(numSamples));
    currentSubLevel.store(std::isfinite(rms) ? rms : 0.0f, std::memory_order_relaxed);

    // 4. Master Filter
    juce::dsp::AudioBlock<float> block(buffer);
    juce::dsp::ProcessContextReplacing<float> context(block);
    masterLowPass.process(context);

    // 5. 036 OVERDRIVE (Parallel compression + 6dB push)
    overdrive036.process(buffer, isOverdrive);

    // 6. 1300 MONO LOCK (Strict mono sub < 90Hz)
    monoLockFilter.process(buffer, isMonoLock);

    // 7. Master Output Volume (per-sample SR-invariant smoothed gain)
    if (smoothedMasterGain.isSmoothing())
        smoothedMasterGain.applyGain(buffer, numSamples);
    else if (std::abs(smoothedMasterGain.getCurrentValue() - 1.0f) > 1e-6f)
        buffer.applyGain(smoothedMasterGain.getCurrentValue());

    // 8. Wait-free triple-buffered post-output telemetry (Scope snapshot + coherent Meter metrics)
    {
        const int activeChannels = juce::jmin(buffer.getNumChannels(), juce::jlimit(1, 2, totalNumOutputChannels));
        float* ch0W = buffer.getWritePointer(0);
        float* ch1W = (activeChannels > 1) ? buffer.getWritePointer(1) : nullptr;

        const int scopeStart = (numSamples > visualizerBufferSize) ? (numSamples - visualizerBufferSize) : 0;
        double sumSquares = 0.0;
        float blockPeakL = 0.0f;
        float blockPeakR = 0.0f;

        for (int s = 0; s < numSamples; ++s)
        {
            float l = (ch0W != nullptr) ? ch0W[s] : 0.0f;
            if (!std::isfinite(l))
            {
                l = 0.0f;
                if (ch0W != nullptr) ch0W[s] = 0.0f;
            }

            float r = (ch1W != nullptr) ? ch1W[s] : l;
            if (!std::isfinite(r))
            {
                r = 0.0f;
                if (ch1W != nullptr) ch1W[s] = 0.0f;
            }

            blockPeakL = juce::jmax(blockPeakL, std::abs(l));
            sumSquares += static_cast<double>(l) * static_cast<double>(l);

            if (ch1W != nullptr)
            {
                blockPeakR = juce::jmax(blockPeakR, std::abs(r));
                sumSquares += static_cast<double>(r) * static_cast<double>(r);
            }
            else
            {
                blockPeakR = blockPeakL;
            }

            if (s >= scopeStart)
            {
                scopeHistory[static_cast<size_t>(scopeHistoryWritePos)] = (ch1W != nullptr) ? (0.5f * (l + r)) : l;
                scopeHistoryWritePos = (scopeHistoryWritePos + 1) & (visualizerBufferSize - 1);
            }
        }

        publishScopeHistoryToTripleBuffer();
        visualizerWriteCounter.fetch_add(1, std::memory_order_release);

        const float blockPeak = juce::jmax(blockPeakL, blockPeakR);
        const float blockRms = static_cast<float>(
            std::sqrt(sumSquares / static_cast<double>(numSamples * juce::jmax(1, activeChannels))));

        ++audioMeterSeq;
        auto& mSlot = meterTripleSlots[meterProducerSlot];
        mSlot.peakLinear      = blockPeak;
        mSlot.rmsLinear       = blockRms;
        mSlot.leftPeakLinear  = blockPeakL;
        mSlot.rightPeakLinear = blockPeakR;
        mSlot.channelCount    = activeChannels;
        mSlot.updateCounter   = audioMeterSeq;

        const uint8_t prevMeter = meterTripleMiddle.exchange(
            static_cast<uint8_t>(meterProducerSlot | kTripleDirtyBit), std::memory_order_acq_rel);
        meterProducerSlot = static_cast<uint8_t>(prevMeter & kTripleSlotMask);

        // Lock-free atomic fetch-max so brief transients between GUI refreshes are captured reliably
        float expectedPeak = outputPeakLatched.load(std::memory_order_relaxed);
        while (blockPeak > expectedPeak
               && !outputPeakLatched.compare_exchange_weak(expectedPeak, blockPeak,
                                                           std::memory_order_release,
                                                           std::memory_order_relaxed))
        {
        }

        float expectedRms = outputRmsLatched.load(std::memory_order_relaxed);
        while (blockRms > expectedRms
               && !outputRmsLatched.compare_exchange_weak(expectedRms, blockRms,
                                                          std::memory_order_release,
                                                          std::memory_order_relaxed))
        {
        }

        if (blockPeak >= 1.0f)
            outputClipLatched.store(true, std::memory_order_release);

        outputMeterCounter.store(audioMeterSeq, std::memory_order_release);
    }
}

void AllyBassAudioProcessor::getVisualizerData(float* destination, int numSamples)
{
    if (destination == nullptr || numSamples <= 0)
        return;

    const int count = juce::jmin(numSamples, visualizerBufferSize);
    for (int i = 0; i < numSamples - count; ++i)
        destination[i] = 0.0f;

    const uint8_t mid = scopeTripleMiddle.load(std::memory_order_relaxed);
    if ((mid & kTripleDirtyBit) != 0u)
    {
        const uint8_t prev = scopeTripleMiddle.exchange(scopeConsumerSlot, std::memory_order_acq_rel);
        scopeConsumerSlot = static_cast<uint8_t>(prev & kTripleSlotMask);
    }

    const auto& src = scopeTripleSlots[scopeConsumerSlot];
    const int srcStart = visualizerBufferSize - count;
    float* dst = destination + (numSamples - count);
    for (int i = 0; i < count; ++i)
    {
        const float val = src[static_cast<size_t>(srcStart + i)];
        dst[i] = std::isfinite(val) ? val : 0.0f;
    }
}

uint64_t AllyBassAudioProcessor::getVisualizerWriteCounter() const noexcept
{
    return visualizerWriteCounter.load(std::memory_order_acquire);
}

bool AllyBassAudioProcessor::isOverdriveActive() const noexcept
{
    return loadFiniteParam(rawOverdrive036, 0.0f, 0.0f, 1.0f) > 0.5f;
}

bool AllyBassAudioProcessor::isMonoLockActive() const noexcept
{
    return loadFiniteParam(rawMonoLock1300, 1.0f, 0.0f, 1.0f) > 0.5f;
}

float AllyBassAudioProcessor::getCurrentSubLevel() const noexcept
{
    return currentSubLevel.load(std::memory_order_relaxed);
}

double AllyBassAudioProcessor::getCurrentSampleRate() const noexcept
{
    return currentSampleRateHz.load(std::memory_order_relaxed);
}

AllyBassAudioProcessor::OutputMeterMetrics AllyBassAudioProcessor::pullLatestMeterBlock() const noexcept
{
    const uint8_t mid = meterTripleMiddle.load(std::memory_order_relaxed);
    if ((mid & kTripleDirtyBit) != 0u)
    {
        const uint8_t prev = meterTripleMiddle.exchange(meterConsumerSlot, std::memory_order_acq_rel);
        meterConsumerSlot = static_cast<uint8_t>(prev & kTripleSlotMask);
    }

    const auto& snap = meterTripleSlots[meterConsumerSlot];
    OutputMeterMetrics m;
    m.peakLinear      = snap.peakLinear;
    m.rmsLinear       = snap.rmsLinear;
    m.leftPeakLinear  = snap.leftPeakLinear;
    m.rightPeakLinear = snap.rightPeakLinear;
    m.channelCount    = snap.channelCount;
    m.updateCounter   = snap.updateCounter;
    return m;
}

AllyBassAudioProcessor::OutputMeterMetrics AllyBassAudioProcessor::peekOutputMeterMetrics() const noexcept
{
    OutputMeterMetrics m = pullLatestMeterBlock();
    const float latchedP = outputPeakLatched.load(std::memory_order_acquire);
    const float latchedR = outputRmsLatched.load(std::memory_order_acquire);
    const bool  latchedC = outputClipLatched.load(std::memory_order_acquire);

    m.peakLinear    = juce::jmax(m.peakLinear, latchedP);
    m.rmsLinear     = juce::jmax(m.rmsLinear, latchedR);
    m.clipped       = latchedC || (m.peakLinear >= 1.0f);
    m.updateCounter = outputMeterCounter.load(std::memory_order_acquire);
    return m;
}

AllyBassAudioProcessor::OutputMeterMetrics AllyBassAudioProcessor::consumeOutputMeterMetrics() noexcept
{
    OutputMeterMetrics m = pullLatestMeterBlock();
    const float latchedP = outputPeakLatched.exchange(0.0f, std::memory_order_acq_rel);
    const float latchedR = outputRmsLatched.exchange(0.0f, std::memory_order_acq_rel);
    const bool  latchedC = outputClipLatched.exchange(false, std::memory_order_acq_rel);

    m.peakLinear    = juce::jmax(m.peakLinear, latchedP);
    m.rmsLinear     = juce::jmax(m.rmsLinear, latchedR);
    m.clipped       = latchedC || (m.peakLinear >= 1.0f);
    m.updateCounter = outputMeterCounter.load(std::memory_order_acquire);
    return m;
}

void AllyBassAudioProcessor::resetOutputTelemetry() noexcept
{
    scopeHistory.fill(0.0f);
    scopeHistoryWritePos = 0;
    scopeTripleSlots[scopeProducerSlot].fill(0.0f);
    const uint8_t prevScope = scopeTripleMiddle.exchange(
        static_cast<uint8_t>(scopeProducerSlot | kTripleDirtyBit), std::memory_order_acq_rel);
    scopeProducerSlot = static_cast<uint8_t>(prevScope & kTripleSlotMask);

    visualizerWriteCounter.store(0, std::memory_order_release);
    currentSubLevel.store(0.0f, std::memory_order_relaxed);

    audioMeterSeq = 0;
    meterTripleSlots[meterProducerSlot] = OutputMeterBlockSnapshot {};
    const uint8_t prevMeter = meterTripleMiddle.exchange(
        static_cast<uint8_t>(meterProducerSlot | kTripleDirtyBit), std::memory_order_acq_rel);
    meterProducerSlot = static_cast<uint8_t>(prevMeter & kTripleSlotMask);

    outputPeakLatched.store(0.0f, std::memory_order_release);
    outputRmsLatched.store(0.0f, std::memory_order_release);
    outputClipLatched.store(false, std::memory_order_release);
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
