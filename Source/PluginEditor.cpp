#include "PluginEditor.h"
#include "Presets/PresetManager.h"

AllyBassAudioProcessorEditor::AllyBassAudioProcessorEditor(AllyBassAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      visualizer(p),
      keyboardComponent(audioProcessor.getKeyboardState(), juce::MidiKeyboardComponent::horizontalKeyboard)
{
    setLookAndFeel(&brutalistLookAndFeel);

    // 1. Central Visualizer
    addAndMakeVisible(visualizer);

    // 2. The 5 Big Primary Knobs
    knobDiepte = std::make_unique<AllyGUI::HeavyKnob>(
        "DIEPTE", "diepte", audioProcessor.apvts, "NAP", false, false);
    addAndMakeVisible(*knobDiepte);

    knobPolderPunch = std::make_unique<AllyGUI::HeavyKnob>(
        "POLDER PUNCH", "polderPunch", audioProcessor.apvts, "%", true, false);
    addAndMakeVisible(*knobPolderPunch);

    knobHavenDirt = std::make_unique<AllyGUI::HeavyKnob>(
        "HAVEN DIRT", "havenDirt", audioProcessor.apvts, "DIRT", true, false);
    addAndMakeVisible(*knobHavenDirt);

    knobWeerwaterGlide = std::make_unique<AllyGUI::HeavyKnob>(
        "WEERWATER GLIDE", "weerwaterGlide", audioProcessor.apvts, "ms", false, false);
    addAndMakeVisible(*knobWeerwaterGlide);

    knobMasterVol = std::make_unique<AllyGUI::HeavyKnob>(
        "OUTPUT", "masterVolume", audioProcessor.apvts, "dB", false, false);
    addAndMakeVisible(*knobMasterVol);

    // 3. The 2 Compact Knobs (Attack & Release) placed outside the main big row
    knobAttack = std::make_unique<AllyGUI::HeavyKnob>(
        "ATTACK", "attack", audioProcessor.apvts, "ms", false, true);
    addAndMakeVisible(*knobAttack);

    knobRelease = std::make_unique<AllyGUI::HeavyKnob>(
        "RELEASE", "release", audioProcessor.apvts, "ms", false, true);
    addAndMakeVisible(*knobRelease);

    // 4. Stadsdeel Mode Selector
    stadsdeelLabel.setText("STADSDEEL DSP", juce::dontSendNotification);
    stadsdeelLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
    stadsdeelLabel.setColour(juce::Label::textColourId, AllyGUI::BrutalistLookAndFeel::industrialText);
    stadsdeelLabel.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(stadsdeelLabel);

    stadsdeelBox.addItem("Haven (1350) - Tape/Tube", 1);
    stadsdeelBox.addItem("Centrum (1315) - Hard Clip", 2);
    stadsdeelBox.addItem("Poort (1363) - Clean Exciter", 3);
    stadsdeelBox.addItem("Buiten (1330) - Foldback", 4);
    addAndMakeVisible(stadsdeelBox);

    stadsdeelAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(
        audioProcessor.apvts, "stadsdeel", stadsdeelBox);

    // 5. Two Master Switches
    toggleOverdrive036 = std::make_unique<AllyGUI::BrutalistToggle>(
        "036 OVERDRIVE", "overdrive036", audioProcessor.apvts, true);
    addAndMakeVisible(*toggleOverdrive036);

    toggleMonoLock1300 = std::make_unique<AllyGUI::BrutalistToggle>(
        "1300 MONO LOCK", "monoLock1300", audioProcessor.apvts, false);
    addAndMakeVisible(*toggleMonoLock1300);

    // 6. Preset Bar
    setupPresets();

    // 7. Interactive Keyboard Component
    keyboardComponent.setAvailableRange(24, 60); // C1 to C4 - sub bass sweetspot
    keyboardComponent.setKeyWidth(24.0f);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xff22252a));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff101214));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, AllyGUI::BrutalistLookAndFeel::weerwaterTeal.withAlpha(0.3f));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, AllyGUI::BrutalistLookAndFeel::concretePanel);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    addAndMakeVisible(keyboardComponent);

    // Request keyboard focus for QWERTY musical typing
    setWantsKeyboardFocus(true);

    // Resizability
    setResizable(true, true);
    setResizeLimits(680, 500, 1200, 850);
    setSize(780, 580);
}

AllyBassAudioProcessorEditor::~AllyBassAudioProcessorEditor()
{
    setLookAndFeel(nullptr);
}

bool AllyBassAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    // Octave Shift: Z / X
    if (key.getKeyCode() == 'Z' || key.getKeyCode() == 'z')
    {
        currentOctave = juce::jmax(1, currentOctave - 1);
        repaint();
        return true;
    }
    if (key.getKeyCode() == 'X' || key.getKeyCode() == 'x')
    {
        currentOctave = juce::jmin(4, currentOctave + 1);
        repaint();
        return true;
    }
    return false;
}

