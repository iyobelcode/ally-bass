#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace AllyDSP
{

/**
 * Polder Transient Module (Subtle Warm Knock / Punch)
 * Re-engineered to eliminate harsh high-frequency chirps or metallic distortion singing.
 * Focuses purely on low-end weight (80 - 150 Hz) and only activates when Punch > 0.
 */
class PolderTransient
{
public:
    PolderTransient() = default;

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
        reset();
    }

    void reset()
    {
        transientPhase = 0.0f;
        ampEnvelope = 0.0f;
        pitchEnvelope = 0.0f;
        isActive = false;
    }

    void trigger(float velocity, float punchAmount)
    {
        if (punchAmount <= 0.01f)
        {
            isActive = false;
            return;
        }

        isActive = true;
        transientPhase = 0.0f;
        ampEnvelope = velocity * punchAmount;
        pitchEnvelope = 1.0f;

        // Warm rounded thump decay: ~20 ms
        float ampDecayTime = 0.020f;
        ampDecayCoeff = std::exp(-1.0f / (ampDecayTime * static_cast<float>(currentSampleRate) / 4.0f));

        // Pitch drop: gentle low drop from ~160 Hz down to fundamental (no high laser whistling)
        float pitchDecayTime = 0.015f;
        pitchDecayCoeff = std::exp(-1.0f / (pitchDecayTime * static_cast<float>(currentSampleRate) / 4.0f));
    }

    /**
     * Process one sample of the transient generator.
     */
    float processSample(float punchAmount)
    {
        if (!isActive || punchAmount <= 0.01f || currentSampleRate <= 0.0)
            return 0.0f;

        const float twoPi = juce::MathConstants<float>::twoPi;

        // Warm low thump frequency: starts at ~160 Hz and slides down to 55 Hz
        float instantFreq = 55.0f + 105.0f * (pitchEnvelope * pitchEnvelope);

        float thumpSample = std::sin(transientPhase * twoPi);
        transientPhase += instantFreq / static_cast<float>(currentSampleRate);
        if (transientPhase >= 1.0f)
            transientPhase -= 1.0f;

        float punchSignal = thumpSample * ampEnvelope;

        // Apply decay envelopes
        ampEnvelope *= ampDecayCoeff;
        pitchEnvelope *= pitchDecayCoeff;

        if (ampEnvelope < 0.0001f)
        {
            isActive = false;
        }

        return punchSignal;
    }

    bool getIsActive() const noexcept { return isActive; }

private:
    double currentSampleRate = 44100.0;

    float transientPhase = 0.0f;
    float ampEnvelope = 0.0f;
    float pitchEnvelope = 0.0f;

    float ampDecayCoeff = 0.98f;
    float pitchDecayCoeff = 0.97f;

    bool isActive = false;
};

} // namespace AllyDSP
