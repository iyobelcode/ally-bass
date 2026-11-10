#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace AllyDSP
{

/**
 * 036 OVERDRIVE
 * Master parallel compression + 6dB extra saturation push for instant Almere club volume.
 */
class Overdrive036
{
public:
    Overdrive036() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        currentSampleRate = spec.sampleRate;
        compressor.prepare(spec);
        compressor.setThreshold(-18.0f);
        compressor.setRatio(6.0f);
        compressor.setAttack(5.0f);   // Fast attack for 808 density
        compressor.setRelease(80.0f); // Fast recovery

        gainSmoothing = 1.0f;
        targetGain = 1.0f;
    }

    void reset()
    {
        compressor.reset();
        gainSmoothing = 1.0f;
    }

    void process(juce::AudioBuffer<float>& buffer, bool isEnabled)
    {
        targetGain = isEnabled ? 2.0f : 1.0f; // +6 dB when enabled

        if (!isEnabled && std::abs(gainSmoothing - 1.0f) < 0.001f)
            return;

        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        // Work on temporary buffer for parallel compression
        parallelBuffer.setSize(numChannels, numSamples, false, false, true);
        for (int ch = 0; ch < numChannels; ++ch)
            parallelBuffer.copyFrom(ch, 0, buffer, ch, 0, numSamples);

        // Process parallel compressed path
        juce::dsp::AudioBlock<float> block(parallelBuffer);
        juce::dsp::ProcessContextReplacing<float> context(block);
        compressor.process(context);

        // Mix parallel path & apply saturation push
        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* dryData = buffer.getWritePointer(ch);
            const auto* wetData = parallelBuffer.getReadPointer(ch);

            for (int i = 0; i < numSamples; ++i)
            {
                // Smooth gain transition to avoid pops
                gainSmoothing += 0.002f * (targetGain - gainSmoothing);

                if (gainSmoothing > 1.001f)
                {
                    // Parallel blend: 75% dry + 50% squashed wet
                    float mixed = dryData[i] * 0.8f + wetData[i] * 0.6f;

                    // +6dB Saturation Push with soft-knee ceiling
                    float boosted = mixed * gainSmoothing;
                    dryData[i] = std::tanh(boosted * 0.9f) * 1.05f;
                }
            }
        }
    }

private:
    double currentSampleRate = 44100.0;
    juce::dsp::Compressor<float> compressor;
    juce::AudioBuffer<float> parallelBuffer;
    float gainSmoothing = 1.0f;
    float targetGain = 1.0f;
};

} // namespace AllyDSP