bool AllyBassAudioProcessorEditor::keyStateChanged(bool isKeyDown)
{
    juce::ignoreUnused(isKeyDown);

    struct KeyMapping {
        int keyCode;
        int noteOffset;
    };

    static const KeyMapping map[] = {
        { 'A', 0 },  // C
        { 'W', 1 },  // C#
        { 'S', 2 },  // D
        { 'E', 3 },  // D#
        { 'D', 4 },  // E
        { 'F', 5 },  // F
        { 'T', 6 },  // F#
        { 'G', 7 },  // G
        { 'Y', 8 },  // G#
        { 'H', 9 },  // A
        { 'U', 10 }, // A#
        { 'J', 11 }, // B
        { 'K', 12 }, // C (+1)
        { 'O', 13 }, // C# (+1)
        { 'L', 14 }, // D (+1)
        { 'P', 15 }  // D# (+1)
    };

    int baseMidiNote = (currentOctave + 1) * 12; // e.g. Octave 2 -> C2 (36)

    for (const auto& k : map)
    {
        bool down = juce::KeyPress::isKeyCurrentlyDown(k.keyCode) ||
                    juce::KeyPress::isKeyCurrentlyDown(std::tolower(k.keyCode));

        auto it = activeNotesByKey.find(k.keyCode);
        bool wasDown = (it != activeNotesByKey.end());

        if (down && !wasDown)
        {
            int note = baseMidiNote + k.noteOffset;
            activeNotesByKey[k.keyCode] = note;
            audioProcessor.getKeyboardState().noteOn(1, note, 0.9f);
        }
        else if (!down && wasDown)
        {
            int note = it->second;
            audioProcessor.getKeyboardState().noteOff(1, note, 0.0f);
            activeNotesByKey.erase(it);
        }
    }

    return true;
}

void AllyBassAudioProcessorEditor::setupPresets()
{
    presetLabel.setText("PRESET", juce::dontSendNotification);
    presetLabel.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 10.5f, juce::Font::bold));
    presetLabel.setColour(juce::Label::textColourId, AllyGUI::BrutalistLookAndFeel::mutedText);
    addAndMakeVisible(presetLabel);

    const auto& presets = AllyPresets::getFactoryPresets();
    for (size_t i = 0; i < presets.size(); ++i)
    {
        presetBox.addItem(presets[i].name, static_cast<int>(i + 1));
    }
    presetBox.setSelectedId(1, juce::dontSendNotification);

    presetBox.onChange = [this]()
    {
        int index = presetBox.getSelectedId() - 1;
        AllyPresets::applyPreset(audioProcessor.apvts, index);
    };

    addAndMakeVisible(presetBox);
}

void AllyBassAudioProcessorEditor::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // 1. Concrete Brutalist Main Background
    g.setColour(AllyGUI::BrutalistLookAndFeel::concreteDark);
    g.fillRect(bounds);

    // Architectural Header Panel
    auto headerArea = bounds.removeFromTop(56.0f);
    g.setColour(AllyGUI::BrutalistLookAndFeel::concretePanel);
    g.fillRect(headerArea);
    g.setColour(AllyGUI::BrutalistLookAndFeel::concreteBorder);
    g.drawHorizontalLine(static_cast<int>(headerArea.getBottom()), 0.0f, bounds.getRight());

    // Logo Typography: ALLY BASS
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 24.0f, juce::Font::bold));
    g.setColour(AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    g.drawText("ALLY BASS", 16, 6, 160, 26, juce::Justification::left);

    // Subtitle & Postcode badge
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.setColour(AllyGUI::BrutalistLookAndFeel::industrialText);
    g.drawText("1300 ALMERE // -4.5m SUB SYNTH", 18, 32, 220, 14, juce::Justification::left);

    // Header Coordinates (Almere Centrum)
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.setColour(AllyGUI::BrutalistLookAndFeel::mutedText);
    g.drawText("52.3702\xc2\xb0 N, 5.2144\xc2\xb0 E", getWidth() - 170, 10, 150, 14, juce::Justification::right);
    g.drawText("NETNUMMER: 036", getWidth() - 170, 26, 150, 14, juce::Justification::right);

    // Panel separator above footer
    float footerY = static_cast<float>(getHeight() - 30);
    g.setColour(AllyGUI::BrutalistLookAndFeel::concreteBorder);
    g.drawHorizontalLine(static_cast<int>(footerY), 0.0f, static_cast<float>(getWidth()));

    // Footer Panel Background
    g.setColour(AllyGUI::BrutalistLookAndFeel::concretePanel);
    g.fillRect(0.0f, footerY, static_cast<float>(getWidth()), 30.0f);

    // Footer Left: Keyboard instruction & Octave status
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
    g.setColour(AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    juce::String kbStatus = "OCTAVE: C" + juce::String(currentOctave) + " (Z/X) // KEYS [A-K] ACTIVE";
    g.drawText(kbStatus, 16, static_cast<int>(footerY + 6), 300, 16, juce::Justification::left);

    // Footer Center/Right: Requested Master Credits
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.0f, juce::Font::bold));
    g.setColour(AllyGUI::BrutalistLookAndFeel::industrialText);
    g.drawText("JUICED UNICORN / IZHIA ARTS / 42GRAM", getWidth() - 320, static_cast<int>(footerY + 6), 300, 16, juce::Justification::right);
}

