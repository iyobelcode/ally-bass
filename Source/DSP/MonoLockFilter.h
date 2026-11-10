#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>

namespace AllyDSP
{

/**
 * 1300 MONO LOCK
 * Crossover filter at 90 Hz that forces all sub frequencies strictly into mono
 * while preserving stereo width above 90 Hz to eliminate club phase cancellation.
 */
class MonoLockFilter
{
public:
    MonoLockFilter() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;
        updateCoefficients();
        reset();
    }

    void reset()
    {
        lowPassL.reset();
        lowPassR.reset();
        highPassL.reset();
        highPassR.reset();
    }

    void updateCoefficients()
    {
        if (currentSampleRate <= 0.0)
            return;

        // 90 Hz Linkwitz-Riley style 2nd order crossover
        const float cutoff = 90.0f;
        const float q = juce::MathConstants<float>::sqrt2 * 0.5f; // Butterworth 0.7071

        auto lpCoeffs = juce::dsp::IIR::Coefficients<float>::makeLowPass(currentSampleRate, cutoff, q);
        auto hpCoeffs = juce::dsp::IIR::Coefficients<float>::makeHighPass(currentSampleRate, cutoff, q);

        lowPassL.coefficients = lpCoeffs;
        lowPassR.coefficients = lpCoeffs;
        highPassL.coefficients = hpCoeffs;
        highPassR.coefficients = hpCoeffs;
    }

    void process(juce::AudioBuffer<float>& buffer, bool monoLockEnabled)
    {
        if (!monoLockEnabled || buffer.getNumChannels() < 2)
            return;

        const int numSamples = buffer.getNumSamples();
        auto* channelL = buffer.getWritePointer(0);
        auto* channelR = buffer.getWritePointer(1);

        for (int i = 0; i < numSamples; ++i)
        {
            float inL = channelL[i];
            float inR = channelR[i];

            // Split into low and high bands
            float lowL = lowPassL.processSample(inL);
            float lowR = lowPassR.processSample(inR);

            float highL = highPassL.processSample(inL);
            float highR = highPassR.processSample(inR);

            // Hard mono lock for low band
            float monoSub = 0.5f * (lowL + lowR);

            // Recombine mono sub with high-passed stereo
            channelL[i] = monoSub + highL;
            channelR[i] = monoSub + highR;
        }
    }

private:
    double currentSampleRate = 44100.0;
    juce::dsp::IIR::Filter<float> lowPassL;
    juce::dsp::IIR::Filter<float> lowPassR;
    juce::dsp::IIR::Filter<float> highPassL;
    juce::dsp::IIR::Filter<float> highPassR;
};

} // namespace AllyDSP
