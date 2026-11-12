#include "EngineeredDepthIcons.h"

namespace AllyGUI
{

namespace
{
void appendWaveRow(juce::Path& p, float xStart, float xEnd, float y, float controlAmp)
{
    const float span = (xEnd - xStart) * 0.25f;
    p.startNewSubPath(xStart, y);
    p.quadraticTo(xStart + span * 0.5f, y - controlAmp, xStart + span, y);
    p.quadraticTo(xStart + span * 1.5f, y + controlAmp, xStart + span * 2.0f, y);
    p.quadraticTo(xStart + span * 2.5f, y - controlAmp, xStart + span * 3.0f, y);
    p.quadraticTo(xStart + span * 3.5f, y + controlAmp, xEnd, y);
}
} // namespace

juce::Path EngineeredDepthIcons::buildStrokePath(IconId icon, juce::Rectangle<float>& outViewBox)
{
    juce::Path path;

    switch (icon)
    {
        case IconId::DiepteWater:
            // Tight horizontal waves tuned to occupy ~36-40% of inner cap diameter in DepthRingLookAndFeel
            outViewBox = { 0.0f, 0.0f, 28.0f, 16.5f };
            appendWaveRow(path, 1.5f, 26.5f, 3.8f, 2.75f);
            appendWaveRow(path, 1.5f, 26.5f, 8.25f, 2.75f);
            appendWaveRow(path, 1.5f, 26.5f, 12.7f, 2.75f);
            break;

        case IconId::PolderPunch:
            // Perspective parallel polder/akkerlijnen receding to horizontal ridge (tight viewBox matching cap span)
            outViewBox = { 0.0f, 0.0f, 26.0f, 12.0f };
            path.startNewSubPath(9.6f, 2.2f);
            path.lineTo(24.0f, 2.2f);
            for (int i = 0; i < 5; ++i)
            {
                const float topX = 9.6f + static_cast<float>(i) * 3.6f;
                const float botX = 2.0f + static_cast<float>(i) * 3.6f;
                path.startNewSubPath(botX, 9.8f);
                path.lineTo(topX, 2.2f);
            }
            break;

        case IconId::HavenDirt:
            outViewBox = { 0.0f, 0.0f, 24.0f, 22.0f };
            appendWaveRow(path, 2.0f, 22.2f, 18.0f, 1.65f);
            appendWaveRow(path, 2.0f, 22.2f, 20.4f, 1.65f);
            break;

        case IconId::WaterwijkGlide:
            outViewBox = { 0.0f, 0.0f, 28.0f, 18.0f };
            appendWaveRow(path, 2.0f, 26.0f, 14.4f, 1.85f);
            path.startNewSubPath(4.2f, 11.4f);
            path.lineTo(6.2f, 11.4f);
            path.lineTo(6.2f, 9.2f);
            path.lineTo(8.8f, 9.2f);
            path.lineTo(8.8f, 11.0f);
            path.lineTo(10.8f, 11.0f);
            path.lineTo(10.8f, 7.6f);
            path.lineTo(12.6f, 7.6f);
            path.lineTo(12.6f, 3.6f);
            path.lineTo(15.4f, 3.6f);
            path.lineTo(15.4f, 10.6f);
            path.lineTo(17.5f, 10.6f);
            path.lineTo(17.5f, 7.6f);
            path.lineTo(20.4f, 7.6f);
            path.lineTo(20.4f, 11.2f);
            path.lineTo(23.8f, 11.2f);
            break;

        case IconId::OutputBars:
            outViewBox = { 0.0f, 0.0f, 26.0f, 18.0f };
            path.addRoundedRectangle(2.6f,  11.8f, 2.8f, 4.2f,  0.55f);
            path.addRoundedRectangle(6.9f,  9.6f,  2.8f, 6.4f,  0.55f);
            path.addRoundedRectangle(11.2f, 7.4f,  2.8f, 8.6f,  0.55f);
            path.addRoundedRectangle(15.5f, 5.0f,  2.8f, 11.0f, 0.55f);
            path.addRoundedRectangle(19.8f, 2.4f,  2.8f, 13.6f, 0.55f);
            break;

        case IconId::None:
        default:
            outViewBox = { 0.0f, 0.0f, 24.0f, 24.0f };
            break;
    }

    return path;
}

void EngineeredDepthIcons::drawIcon(juce::Graphics& g,
                                    IconId icon,
                                    juce::Rectangle<float> targetBounds,
                                    juce::Colour primaryColour,
                                    juce::Colour secondaryColour,
                                    float strokeThickness,
                                    float glowAlpha)
{
    if (icon == IconId::None || targetBounds.isEmpty())
        return;

    juce::Rectangle<float> viewBox;
    auto path = buildStrokePath(icon, viewBox);

    // Uniform scale keeps stroke geometry and multi-layer icons aligned
    const float scale = juce::jmin(targetBounds.getWidth() / viewBox.getWidth(),
                                   targetBounds.getHeight() / viewBox.getHeight());
    const float offsetX = targetBounds.getCentreX() - viewBox.getCentreX() * scale;
    const float offsetY = targetBounds.getCentreY() - viewBox.getCentreY() * scale;
    const auto viewTransform = juce::AffineTransform::scale(scale).translated(offsetX, offsetY);

    const auto stroke = juce::PathStrokeType(strokeThickness,
                                             juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded);

    if (icon == IconId::OutputBars)
    {
        if (glowAlpha > 0.01f)
        {
            g.setColour(primaryColour.withAlpha(glowAlpha * 0.45f));
            g.strokePath(path, juce::PathStrokeType(strokeThickness * 1.6f), viewTransform);
        }

        const auto transformedBounds = path.getBounds().transformedBy(viewTransform);
        juce::ColourGradient barGrad(secondaryColour,
                                     transformedBounds.getCentreX(), transformedBounds.getY(),
                                     primaryColour.darker(0.18f),
                                     transformedBounds.getCentreX(), transformedBounds.getBottom(),
                                     false);
        g.setGradientFill(barGrad);
        g.fillPath(path, viewTransform);
        return;
    }

    if (icon == IconId::HavenDirt)
    {
        // Shared single thin mast (no double vertical inner sail strokes blurring together)
        juce::Path thinMast;
        thinMast.startNewSubPath(6.8f, 2.2f);
        thinMast.lineTo(6.8f, 12.6f);

        juce::Path upperBoatAndPier;
        // Left jib outer + bottom + right mainsail outer + bottom (meeting cleanly at mast)
        upperBoatAndPier.startNewSubPath(6.8f, 3.6f);
        upperBoatAndPier.lineTo(3.3f, 11.4f);
        upperBoatAndPier.lineTo(6.8f, 11.4f);
        upperBoatAndPier.startNewSubPath(6.8f, 4.8f);
        upperBoatAndPier.lineTo(10.5f, 11.4f);
        upperBoatAndPier.lineTo(6.8f, 11.4f);

        // Boat hull
        upperBoatAndPier.startNewSubPath(2.2f, 13.0f);
        upperBoatAndPier.lineTo(11.6f, 13.0f);
        upperBoatAndPier.lineTo(9.8f, 15.8f);
        upperBoatAndPier.lineTo(4.1f, 15.8f);
        upperBoatAndPier.closeSubPath();

        // Small harbor jetty/pier (no crane)
        upperBoatAndPier.startNewSubPath(13.8f, 9.2f);
        upperBoatAndPier.lineTo(21.5f, 9.2f);
        upperBoatAndPier.startNewSubPath(15.0f, 9.2f);
        upperBoatAndPier.lineTo(15.0f, 15.8f);
        upperBoatAndPier.startNewSubPath(20.3f, 9.2f);
        upperBoatAndPier.lineTo(20.3f, 15.8f);
        upperBoatAndPier.startNewSubPath(13.1f, 12.9f);
        upperBoatAndPier.lineTo(21.7f, 12.9f);

        const auto sceneStroke = juce::PathStrokeType(strokeThickness * 0.92f,
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded);
        const auto mastStroke  = juce::PathStrokeType(juce::jmax(0.85f, strokeThickness * 0.72f),
                                                      juce::PathStrokeType::curved,
                                                      juce::PathStrokeType::rounded);
        if (glowAlpha > 0.01f)
        {
            g.setColour(primaryColour.withAlpha(glowAlpha * 0.48f));
            g.strokePath(path, juce::PathStrokeType(strokeThickness * 2.0f), viewTransform);
        }
        g.setColour(secondaryColour);
        g.strokePath(upperBoatAndPier, sceneStroke, viewTransform);
        g.strokePath(thinMast, mastStroke, viewTransform);
        g.setColour(primaryColour);
        g.strokePath(path, sceneStroke, viewTransform);
        return;
    }

    if (glowAlpha > 0.01f)
    {
        g.setColour(primaryColour.withAlpha(glowAlpha * 0.55f));
        g.strokePath(path,
                     juce::PathStrokeType(strokeThickness * 2.2f,
                                          juce::PathStrokeType::curved,
                                          juce::PathStrokeType::rounded),
                     viewTransform);
    }

    g.setColour(primaryColour);
    g.strokePath(path, stroke, viewTransform);
}

} // namespace AllyGUI
