#include "NapWaveVisualizer.h"
#include "../PluginProcessor.h"
#include "BrutalistLookAndFeel.h"

namespace AllyGUI
{

NapWaveVisualizer::NapWaveVisualizer(AllyBassAudioProcessor& p)
    : processor(p)
{
    scopeData.fill(0.0f);
    displayPoints.fill(0.0f);
    startTimerHz(60); // 60 FPS smooth rendering
}

NapWaveVisualizer::~NapWaveVisualizer()
{
    stopTimer();
}

void NapWaveVisualizer::timerCallback()
{
    // Retrieve real-time audio samples
    processor.getVisualizerData(scopeData.data(), numScopePoints);

    float currentRms = processor.getCurrentSubLevel();
    smoothRms += 0.2f * (currentRms - smoothRms);

    // Advance ambient Weerwater fluid ripples
    waterPhase += 0.04f;
    if (waterPhase >= juce::MathConstants<float>::twoPi)
        waterPhase -= juce::MathConstants<float>::twoPi;

    // Smooth interpolated points to eliminate jitter
    for (size_t i = 0; i < numScopePoints; ++i)
    {
        float targetVal = scopeData[i];
        displayPoints[i] += 0.35f * (targetVal - displayPoints[i]);
    }

    repaint();
}

void NapWaveVisualizer::resized()
{
}

void NapWaveVisualizer::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // 1. Brutalist Concrete Display Well
    g.setColour(juce::Colour(0xff0e1012));
    g.fillRoundedRectangle(bounds, 4.0f);

    // Subtle Outer Bevel / Border
    g.setColour(BrutalistLookAndFeel::concreteBorder);
    g.drawRoundedRectangle(bounds, 4.0f, 1.5f);

    // 2. Architectural Blueprint Grid
    drawArchitecturalGrid(g, bounds);

    // 3. -4.5m NAP Datum Water Line
    // Placed at 55% height
    float napY = bounds.getY() + bounds.getHeight() * 0.55f;
    drawNapWaterLine(g, bounds, napY);

    // 4. Real-time Weerwater Vector Waveform & Underwater Dive Glow
    bool isOverdrive = processor.isOverdriveActive();
    drawWaveform(g, bounds, napY, isOverdrive);

    // 5. Technical Telemetry & Almere Coordinates
    drawTechnicalTelemetry(g, bounds);
}

void NapWaveVisualizer::drawArchitecturalGrid(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setColour(juce::Colour(0xff181c22));

    // Vertical grid lines
    int numVertLines = 16;
    float dx = bounds.getWidth() / static_cast<float>(numVertLines);
    for (int i = 1; i < numVertLines; ++i)
    {
        float x = bounds.getX() + static_cast<float>(i) * dx;
        g.drawVerticalLine(static_cast<int>(x), bounds.getY(), bounds.getBottom());
    }

    // Horizontal grid lines
    int numHorizLines = 8;
    float dy = bounds.getHeight() / static_cast<float>(numHorizLines);
    for (int j = 1; j < numHorizLines; ++j)
    {
        float y = bounds.getY() + static_cast<float>(j) * dy;
        g.drawHorizontalLine(static_cast<int>(y), bounds.getX(), bounds.getRight());
    }
}

void NapWaveVisualizer::drawNapWaterLine(juce::Graphics& g, juce::Rectangle<float> bounds, float napY)
{
    // Ambient Weerwater surface ripple
    juce::Path surfacePath;
    surfacePath.startNewSubPath(bounds.getX(), napY);

    for (float x = bounds.getX(); x <= bounds.getRight(); x += 8.0f)
    {
        float relX = (x - bounds.getX()) / bounds.getWidth();
        float ripple = std::sin(relX * 12.0f + waterPhase) * 2.0f * (1.0f + smoothRms * 2.0f);
        surfacePath.lineTo(x, napY + ripple);
    }

    // Dashed technical style
    juce::PathStrokeType stroke(1.2f);
    juce::Path dashedPath;
    float dashLengths[2] = { 6.0f, 4.0f };
    stroke.createDashedStroke(dashedPath, surfacePath, dashLengths, 2);

    g.setColour(BrutalistLookAndFeel::weerwaterTeal.withAlpha(0.6f));
    g.strokePath(dashedPath, stroke);

    // Labeled Datum text
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.0f, juce::Font::bold));
    g.setColour(BrutalistLookAndFeel::weerwaterTeal.withAlpha(0.85f));
    g.drawText("-4.5m NAP (ZEEBODEM)", static_cast<int>(bounds.getX() + 10.0f), static_cast<int>(napY - 14.0f), 180, 12, juce::Justification::left);
}

