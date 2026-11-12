#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace AllyGUI
{

/**
 * Engineered Depth Design System — Tokens, Typography, Surfaces & State Styling
 *
 * Visual source of truth: ALLY1300_APPROVED_MASTER_FULL.png & Design Bible v1.0.
 * Pure vector/JUCE rendering: scales cleanly across window sizes and HiDPI scales
 * with zero PNG dependencies or continuous animation timers.
 */
struct EngineeredDepthTheme
{
    enum class AccentTheme
    {
        Cyan,
        Red
    };

    struct ControlState
    {
        bool hovered = false;
        bool dragging = false;
        bool active = true;
        bool focused = false;
        bool enabled = true;
    };

    struct AccentPalette
    {
        juce::Colour primary;
        juce::Colour coreHighlight;
        juce::Colour glow;
        juce::Colour dim;
        juce::Colour focus;
        juce::Colour trackInactive;
        juce::Colour bracket;
        juce::Colour textValue;
    };

    // Core Engineered Depth Color Tokens
    struct Colors
    {
        static inline const juce::Colour bg            { 0xff071017 };
        static inline const juce::Colour surface1      { 0xff0b202b };
        static inline const juce::Colour surface2      { 0xff102c39 };
        static inline const juce::Colour surfaceRaised { 0xff132f3d };
        static inline const juce::Colour stroke        { 0xff25627a };
        static inline const juce::Colour strokeSubtle  { 0xff18465c };
        static inline const juce::Colour cyan          { 0xff00e4f1 };
        static inline const juce::Colour cyanBright    { 0xffccfaff };
        static inline const juce::Colour cyanDim       { 0xff63aebf };
        static inline const juce::Colour cyanFocus     { 0xff78d9e6 };
        static inline const juce::Colour red           { 0xffff4653 };
        static inline const juce::Colour redBright     { 0xffffd4d8 };
        static inline const juce::Colour redDim        { 0xffaa3946 };
        static inline const juce::Colour textPrimary   { 0xffedf7fa };
        static inline const juce::Colour textSecondary { 0xffadc4cf };
        static inline const juce::Colour textMuted     { 0xff678996 };
        static inline const juce::Colour disabled      { 0xff526671 };
        static inline const juce::Colour warning       { 0xffffc46b };
        static inline const juce::Colour black         { 0xff030a0e };

        // Layered dial metal & well tokens calibrated to ALLY1300_APPROVED_MASTER_FULL.png
        static inline const juce::Colour dialWell      { 0xff02090d };
        static inline const juce::Colour trackDark     { 0xff122c3a };
        static inline const juce::Colour metalRimLight { 0xff8db3c4 };
        static inline const juce::Colour metalRimMid   { 0xff2c5264 };
        static inline const juce::Colour metalRimDark  { 0xff051118 };
        static inline const juce::Colour dialFaceTop   { 0xff243f4e };
        static inline const juce::Colour dialFaceMid   { 0xff0d202b };
        static inline const juce::Colour dialFaceBot   { 0xff05121a };
        static inline const juce::Colour capFillTop    { 0xff0a1c26 };
        static inline const juce::Colour capFillBot    { 0xff051017 };
        static inline const juce::Colour capStroke     { 0xff22495b };
        static inline const juce::Colour tickMajor     { 0xffdcebfe };
        static inline const juce::Colour tickMinorDim  { 0xff275a70 };
    };

    struct Metrics
    {
        static constexpr float spaceXS = 4.0f;
        static constexpr float spaceS  = 8.0f;
        static constexpr float spaceM  = 12.0f;
        static constexpr float spaceL  = 16.0f;
        static constexpr float spaceXL = 24.0f;

        static constexpr float radiusPanel  = 16.0f;
        static constexpr float radiusButton = 10.0f;
        static constexpr float radiusSmall  = 6.0f;

        static constexpr float hairlineWidth  = 1.0f;
        static constexpr float panelStroke    = 1.25f;
        static constexpr float maxCyanGlowAlpha = 0.24f;
        static constexpr float maxRedGlowAlpha  = 0.20f;
    };

    // Accent resolution across normal / hover / active / dragging / focus / disabled
    static AccentPalette resolveAccent(AccentTheme theme, const ControlState& state);

    // Typography (resolves licensed system/installed UI & monospace families cleanly)
    static const juce::String& getUiFontFamily();
    static const juce::String& getDataFontFamily();
    static juce::Font makeUiFont(float pointSize, bool bold = false);
    static juce::Font makeDataFont(float pointSize, bool bold = false);

    // Reusable vector surface, contour, readout, pill & focus primitives
    static juce::Path buildChamferedRectPath(juce::Rectangle<float> bounds,
                                             float topChamfer,
                                             float bottomChamfer);

    static void drawSurfacePanel(juce::Graphics& g,
                                 juce::Rectangle<float> bounds,
                                 float cornerRadius = Metrics::radiusPanel,
                                 bool raised = false,
                                 bool highlighted = false);

    static void drawContourLine(juce::Graphics& g,
                                juce::Point<float> start,
                                juce::Point<float> end,
                                float thickness = Metrics::hairlineWidth,
                                float alpha = 0.75f);

    static void drawAccentPill(juce::Graphics& g,
                               juce::Rectangle<float> bounds,
                               juce::Colour accentColour,
                               float glowAlpha,
                               bool enabled);

    static void drawBracketReadout(juce::Graphics& g,
                                   juce::Rectangle<float> bounds,
                                   const juce::String& text,
                                   const juce::Font& font,
                                   juce::Colour bracketColour,
                                   juce::Colour textColour,
                                   float strokeThickness,
                                   bool highlighted);

    static void drawFocusOutline(juce::Graphics& g,
                                 juce::Rectangle<float> bounds,
                                 float cornerRadius,
                                 juce::Colour focusColour);

    static void drawTrackedText(juce::Graphics& g,
                                const juce::String& text,
                                juce::Rectangle<float> bounds,
                                const juce::Font& font,
                                juce::Colour colour,
                                float extraTrackingEm = 0.08f,
                                juce::Justification justification = juce::Justification::centred);
};

} // namespace AllyGUI
