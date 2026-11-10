#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace AllyDSP
{

/**
 * Polder Transient Module (De Attack/Click)
 * Generates an aggressive kick/click punch transient synchronized to note-on,
 * allowing 808s to cut through smartphone speakers and club PAs.
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
        noiseEnvelope = 0.0f;
        pitchEnvelope = 0.0f;
        isActive = false;
        noiseFilterState = 0.0f;
    }

    void trigger(float velocity)
    {
        isActive = true;
        transientPhase = 0.0f;
        
        // Envelope starts at peak scaled by velocity
        ampEnvelope = velocity;
        noiseEnvelope = velocity * 0.7f;
        pitchEnvelope = 1.0f;

        // Calculate sample-accurate decay rates
        // Amp decay: ~25 ms total punch duration
        float ampDecayTime = 0.025f;
        ampDecayCoeff = std::exp(-1.0f / (ampDecayTime * static_cast<float>(currentSampleRate) / 4.0f));

        // Noise click decay: ultra-fast ~5 ms snap for phone speakers
        float noiseDecayTime = 0.005f;
        noiseDecayCoeff = std::exp(-1.0f / (noiseDecayTime * static_cast<float>(currentSampleRate) / 4.0f));

        // Pitch drop: drops from 1200 Hz down to sub in ~12 ms
        float pitchDecayTime = 0.012f;
        pitchDecayCoeff = std::exp(-1.0f / (pitchDecayTime * static_cast<float>(currentSampleRate) / 4.0f));
    }

    /**
     * Process one sample of the transient generator.
     * @param punchAmount Controls transient volume and knock intensity [0.0 - 1.0]
     */
    float processSample(float punchAmount)
    {
        if (!isActive || punchAmount <= 0.001f || currentSampleRate <= 0.0)
            return 0.0f;

        const float twoPi = juce::MathConstants<float>::twoPi;

        // 1. Chirp Frequency: Starts at 1400 Hz and plunges down to fundamental
        float instantFreq = 80.0f + 1320.0f * (pitchEnvelope * pitchEnvelope);

        // Advance sine chirp
        float chirpSample = std::sin(transientPhase * twoPi);
        transientPhase += instantFreq / static_cast<float>(currentSampleRate);
        if (transientPhase >= 1.0f)
            transientPhase -= 1.0f;

        // 2. High-frequency polder clay click (noise band around 2.4 kHz)
        // White noise generation
        float rawNoise = random.nextFloat() * 2.0f - 1.0f;
        // Simple bandpass filter for the woody/clay click
        noiseFilterState += 0.35f * (rawNoise - noiseFilterState);
        float clickNoise = rawNoise - noiseFilterState; // High pass aspect

        // 3. Combine chirp knock + snap click
        float punchSignal = (chirpSample * ampEnvelope) + (clickNoise * noiseEnvelope * 0.8f);

        // Apply decay envelopes
        ampEnvelope *= ampDecayCoeff;
        noiseEnvelope *= noiseDecayCoeff;
        pitchEnvelope *= pitchDecayCoeff;

        // Check if finished
        if (ampEnvelope < 0.0001f && noiseEnvelope < 0.0001f)
        {
            isActive = false;
        }

        return punchSignal * punchAmount;
    }

    bool getIsActive() const noexcept { return isActive; }

private:
    double currentSampleRate = 44100.0;
    juce::Random random;

    float transientPhase = 0.0f;
    float ampEnvelope = 0.0f;
    float noiseEnvelope = 0.0f;
    float pitchEnvelope = 0.0f;

    float ampDecayCoeff = 0.99f;
    float noiseDecayCoeff = 0.95f;
    float pitchDecayCoeff = 0.98f;

    float noiseFilterState = 0.0f;
    bool isActive = false;
};

} // namespace AllyDSP