void AllyBassAudioProcessorEditor::resized()
{
    auto area = getLocalBounds();

    // Header area (56px)
    area.removeFromTop(56);
    int presetWidth = 236;
    presetLabel.setBounds(getWidth() / 2 - presetWidth / 2 - 58, 16, 52, 24);
    presetBox.setBounds(getWidth() / 2 - presetWidth / 2, 16, presetWidth, 24);

    // Footer area (30px)
    area.removeFromBottom(30);

    // Bottom Keyboard area (58px)
    auto keyboardArea = area.removeFromBottom(58).reduced(14, 2);
    keyboardComponent.setBounds(keyboardArea);

    // Middle Visualizer Area (~140px)
    auto visualizerArea = area.removeFromTop(140).reduced(14, 4);
    visualizer.setBounds(visualizerArea);

    // Controls Section
    auto controlsArea = area.reduced(14, 4);

    // 1. Bottom Sub-Row for:
    // [ STADSDEEL SELECTOR ] [ ATTACK (mini) ] [ RELEASE (mini) ] [ 036 OVERDRIVE ] [ 1300 MONO LOCK ]
    auto bottomRow = controlsArea.removeFromBottom(56);

    int stadsdeelWidth = 190;
    int miniKnobWidth = 58;
    int switchWidth = 145;
    int gap = 10;

    int totalBottomWidth = stadsdeelWidth + gap + (miniKnobWidth * 2 + 4) + gap + (switchWidth * 2 + gap);
    int startX = controlsArea.getX() + (controlsArea.getWidth() - totalBottomWidth) / 2;

    // Stadsdeel
    stadsdeelLabel.setBounds(startX, bottomRow.getY() + 2, stadsdeelWidth, 14);
    stadsdeelBox.setBounds(startX, bottomRow.getY() + 18, stadsdeelWidth, 28);
    startX += stadsdeelWidth + gap;

    // Small Attack & Release Knobs
    knobAttack->setBounds(startX, bottomRow.getY(), miniKnobWidth, bottomRow.getHeight());
    startX += miniKnobWidth + 4;

    knobRelease->setBounds(startX, bottomRow.getY(), miniKnobWidth, bottomRow.getHeight());
    startX += miniKnobWidth + gap;

    // Master Switches
    toggleOverdrive036->setBounds(startX, bottomRow.getY() + 18, switchWidth, 28);
    startX += switchWidth + gap;

    toggleMonoLock1300->setBounds(startX, bottomRow.getY() + 18, switchWidth, 28);

    // 2. Top Main Knobs Row: Exactly 5 BIG, HEAVY KNOBS
    // [ DIEPTE ] [ POLDER PUNCH ] [ HAVEN DIRT ] [ WEERWATER GLIDE ] [ OUTPUT ]
    int numBigKnobs = 5;
    float bigKnobWidth = static_cast<float>(controlsArea.getWidth()) / static_cast<float>(numBigKnobs);

    knobDiepte->setBounds(static_cast<int>(controlsArea.getX() + 0 * bigKnobWidth), controlsArea.getY(), static_cast<int>(bigKnobWidth), controlsArea.getHeight());
    knobPolderPunch->setBounds(static_cast<int>(controlsArea.getX() + 1 * bigKnobWidth), controlsArea.getY(), static_cast<int>(bigKnobWidth), controlsArea.getHeight());
    knobHavenDirt->setBounds(static_cast<int>(controlsArea.getX() + 2 * bigKnobWidth), controlsArea.getY(), static_cast<int>(bigKnobWidth), controlsArea.getHeight());
    knobWeerwaterGlide->setBounds(static_cast<int>(controlsArea.getX() + 3 * bigKnobWidth), controlsArea.getY(), static_cast<int>(bigKnobWidth), controlsArea.getHeight());
    knobMasterVol->setBounds(static_cast<int>(controlsArea.getX() + 4 * bigKnobWidth), controlsArea.getY(), static_cast<int>(bigKnobWidth), controlsArea.getHeight());
}
