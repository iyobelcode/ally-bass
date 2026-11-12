#include "EngineeredDepthIcons.h"

namespace AllyGUI
{

namespace
{
void appendWaveRow(juce::Path& p, float y)
{
    // Smooth two-crest wave derived from diepte_water.svg and tuned to approved master proportions
    p.startNewSubPath(2.0f, y);
    p.quadraticTo(5.0f, y - 3.4f, 8.0f, y);
    p.quadraticTo(11.0f, y + 3.4f, 14.0f, y);
    p.quadraticTo(17.0f, y - 3.4f, 20.0f, y);
    p.quadraticTo(23.0f, y + 3.4f, 26.0f, y);
}

void appendWaveRow24(juce::Path& p, float y)
{
    // 24x24 harbor wave contour matching haven_dirt_haventje.svg
    p.startNewSubPath(2.1f, y);
    p.quadraticTo(4.3f, y - 1.05f, 6.5f, y);
    p.quadraticTo(8.7f, y + 1.05f, 11.0f, y);
    p.quadraticTo(13.2f, y - 1.05f, 15.5f, y);
    p.quadraticTo(17.8f, y + 1.05f, 20.0f, y);
    p.quadraticTo(21.1f, y - 0.55f, 22.0f, y - 0.25f);
}
} // namespace

juce::Path EngineeredDepthIcons::buildStrokePath(IconId icon, juce::Rectangle<float>& outViewBox)
{
    juce::Path path;

    switch (icon)
    {
        case IconId::DiepteWater:
            outViewBox = { 0.0f, 0.0f, 28.0f, 26.0f };
            appendWaveRow(path, 7.0f);
            appendWaveRow(path, 13.0f);
            appendWaveRow(path, 19.0f);
            break;

        case IconId::PolderPunch:
            outViewBox = { 0.0f, 0.0f, 24.0f, 24.0f };
            path.startNewSubPath(2.0f, 10.0f);
            path.lineTo(15.0f, 3.0f);
            path.lineTo(22.0f, 5.0f);
            path.lineTo(9.0f, 12.0f);
            path.startNewSubPath(2.0f, 15.0f);
            path.lineTo(15.0f, 8.0f);
            path.startNewSubPath(8.0f, 19.0f);
            path.lineTo(20.0f, 8.0f);
            path.startNewSubPath(14.0f, 21.0f);
            path.lineTo(22.0f, 12.0f);
            break;

        case IconId::HavenDirt:
            outViewBox = { 0.0f, 0.0f, 24.0f, 24.0f };
            appendWaveRow24(path, 19.0f);
            appendWaveRow24(path, 21.3f);
            break;

        case IconId::WaterwijkGlide:
            outViewBox = { 0.0f, 0.0f, 28.0f, 24.0f };
            appendWaveRow(path, 16.0f);
            path.startNewSubPath(3.0f, 12.0f);
            path.lineTo(5.0f, 12.0f);
            path.lineTo(5.0f, 10.0f);
            path.lineTo(8.0f, 10.0f);
            path.lineTo(8.0f, 12.0f);
            path.lineTo(10.0f, 12.0f);
            path.lineTo(10.0f, 5.0f);
            path.lineTo(14.0f, 5.0f);
            path.lineTo(14.0f, 12.0f);
            path.lineTo(17.0f, 12.0f);
            path.lineTo(17.0f, 9.0f);
            path.lineTo(19.0f, 9.0f);
            path.lineTo(19.0f, 12.0f);
            path.lineTo(22.0f, 12.0f);
            break;

        case IconId::OutputBars:
            outViewBox = { 0.0f, 0.0f, 28.0f, 24.0f };
            path.addRoundedRectangle(3.0f, 15.0f, 3.2f, 4.0f, 0.6f);
            path.addRoundedRectangle(9.0f, 12.0f, 3.2f, 7.0f, 0.6f);
            path.addRoundedRectangle(15.0f, 8.0f, 3.2f, 11.0f, 0.6f);
            path.addRoundedRectangle(21.0f, 4.0f, 3.2f, 15.0f, 0.6f);
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
            g.setColour(primaryColour.withAlpha(glowAlpha * 0.6f));
            g.strokePath(path, juce::PathStrokeType(strokeThickness * 1.8f), viewTransform);
        }
        g.setColour(primaryColour);
        g.fillPath(path, viewTransform);
        return;
    }

    if (icon == IconId::HavenDirt)
    {
        juce::Path upperBoatAndPier;
        upperBoatAndPier.startNewSubPath(6.8f, 3.8f);
        upperBoatAndPier.lineTo(6.8f, 13.2f);
        upperBoatAndPier.startNewSubPath(6.1f, 5.2f);
        upperBoatAndPier.lineTo(3.4f, 12.5f);
        upperBoatAndPier.lineTo(6.1f, 12.5f);
        upperBoatAndPier.closeSubPath();
        upperBoatAndPier.startNewSubPath(7.5f, 6.4f);
        upperBoatAndPier.lineTo(10.5f, 12.5f);
        upperBoatAndPier.lineTo(7.5f, 12.5f);
        upperBoatAndPier.closeSubPath();
        upperBoatAndPier.startNewSubPath(2.3f, 14.1f);
        upperBoatAndPier.lineTo(11.7f, 14.1f);
        upperBoatAndPier.lineTo(9.7f, 17.1f);
        upperBoatAndPier.lineTo(4.8f, 17.1f);
        upperBoatAndPier.closeSubPath();
        upperBoatAndPier.startNewSubPath(13.4f, 14.2f);
        upperBoatAndPier.lineTo(22.0f, 14.2f);
        upperBoatAndPier.startNewSubPath(15.2f, 10.5f);
        upperBoatAndPier.lineTo(15.2f, 17.0f);
        upperBoatAndPier.startNewSubPath(21.0f, 11.3f);
        upperBoatAndPier.lineTo(21.0f, 17.0f);
        upperBoatAndPier.startNewSubPath(14.9f, 10.5f);
        upperBoatAndPier.lineTo(21.4f, 10.5f);

        const auto thinStroke = juce::PathStrokeType(strokeThickness * 0.90f,
                                                     juce::PathStrokeType::curved,
                                                     juce::PathStrokeType::rounded);
        if (glowAlpha > 0.01f)
        {
            g.setColour(primaryColour.withAlpha(glowAlpha * 0.5f));
            g.strokePath(path, juce::PathStrokeType(strokeThickness * 2.1f), viewTransform);
        }
        g.setColour(secondaryColour);
        g.strokePath(upperBoatAndPier, thinStroke, viewTransform);
        g.setColour(primaryColour);
        g.strokePath(path, thinStroke, viewTransform);
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
