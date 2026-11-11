#pragma once
#include <juce_core/juce_core.h>
#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace AllyDSP
{

enum class StadsdeelMode
{
    Haven = 0,    // 1350: Warm tape/tube saturation, analog & raw
    Centrum = 1,  // 1315: Aggressive digital hard-clipping / waveshaping for trap & drill
    Poort = 2,    // 1363: Ultra-clean sub-boost with subtle harmonic excitation
    Buiten = 3    // 1330: Foldback distortion with mid-range resonance
};

/**
 * Stadsdeel Saturatie (4 Karakteristieke DSP-modi)
 * 
 * DSP Wiskunde:
 * 1. HAVEN (1350) - Tape/Tube Asymmetric Saturation:
 *    x_biased = x + 0.18 * x^2 - 0.05 * x^3
 *    y = tanh((1.0 + 3.0 * drive) * x_biased)
 *    Followed by DC blocker: y_clean = y[n] - y[n-1] + 0.995 * y_clean[n-1]
 * 
 * 2. CENTRUM (1315) - Trap/Drill Hard-Clipped Waveshaper:
 *    u = x * (1.0 + 8.0 * drive)
 *    if (|u| >= 1.0) y = sgn(u)
 *    else y = 1.5 * u - 0.5 * u^3
 * 
 * 3. POORT (1363) - Clean Sub Exciter:
 *    Sub fundamental retained pure; subtle harmonic excitation in mid-harmonics:
 *    y = x + 0.15 * drive * (2.0 * x^2 - 1.0)
 * 
 * 4. BUITEN (1330) - Foldback Distortion:
 *    y = sin(x * (1.0 + 5.0 * drive) * (PI / 2))
 */
class StadsdeelSaturation
{
public:
    StadsdeelSaturation() = default;

    void prepare(double sampleRate)
    {
        currentSampleRate = sampleRate;
        dcBlockerPrevIn = 0.0f;
        dcBlockerPrevOut = 0.0f;
        updateFilters();
    }

    void reset()
    {
        dcBlockerPrevIn = 0.0f;
        dcBlockerPrevOut = 0.0f;
        buitenFilter.reset();
    }

    void updateFilters()
    {
        if (currentSampleRate <= 0.0)
            return;

        // Buiten mid-resonance around 550 Hz with Q=2.0
        buitenFilter.coefficients = juce::dsp::IIR::Coefficients<float>::makePeakFilter(
            currentSampleRate, 550.0f, 2.0f, 2.2f);
    }

    float processSample(float input, float drive, StadsdeelMode mode)
    {
        if (drive <= 0.001f)
            return input;

        switch (mode)
        {
            case StadsdeelMode::Haven:
                return processHaven(input, drive);

            case StadsdeelMode::Centrum:
                return processCentrum(input, drive);

            case StadsdeelMode::Poort:
                return processPoort(input, drive);

            case StadsdeelMode::Buiten:
                return processBuiten(input, drive);

            default:
                return input;
        }
    }

private:
    float processHaven(float x, float drive)
    {
        // Haven (1350): Warm tape & tube saturation
        // Asymmetric transfer function generating even harmonics
        float driveGain = 1.0f + drive * 3.5f;
        float xBiased = x + 0.18f * (x * x) - 0.05f * (x * x * x);
        float saturated = std::tanh(driveGain * xBiased);

        // DC Blocker filter: y[n] = x[n] - x[n-1] + R * y[n-1]
        const float R = 0.995f;
        float dcCleaned = saturated - dcBlockerPrevIn + R * dcBlockerPrevOut;
        dcBlockerPrevIn = saturated;
        dcBlockerPrevOut = dcCleaned;

        // Warm level compensation
        float makeup = 1.0f / std::sqrt(driveGain);
        return dcCleaned * makeup;
    }

    float processCentrum(float x, float drive)
    {
        // Centrum (1315): Trap & Drill aggressive digital hard-clipping and waveshaping
        // High gain drive into cubic shaper + hard threshold limit
        float driveGain = 1.0f + drive * 7.5f;
        float u = x * driveGain;

        float shaped;
        if (u > 1.0f)
            shaped = 1.0f;
        else if (u < -1.0f)
            shaped = -1.0f;
        else
            shaped = 1.5f * u - 0.5f * (u * u * u);

        // Normalize output level for balanced mix
        float makeup = 1.0f / (1.0f + drive * 1.5f);
        return shaped * makeup;
    }

    float processPoort(float x, float drive)
    {
        // Poort (1363): Ultra-cleane, moderne sub-boost met subtiele harmonische opwinding
        // Sub stays 100% clean and solid; subtle Chebyshev 2nd harmonic added for presence
        float cleanSub = x * (1.0f + drive * 0.4f);
        float chebyshev2nd = 2.0f * (x * x);
        
        // Blend in minimal excitement
        float exciter = chebyshev2nd * (drive * 0.15f);
        float combined = cleanSub + exciter;

        // Soft limit peak
        return std::tanh(combined * 1.1f) * 0.95f;
    }

    float processBuiten(float x, float drive)
    {
        // Buiten (1330): Foldback-distortie met lichte mid-range resonantie
        // Mid-boost into sine wavefolder
        float boosted = buitenFilter.processSample(x);
        float foldDrive = 1.0f + drive * 4.5f;
        float w = boosted * foldDrive;

        const float halfPi = juce::MathConstants<float>::halfPi;
        float folded = std::sin(w * halfPi);

        // Level compensation
        float makeup = 1.0f / std::sqrt(1.0f + drive * 2.0f);
        return folded * makeup;
    }

    double currentSampleRate = 44100.0;
    float dcBlockerPrevIn = 0.0f;
    float dcBlockerPrevOut = 0.0f;
    juce::dsp::IIR::Filter<float> buitenFilter;
};

} // namespace AllyDSP