void NapWaveVisualizer::drawWaveform(juce::Graphics& g, juce::Rectangle<float> bounds, float napY, bool isOverdrive)
{
    juce::Path wavePath;
    juce::Path underwaterGlowPath;

    float width = bounds.getWidth();
    float height = bounds.getHeight();
    float centerY = bounds.getY() + height * 0.45f;
    float ampScale = height * 0.42f;

    bool pathStarted = false;
    float startX = bounds.getX();
    float stepX = width / static_cast<float>(numScopePoints - 1);

    for (int i = 0; i < numScopePoints; ++i)
    {
        float x = startX + static_cast<float>(i) * stepX;
        float audioSample = displayPoints[static_cast<size_t>(i)];

        // Fluid undulation on wave
        float ripple = std::sin(static_cast<float>(i) * 0.15f + waterPhase) * 1.5f * (smoothRms + 0.1f);
        float y = centerY + audioSample * ampScale + ripple;

        if (!pathStarted)
        {
            wavePath.startNewSubPath(x, y);
            pathStarted = true;
        }
        else
        {
            wavePath.lineTo(x, y);
        }
    }

    // Underwater Plunge Fill:
    // If the wave plunges below the -4.5m NAP line, fill the sub-surface basin with a glowing cyan mist
    underwaterGlowPath = wavePath;
    underwaterGlowPath.lineTo(bounds.getRight(), bounds.getBottom());
    underwaterGlowPath.lineTo(bounds.getX(), bounds.getBottom());
    underwaterGlowPath.closeSubPath();

    juce::Colour glowColour = isOverdrive ? BrutalistLookAndFeel::allyRed : BrutalistLookAndFeel::weerwaterTeal;
    float plungeIntensity = juce::jlimit(0.08f, 0.45f, smoothRms * 1.5f + 0.05f);

    juce::ColourGradient subGlow(glowColour.withAlpha(plungeIntensity),
                                bounds.getCentreX(), napY,
                                juce::Colours::transparentBlack,
                                bounds.getCentreX(), bounds.getBottom(), false);
    g.setGradientFill(subGlow);
    g.fillPath(underwaterGlowPath);

    // Overdrive Hot Aura (if active)
    if (isOverdrive)
    {
        g.setColour(BrutalistLookAndFeel::allyRed.withAlpha(0.35f));
        g.strokePath(wavePath, juce::PathStrokeType(6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // Outer Neon Glow of the Vector Waveform
    g.setColour(glowColour.withAlpha(0.3f));
    g.strokePath(wavePath, juce::PathStrokeType(4.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    // Core Crisp Vector Line
    g.setColour(juce::Colours::white);
    g.strokePath(wavePath, juce::PathStrokeType(1.6f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void NapWaveVisualizer::drawTechnicalTelemetry(juce::Graphics& g, juce::Rectangle<float> bounds)
{
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.setColour(BrutalistLookAndFeel::mutedText);

    // Top-left: Title & Mode
    g.drawText("WEERWATER VECTOR SCOPE // NAP LOCK ACTIVE",
               static_cast<int>(bounds.getX() + 10.0f), static_cast<int>(bounds.getY() + 8.0f), 320, 12, juce::Justification::left);

    // Bottom-left: Sub activity meter
    juce::String status = smoothRms > 0.02f ? "SUB DIVE ACTIVE [-4.5m]" : "POLDER STANDBY";
    g.setColour(smoothRms > 0.02f ? BrutalistLookAndFeel::weerwaterTeal : BrutalistLookAndFeel::mutedText);
    g.drawText(status, static_cast<int>(bounds.getX() + 10.0f), static_cast<int>(bounds.getBottom() - 18.0f), 200, 12, juce::Justification::left);

    // Bottom-right: NAP-Lock status
    g.setColour(BrutalistLookAndFeel::mutedText);
    g.drawText("1300 MONO PHASE COHERENCE: 100%",
               static_cast<int>(bounds.getRight() - 250.0f), static_cast<int>(bounds.getBottom() - 18.0f), 240, 12, juce::Justification::right);
}

} // namespace AllyGUI
