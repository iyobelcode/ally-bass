#include "SettingsOverlay.h"
#include "../Presets/PresetManager.h"

namespace AllyGUI
{

SettingsOverlay::SettingsOverlay()
    : EngineeredDepthModalOverlay("INSTELLINGEN & INFO", "ALLY - 1300 SUB SYNTH // ENGINEERED DEPTH")
{
    setName("SettingsOverlay");

    doneButton.onClick = [this]() { closeModal(); };
    addAndMakeVisible(doneButton);
}

void SettingsOverlay::setCurrentOctave(int octaveNumber)
{
    if (currentOctave != octaveNumber)
    {
        currentOctave = octaveNumber;
        repaint();
    }
}

void SettingsOverlay::setActivePresetSummary(const juce::String& summaryText)
{
    if (activePresetSummary != summaryText)
    {
        activePresetSummary = summaryText;
        repaint();
    }
}

juce::Point<int> SettingsOverlay::computePreferredPanelSize(int hostWidth, int hostHeight) const
{
    return { juce::jlimit(440, hostWidth - 36, 560),
             juce::jlimit(330, hostHeight - 36, 386) };
}

void SettingsOverlay::layoutModalContent(juce::Rectangle<int> contentArea)
{
    auto remaining = contentArea;
    auto footerRow = remaining.removeFromBottom(32);
    doneButton.setBounds(footerRow.removeFromRight(116).reduced(0, 2));

    remaining.removeFromBottom(8);
    infoCardsArea = remaining;
}

void SettingsOverlay::paint(juce::Graphics& g)
{
    EngineeredDepthModalOverlay::paint(g);

    if (infoCardsArea.isEmpty())
        return;

    struct Section
    {
        juce::String heading;
        juce::String line1;
        juce::String line2;
    };

#ifdef JucePlugin_VersionString
    const juce::String versionStr(JucePlugin_VersionString);
#else
    const juce::String versionStr("1.0.0");
#endif

    const auto presetPath = AllyPresets::getDefaultUserPresetsDirectory().getFullPathName();
    const auto octaveLabel = "C" + juce::String(currentOctave);

    const Section sections[] = {
        {
            "SYSTEEM & VERSIE",
            "ALLY - 1300 SUB SYNTH  |  Versie " + versionStr + "  |  Formaat: VST3 / AU / Standalone",
            "Actieve preset: " + activePresetSummary + "   (14 beveiligde factory presets + .allypreset)"
        },
        {
            "MIDI & QWERTY BEDIENING",
            "Computertoetsenbord: [A - P] chromatische noten   |   Octaaf: [Z] omlaag / [X] omhoog",
            "Huidig basisoctaaf: " + octaveLabel + "   |   MIDI-invoer: Alle kanalen (Ch 1 weergave)"
        },
        {
            "USER PRESETLOCATIE",
            presetPath,
            "Bestandsformaat: .allypreset (schema v1 XML, volledige parameteropslag buiten audio-thread)"
        },
        {
            "CREDITS & LICENTIES",
            "Concept, DSP & UI: JUICED UNICORN / IZHIA ARTS / 42GRAM   |   Almere 036 - 1300 (-4.5m NAP)",
            "Typografie: Systeem UI-sans (" + EngineeredDepthTheme::getUiFontFamily() + ") & mono ("
                + EngineeredDepthTheme::getDataFontFamily() + ") zonder derde-partij fontbundels"
        }
    };

    const int count = 4;
    const int gap = 7;
    const int totalH = infoCardsArea.getHeight() - gap * (count - 1);
    const int cardH = juce::jmax(44, totalH / count);

    const float headFontPx = juce::jlimit(9.0f, 10.5f, static_cast<float>(infoCardsArea.getWidth()) * 0.020f);
    const float bodyFontPx = juce::jlimit(9.0f, 11.0f, static_cast<float>(infoCardsArea.getWidth()) * 0.021f);

    int y = infoCardsArea.getY();
    for (int i = 0; i < count; ++i)
    {
        const int h = (i == count - 1) ? juce::jmax(42, infoCardsArea.getBottom() - y) : cardH;
        const auto card = juce::Rectangle<int>(infoCardsArea.getX(), y, infoCardsArea.getWidth(), h).toFloat();
        y += h + gap;

        g.setColour(EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.42f));
        g.fillRoundedRectangle(card, 7.0f);
        g.setColour(EngineeredDepthTheme::Colors::strokeSubtle.withAlpha(0.85f));
        g.drawRoundedRectangle(card, 7.0f, 1.0f);

        // Left cyan marker stripe
        g.setColour(EngineeredDepthTheme::Colors::cyan.withAlpha(0.75f));
        g.fillRoundedRectangle(card.getX() + 1.0f, card.getY() + 6.0f, 2.5f, card.getHeight() - 12.0f, 1.0f);

        auto textBox = card.reduced(12.0f, 5.0f).toNearestInt();
        const int hH = juce::jmax(13, textBox.getHeight() / 3);

        g.setColour(EngineeredDepthTheme::Colors::cyan);
        g.setFont(EngineeredDepthTheme::makeUiFont(headFontPx, true));
        g.drawFittedText(sections[i].heading, textBox.removeFromTop(hH), juce::Justification::centredLeft, 1, 0.85f);

        const int lH = juce::jmax(13, textBox.getHeight() / 2);
        g.setColour(EngineeredDepthTheme::Colors::textPrimary);
        g.setFont(EngineeredDepthTheme::makeDataFont(bodyFontPx, false));
        g.drawFittedText(sections[i].line1, textBox.removeFromTop(lH), juce::Justification::centredLeft, 1, 0.82f);

        g.setColour(EngineeredDepthTheme::Colors::textSecondary);
        g.setFont(EngineeredDepthTheme::makeUiFont(juce::jmax(8.5f, bodyFontPx * 0.94f), false));
        g.drawFittedText(sections[i].line2, textBox, juce::Justification::centredLeft, 1, 0.82f);
    }
}

} // namespace AllyGUI
