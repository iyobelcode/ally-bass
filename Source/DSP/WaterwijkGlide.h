#pragma once
#include <juce_core/juce_core.h>
#include <array>
#include <cmath>

namespace AllyDSP
{

class WaterwijkGlide
{
public:
    static constexpr int maxActiveNotes = 128;

    WaterwijkGlide() = default;

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate > 1.0 ? sampleRate : 44100.0;
        reset();
    }

    void reset()
    {
        activeNoteCount = 0;
        currentFrequency = 0.0f;
        targetFrequency = 0.0f;
        currentVelocity = 0.0f;
        glideCoeff = 1.0f;
        isNoteActive = false;
        isLegato = false;
        triggerTransient = false;
        resetPhase = false;
    }

    void noteOn(int midiNoteNumber, float velocity, float glideTimeMs)
    {
        // Monofone note stack op vaste stack-array (nul heap-allocaties in realtime audiothread)
        for (int i = 0; i < activeNoteCount; ++i)
        {
            if (activeNotes[static_cast<size_t>(i)] == midiNoteNumber)
            {
                for (int j = i + 1; j < activeNoteCount; ++j)
                    activeNotes[static_cast<size_t>(j - 1)] = activeNotes[static_cast<size_t>(j)];
                --activeNoteCount;
                break;
            }
        }

        if (activeNoteCount >= maxActiveNotes)
        {
            for (int j = 1; j < activeNoteCount; ++j)
                activeNotes[static_cast<size_t>(j - 1)] = activeNotes[static_cast<size_t>(j)];
            --activeNoteCount;
        }
        activeNotes[static_cast<size_t>(activeNoteCount++)] = midiNoteNumber;

        float targetFreq = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(midiNoteNumber));
        currentVelocity = velocity;

        if (!isNoteActive)
        {
            // Eerste nootaanslag vanuit stilte
            currentFrequency = targetFreq;
            targetFrequency = targetFreq;
            isNoteActive = true;
            isLegato = false;
            triggerTransient = true;
            resetPhase = false;
        }
        else
        {
            // Legato overgang naar nieuwe noot
            targetFrequency = targetFreq;
            isLegato = (glideTimeMs > 0.001f);
            if (!isLegato)
                currentFrequency = targetFreq; // Zonder glide direct naar doelpitch
            triggerTransient = false;
            resetPhase = false;
        }

        updateGlideCoeff(glideTimeMs, velocity);
    }

    void noteOff(int midiNoteNumber, float glideTimeMs = 0.0f)
    {
        for (int i = 0; i < activeNoteCount; ++i)
        {
            if (activeNotes[static_cast<size_t>(i)] == midiNoteNumber)
            {
                for (int j = i + 1; j < activeNoteCount; ++j)
                    activeNotes[static_cast<size_t>(j - 1)] = activeNotes[static_cast<size_t>(j)];
                --activeNoteCount;
                break;
            }
        }

        if (activeNoteCount == 0)
        {
            isNoteActive = false;
            isLegato = false;
        }
        else
        {
            // Als er nog een eerder vastgehouden noot in de stack zit: terugkeren (en gliden indien aan)
            int prevNote = activeNotes[static_cast<size_t>(activeNoteCount - 1)];
            float prevFreq = static_cast<float>(juce::MidiMessage::getMidiNoteInHertz(prevNote));
            targetFrequency = prevFreq;
            isLegato = (glideTimeMs > 0.001f);
            if (!isLegato)
                currentFrequency = prevFreq;
            updateGlideCoeff(glideTimeMs, currentVelocity);
        }
    }

    void updateGlideCoeff(float glideTimeMs, float velocity)
    {
        if (glideTimeMs <= 0.001f || !isLegato)
        {
            glideCoeff = 1.0f;
            return;
        }

        // Waterwijk Dynamic Glide: Higher velocity = faster slide curve
        // Velocity scales glide time from 0.6x (velocity 1.0) up to 1.3x (velocity 0.0)
        float dynamicScale = juce::jlimit(0.5f, 1.4f, 1.3f - 0.7f * velocity);
        float effectiveGlideTimeMs = glideTimeMs * dynamicScale;

        // Exponential smoothing coefficient calculation
        float timeInSeconds = effectiveGlideTimeMs * 0.001f;
        float numSamples = timeInSeconds * static_cast<float>(currentSampleRate);
        
        // 95% convergence in numSamples: exp(-3.0 / numSamples)
        if (numSamples > 1.0f)
            glideCoeff = 1.0f - std::exp(-3.0f / numSamples);
        else
            glideCoeff = 1.0f;
    }

    // Advances frequency by one sample and returns the current instantaneous pitch
    float processNextFrequency()
    {
        if (!isNoteActive && activeNoteCount == 0)
            return targetFrequency;

        if (glideCoeff < 1.0f)
        {
            currentFrequency += glideCoeff * (targetFrequency - currentFrequency);
        }
        else
        {
            currentFrequency = targetFrequency;
        }

        return currentFrequency;
    }

    bool popTransientTrigger()
    {
        bool trig = triggerTransient;
        triggerTransient = false;
        return trig;
    }

    bool popPhaseReset()
    {
        bool reset = resetPhase;
        resetPhase = false;
        return reset;
    }

    bool getIsNoteActive() const noexcept { return isNoteActive; }
    float getCurrentVelocity() const noexcept { return currentVelocity; }
    float getCurrentFrequency() const noexcept { return currentFrequency; }

private:
    double currentSampleRate = 44100.0;
    std::array<int, maxActiveNotes> activeNotes {};
    int activeNoteCount = 0;

    float currentFrequency = 0.0f;
    float targetFrequency = 0.0f;
    float currentVelocity = 0.0f;
    float glideCoeff = 1.0f;

    bool isNoteActive = false;
    bool isLegato = false;
    bool triggerTransient = false;
    bool resetPhase = false;
};

} // namespace AllyDSP
