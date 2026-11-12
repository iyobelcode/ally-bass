#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
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
        safeSpec.numChannels = juce::jlimit<juce::uint32>(2u, static_cast<juce::uint32>(maxChannels), spec.numChannels);

        compressor.prepare(safeSpec);
        compressor.setThreshold(-18.0f);
        compressor.setRatio(6.0f);
        compressor.setAttack(5.0f);   // Fast attack for 808 density
        compressor.setRelease(80.0f); // Fast recovery

        // Pre-allocate parallel buffer during prepareToPlay to eliminate all heap allocations in processBlock
        parallelBuffer.setSize(static_cast<int>(safeSpec.numChannels),
                               static_cast<int>(safeSpec.maximumBlockSize),
                               false, true, false);

        const double sr = currentSampleRate;
        // 8 ms SR-invariant transition time + 5 Hz SR-invariant post-overdrive DC blocker
        mixSmoothCoeff = 1.0f - static_cast<float>(std::exp(-1.0 / (0.008 * sr)));
        dcBlockCoeff = static_cast<float>(std::exp(-juce::MathConstants<double>::twoPi * 5.0 / sr));

        reset();
    }

    void reset()
    {
        compressor.reset();
        resetDcBlockers();
        wetMix = 0.0f;
        isBypassedState = true;
    }

    void process(juce::AudioBuffer<float>& buffer, bool isEnabled)
    {
        const float targetMix = isEnabled ? 1.0f : 0.0f;

        if (!isEnabled && wetMix <= 1e-5f)
        {
            wetMix = 0.0f;
            if (!isBypassedState)
            {
                compressor.reset();
                resetDcBlockers();
                isBypassedState = true;
            }
            return;
        }

        if (isEnabled && isBypassedState)
        {
            compressor.reset();
            resetDcBlockers();
            isBypassedState = false;
        }

        const int numChannels = juce::jmin(buffer.getNumChannels(),
                                           juce::jmin(parallelBuffer.getNumChannels(), maxChannels));
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

            std::array<float*, maxChannels> dryPtrs {};
            std::array<const float*, maxChannels> wetPtrs {};
            for (int ch = 0; ch < numChannels; ++ch)
            {
                dryPtrs[static_cast<size_t>(ch)] = buffer.getWritePointer(ch) + offset;
                wetPtrs[static_cast<size_t>(ch)] = parallelBuffer.getReadPointer(ch);
            }

            // Advance wetMix once per sample frame so stereo L/R channels stay identically balanced
            for (int i = 0; i < chunkSamples; ++i)
            {
                wetMix += mixSmoothCoeff * (targetMix - wetMix);
                if (!isEnabled && wetMix < 1e-5f)
                    wetMix = 0.0f;

                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const size_t idx = static_cast<size_t>(ch);
                    const float dry = dryPtrs[idx][i];
                    const float compressed = wetPtrs[idx][i];
                    const float mixed = dry * 0.8f + compressed * 0.6f;
                    const float wetSaturated = std::tanh(mixed * 1.8f) * 1.05f;
                    const float wetClean = dcBlockers[idx].process(wetSaturated, dcBlockCoeff);

                    dryPtrs[idx][i] = dry + wetMix * (wetClean - dry);
                }
            }
        }
    }

private:
    static constexpr int maxChannels = 8;

    struct DcBlockerState
    {
        float prevIn = 0.0f;
        float prevOut = 0.0f;

        float process(float x, float r) noexcept
        {
            const float y = x - prevIn + r * prevOut;
            prevIn = x;
            prevOut = y;
            return y;
        }

        void reset() noexcept
        {
            prevIn = 0.0f;
            prevOut = 0.0f;
        }
    };

    void resetDcBlockers() noexcept
    {
        for (auto& dc : dcBlockers)
            dc.reset();
    }

    double currentSampleRate = 44100.0;
    juce::dsp::Compressor<float> compressor;
    juce::AudioBuffer<float> parallelBuffer;
    std::array<DcBlockerState, maxChannels> dcBlockers {};

    float wetMix = 0.0f;
    float mixSmoothCoeff = 0.002f;
    float dcBlockCoeff = 0.9993f;
    bool isBypassedState = true;
};

} // namespace AllyDSP
