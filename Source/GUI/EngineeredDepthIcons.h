#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AllyGUI
{

/**
 * Pure JUCE vector icon renderer derived from Phase 5 SVGs and corrected
 * against ALLY1300_APPROVED_MASTER_FULL.png.
 *
 * Keeps icons crisp at any scale factor without runtime SVG parsing or PNG textures.
 */
struct EngineeredDepthIcons
{
    enum class IconId
    {
        None = 0,
        DiepteWater,
        PolderPunch,
        HavenDirt,
        WaterwijkGlide,
        OutputBars,
        StadsdeelDsp,
        ChevronDown
    };

    /** Returns normalized vector geometry in viewBox space and its reference bounds. */
    static juce::Path buildStrokePath(IconId icon, juce::Rectangle<float>& outViewBox);

    /** Draws the vector icon centered inside targetBounds, preserving aspect ratio. */
    static void drawIcon(juce::Graphics& g,
                         IconId icon,
                         juce::Rectangle<float> targetBounds,
                         juce::Colour primaryColour,
                         juce::Colour secondaryColour,
                         float strokeThickness,
                         float glowAlpha = 0.0f);
};

} // namespace AllyGUI
