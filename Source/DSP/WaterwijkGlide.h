#pragma once
#include <juce_core/juce_core.h>
#include <vector>

namespace AllyDSP
{

class WaterwijkGlide
{
public:
    WaterwijkGlide() = default;

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
        reset();
    }

    void reset()
    {
        activeNotes.clear();
        currentFrequency = 0.0f;
        targetFrequency = 0.0f;
        currentVelocity = 0.0f;
        isNoteActive = false;
        isLegato = false;
        triggerTransient = false;
        resetPhase = false;
    }

    void noteOn(int midiNoteNumber, float velocity, float glideTimeMs)
    {
        // Monofone note stack: verwijder eventueel duplicaat en voeg nieuwe noot bovenaan toe
        auto it = std::find(activeNotes.begin(), activeNotes.end(), midiNoteNumber);
        if (it != activeNotes.end())
            activeNotes.erase(it);
        activeNotes.push_back(midiNoteNumber);

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
        auto it = std::find(activeNotes.begin(), activeNotes.end(), midiNoteNumber);
        if (it != activeNotes.end())
            activeNotes.erase(it);

        if (activeNotes.empty())
        {
            isNoteActive = false;
            isLegato = false;
        }
        else
        {
            // Als er nog een eerder vastgehouden noot in de stack zit: terugkeren (en gliden indien aan)
            int prevNote = activeNotes.back();
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
        if (!isNoteActive && activeNotes.empty())
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
    std::vector<int> activeNotes;

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
