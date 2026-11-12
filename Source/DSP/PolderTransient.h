#pragma once
#include <juce_core/juce_core.h>
#include <cmath>

namespace AllyDSP
{

/**
 * Polder Transient Module (Click-Free Warm 808 Knock / Punch)
 * Generates a short, phase-continuous low-mid knock (250 Hz -> 60 Hz) with a
 * raised-cosine attack ramp and retrigger de-click crossfade.
 * Produces exact 0.0f output when punchAmount is 0.0f.
 */
class PolderTransient
{
public:
    PolderTransient() = default;

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
        const float sr = static_cast<float>(currentSampleRate);

        // Raised-cosine attack (~2.0 ms) eliminates sample-0 slope discontinuity
        attackIncrement = 1.0f / juce::jmax(1.0f, 0.0020f * sr);

        // Sample-rate-invariant exponential decay & smoothing coefficients
        ampDecayCoeff    = std::exp(-1.0f / (0.0140f * sr)); // ~14 ms body decay
        pitchDecayCoeff  = std::exp(-1.0f / (0.0100f * sr)); // ~10 ms pitch drop (~5 ms squared half-life)
        fadeOutCoeff     = std::exp(-1.0f / (0.0020f * sr)); // ~2.0 ms de-click crossfade tail
        punchSmoothCoeff = 1.0f - std::exp(-1.0f / (0.0050f * sr));

        reset();
    }

    void reset()
    {
        transientPhase = 0.0f;
        attackProgress = 0.0f;
        ampEnvelope = 0.0f;
        pitchEnvelope = 0.0f;
        currentVelocity = 0.0f;
        smoothedPunch = 0.0f;
        normalizedEnvelope = 0.0f;
        fadeOutSample = 0.0f;
        lastOutput = 0.0f;
        isActive = false;
    }

    void trigger(float velocity, float punchAmount)
    {
        const float clampedPunch = juce::jlimit(0.0f, 1.0f, punchAmount);
        const float clampedVel = juce::jlimit(0.0f, 1.0f, velocity);
        smoothedPunch = clampedPunch;

        // Crossfade out any interrupted transient tail to prevent step clicks on retrigger
        fadeOutSample = lastOutput;

        if (clampedPunch <= 0.0f || clampedVel <= 0.0f)
        {
            transientPhase = 0.0f;
            attackProgress = 0.0f;
            ampEnvelope = 0.0f;
            pitchEnvelope = 0.0f;
            currentVelocity = 0.0f;
            isActive = false;
            return;
        }

        isActive = true;
        transientPhase = 0.0f;
        attackProgress = 0.0f;
        currentVelocity = clampedVel;
        ampEnvelope = 1.0f;
        pitchEnvelope = 1.0f;
    }

    /**
     * Process one sample of the transient generator.
     * @param punchAmount Continuous punch control [0.0 - 1.0]
     */
    float processSample(float punchAmount)
    {
        if (currentSampleRate <= 0.0)
            return 0.0f;

        const float targetPunch = juce::jlimit(0.0f, 1.0f, punchAmount);
        smoothedPunch += punchSmoothCoeff * (targetPunch - smoothedPunch);
        if (targetPunch <= 0.0f && smoothedPunch < 1e-5f)
            smoothedPunch = 0.0f;

        if (!isActive && std::abs(fadeOutSample) <= 1e-6f)
        {
            fadeOutSample = 0.0f;
            lastOutput = 0.0f;
            normalizedEnvelope = 0.0f;
            return 0.0f;
        }

        const float fade = fadeOutSample;
        fadeOutSample *= fadeOutCoeff;
        if (std::abs(fadeOutSample) < 1e-6f)
            fadeOutSample = 0.0f;

        float knock = 0.0f;
        float currentNormalizedEnv = 0.0f;

        if (isActive && ampEnvelope > 0.0001f)
        {
            const float twoPi = juce::MathConstants<float>::twoPi;

            // Smooth raised-cosine onset ramp [0 -> 1] with zero initial derivative
            float attackGain = 1.0f;
            if (attackProgress < 1.0f)
            {
                attackGain = 0.5f * (1.0f - std::cos(juce::MathConstants<float>::pi * attackProgress));
                attackProgress = juce::jmin(1.0f, attackProgress + attackIncrement);
            }

            // Warm low-mid 808 beater knock: 250 Hz sweeping down to 60 Hz
            const float instantFreq = 60.0f + 190.0f * (pitchEnvelope * pitchEnvelope);

            const float thumpSample = std::sin(transientPhase * twoPi);
            transientPhase += instantFreq / static_cast<float>(currentSampleRate);
            if (transientPhase >= 1.0f)
                transientPhase -= 1.0f;

            const float envShape = attackGain * ampEnvelope;
            currentNormalizedEnv = envShape * smoothedPunch;

            constexpr float maxKnockGain = 0.55f;
            knock = thumpSample * envShape * currentVelocity * smoothedPunch * maxKnockGain;

            ampEnvelope *= ampDecayCoeff;
            pitchEnvelope *= pitchDecayCoeff;

            if (ampEnvelope <= 0.0001f)
            {
                ampEnvelope = 0.0f;
                isActive = false;
            }
        }
        else
        {
            isActive = false;
        }

        normalizedEnvelope = currentNormalizedEnv;

        const float out = knock + fade;
        lastOutput = out;
        return out;
    }

    float getNormalizedEnvelope() const noexcept { return normalizedEnvelope; }
    bool getIsActive() const noexcept { return isActive || std::abs(fadeOutSample) > 1e-6f; }

private:
    double currentSampleRate = 44100.0;

    float transientPhase = 0.0f;
    float attackProgress = 0.0f;
    float ampEnvelope = 0.0f;
    float pitchEnvelope = 0.0f;
    float currentVelocity = 0.0f;
    float smoothedPunch = 0.0f;
    float normalizedEnvelope = 0.0f;
    float fadeOutSample = 0.0f;
    float lastOutput = 0.0f;

    float attackIncrement = 0.01f;
    float ampDecayCoeff = 0.998f;
    float pitchDecayCoeff = 0.997f;
    float fadeOutCoeff = 0.99f;
    float punchSmoothCoeff = 0.005f;

    bool isActive = false;
};

} // namespace AllyDSP
