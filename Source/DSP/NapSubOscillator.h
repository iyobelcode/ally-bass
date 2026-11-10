#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace AllyDSP
{

/**
 * NAP Sub-Oscillator (Onder Zeeniveau Engine)
 * Ultra-stable sine/triangle hybrid tuned for sub-harmonics between 25 Hz and 80 Hz.
 * Incorporates the NAP-Lock algorithm for phase alignment and mono coherence.
 */
class NapSubOscillator
{
public:
    NapSubOscillator() = default;

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
        reset();
    }

    void reset()
    {
        phase = 0.0f;
        subPhase = 0.0f;
    }

    void resetNapLock()
    {
        // NAP-Lock: Locks phase to zero-crossing on note strike
        phase = 0.0f;
        subPhase = 0.0f;
    }

    /**
     * Generates one sample of the NAP sub oscillator.
     * @param frequency Current fundamental frequency in Hz
     * @param diepte Depth control [0.0 - 1.0]: blends triangle hybrid and sub-octave -4.5m NAP resonance
     */
    float processSample(float frequency, float diepte)
    {
        if (currentSampleRate <= 0.0 || frequency <= 0.0f)
            return 0.0f;

        const float twoPi = juce::MathConstants<float>::twoPi;

        // 1. Pure Sine component
        float sineVal = std::sin(phase * twoPi);

        // 2. Parabolic/Triangle component (richer sub-harmonics)
        // Fast triangle wave approximation from phase: range [-1, 1]
        float triVal = 4.0f * std::abs(phase - 0.5f) - 1.0f;
        // Soften triangle with gentle polynomial to eliminate harsh high-order harmonics
        triVal = triVal * (1.5f - 0.5f * triVal * triVal);

        // Diepte controls the blend between pure sine and warm triangle hybrid
        // At 0.0: Pure sine (deepest sub)
        // At 1.0: Rich triangle hybrid with pronounced 2nd/3rd sub-harmonics
        float hybridVal = (1.0f - diepte * 0.4f) * sineVal + (diepte * 0.4f) * triVal;

        // 3. -4.5m NAP Sub-Resonance Engine:
        // Adds subtle sub-octave weight (~-4.5m Almere zeebodem resonance) when Diepte is turned up
        float subSine = std::sin(subPhase * twoPi);
        float subWeight = diepte * 0.35f;

        float output = hybridVal + subSine * subWeight;

        // Normalize gain so overall RMS stays consistent across Diepte range
        output *= (1.0f / (1.0f + subWeight * 0.5f));

        // Advance phases
        float phaseInc = frequency / static_cast<float>(currentSampleRate);
        phase += phaseInc;
        if (phase >= 1.0f)
            phase -= 1.0f;

        // Sub-octave phase advances at half rate
        float subPhaseInc = phaseInc * 0.5f;
        subPhase += subPhaseInc;
        if (subPhase >= 1.0f)
            subPhase -= 1.0f;

        return output;
    }

private:
    double currentSampleRate = 44100.0;
    float phase = 0.0f;
    float subPhase = 0.0f;
};

} // namespace AllyDSP
