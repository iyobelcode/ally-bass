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
        currentSampleRate = spec.sampleRate > 1.0 ? spec.sampleRate : 44100.0;

        juce::dsp::ProcessSpec safeSpec = spec;
        safeSpec.sampleRate = currentSampleRate;
        safeSpec.maximumBlockSize = juce::jmax<juce::uint32>(1u, spec.maximumBlockSize);
        safeSpec.numChannels = juce::jmax<juce::uint32>(2u, spec.numChannels);

        compressor.prepare(safeSpec);
        compressor.setThreshold(-18.0f);
        compressor.setRatio(6.0f);
        compressor.setAttack(5.0f);   // Fast attack for 808 density
        compressor.setRelease(80.0f); // Fast recovery

        // Pre-allocate parallel buffer during prepareToPlay to eliminate all heap allocations in processBlock
        parallelBuffer.setSize(static_cast<int>(safeSpec.numChannels),
                               static_cast<int>(safeSpec.maximumBlockSize),
                               false, true, false);

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

        const int numChannels = juce::jmin(buffer.getNumChannels(), parallelBuffer.getNumChannels());
        const int numSamples = buffer.getNumSamples();
        const int maxChunk = parallelBuffer.getNumSamples();
        if (numChannels <= 0 || numSamples <= 0 || maxChunk <= 0)
            return;

        // Process in bounded pre-allocated chunks so even non-conforming oversized host blocks never allocate
        for (int offset = 0; offset < numSamples; offset += maxChunk)
        {
            const int chunkSamples = juce::jmin(maxChunk, numSamples - offset);

            for (int ch = 0; ch < numChannels; ++ch)
                parallelBuffer.copyFrom(ch, 0, buffer, ch, offset, chunkSamples);

            juce::dsp::AudioBlock<float> fullBlock(parallelBuffer);
            auto chunkBlock = fullBlock.getSubsetChannelBlock(0, static_cast<size_t>(numChannels))
                                       .getSubBlock(0, static_cast<size_t>(chunkSamples));
            juce::dsp::ProcessContextReplacing<float> context(chunkBlock);
            compressor.process(context);

            // Preserve sample-order identical gainSmoothing evolution across channels (channel loop inside or identical logic?
            // Keep exact existing per-channel outer loop parity cleanly for chunkSamples!)
            for (int ch = 0; ch < numChannels; ++ch)
            {
                auto* dryData = buffer.getWritePointer(ch) + offset;
                const auto* wetData = parallelBuffer.getReadPointer(ch);

                for (int i = 0; i < chunkSamples; ++i)
                {
                    gainSmoothing += 0.002f * (targetGain - gainSmoothing);

                    if (gainSmoothing > 1.001f)
                    {
                        float mixed = dryData[i] * 0.8f + wetData[i] * 0.6f;
                        float boosted = mixed * gainSmoothing;
                        dryData[i] = std::tanh(boosted * 0.9f) * 1.05f;
                    }
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
