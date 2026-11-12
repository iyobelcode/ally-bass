#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

namespace AllyDSP
{

/**
 * 1300 MONO LOCK
 * Phase-consistent Mid/Side sub-bass mono lock at 90 Hz:
 * preserves the Mid (L+R)/2 component bit-identically with zero magnitude/phase/transient
 * distortion while high-passing only the Side (L-R)/2 component with a 24 dB/oct
 * cascaded 2nd-order Butterworth (LR4-slope) HPF and a click-free 8 ms bypass crossfade.
 */
class MonoLockFilter
{
public:
    static constexpr float crossoverHz = 90.0f;
    static constexpr double toggleCrossfadeSeconds = 0.008;

    MonoLockFilter() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;
        updateCoefficients();
        reset();
    }

    void reset() noexcept
    {
        sideHighPassStage1.reset();
        sideHighPassStage2.reset();
        sideLockMix = 1.0f;
        hasInitializedMix = false;
    }

    void updateCoefficients()
    {
        if (currentSampleRate <= 0.0)
            return;

        const float q = juce::MathConstants<float>::sqrt2 * 0.5f;
        auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSampleRate, crossoverHz, q);

        sideHighPassStage1.coefficients = hpCoeffs;
        sideHighPassStage2.coefficients = hpCoeffs;
        fadeStep = 1.0f / juce::jmax(1.0f, static_cast<float>(toggleCrossfadeSeconds * currentSampleRate));
    }

    void process(juce::AudioBuffer<float>& buffer, bool monoLockEnabled) noexcept
    {
        if (buffer.getNumChannels() < 2 || buffer.getNumSamples() <= 0)
            return;

        juce::ScopedNoDenormals noDenormals;

        const float targetMix = monoLockEnabled ? 1.0f : 0.0f;
        if (!hasInitializedMix)
        {
            sideLockMix = targetMix;
            hasInitializedMix = true;
        }

        if (targetMix <= 0.0f && sideLockMix <= 0.0f)
            return;

        const int numSamples = buffer.getNumSamples();
        auto* channelL = buffer.getWritePointer(0);
        auto* channelR = buffer.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i)
        {
            if (sideLockMix < targetMix)
                sideLockMix = juce::jmin(targetMix, sideLockMix + fadeStep);
            else if (sideLockMix > targetMix)
                sideLockMix = juce::jmax(targetMix, sideLockMix - fadeStep);

            const float inL = channelL[i];
            const float inR = channelR[i];

            const float mid  = 0.5f * (inL + inR);
            const float side = 0.5f * (inL - inR);

            // 24 dB/oct Side high-pass leaves Mid (and therefore L=R mono + L+R sum) untouched
            const float hpSide = sideHighPassStage2.processSample(sideHighPassStage1.processSample(side));
            const float outSide = side + sideLockMix * (hpSide - side);

            channelL[i] = mid + outSide;
            channelR[i] = mid - outSide;
        }

        if (targetMix <= 0.0f && sideLockMix <= 0.0f)
        {
            sideHighPassStage1.reset();
            sideHighPassStage2.reset();
        }
    }

private:
    double currentSampleRate = 44100.0;
    float fadeStep = 1.0f / 352.8f;
    float sideLockMix = 1.0f;
    bool hasInitializedMix = false;

    juce::dsp::IIR::Filter<float> sideHighPassStage1;
    juce::dsp::IIR::Filter<float> sideHighPassStage2;
};

} // namespace AllyDSP
