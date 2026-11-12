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

    // 2. Shared Engineered Depth Primary Knobs Panel (DIEPTE, POLDER PUNCH, HAVEN DIRT, WATERWIJK GLIDE, OUTPUT)
    primaryKnobsPanel = std::make_unique<AllyGUI::PrimaryKnobsPanel>(audioProcessor.apvts);
    addAndMakeVisible(*primaryKnobsPanel);

    // 3. Shared Engineered Depth Secondary DSP Panel (STADSDEEL DSP, ATTACK/RELEASE Mini Rings, 036 OVERDRIVE, 1300 MONO LOCK)
    secondaryDspPanel = std::make_unique<AllyGUI::SecondaryDspPanel>(audioProcessor.apvts);
    addAndMakeVisible(*secondaryDspPanel);

    // 6. Preset Bar
    setupPresets();

    // 7. Interactive Keyboard Component
    // Schakel ingebouwde QWERTY-mapping (die standaard op C5 staat!) en focus-steal van MidiKeyboardComponent UIT,
    // zodat alleen de Editor QWERTY-toetsen afhandelt in het gekozen octaaf (bijv. C2).
    keyboardComponent.clearKeyMappings();
    keyboardComponent.setWantsKeyboardFocus(false);
    keyboardComponent.setOctaveForMiddleC(4); // C2 = MIDI 36, C4 = MIDI 60
    keyboardComponent.setScrollButtonsVisible(false);
    updateKeyboardRange();
    keyboardComponent.setColour(juce::MidiKeyboardComponent::whiteNoteColourId, juce::Colour(0xff22252a));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::blackNoteColourId, juce::Colour(0xff101214));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::keyDownOverlayColourId, AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::mouseOverKeyOverlayColourId, AllyGUI::BrutalistLookAndFeel::weerwaterTeal.withAlpha(0.3f));
    keyboardComponent.setColour(juce::MidiKeyboardComponent::upDownButtonBackgroundColourId, AllyGUI::BrutalistLookAndFeel::concretePanel);
    keyboardComponent.setColour(juce::MidiKeyboardComponent::upDownButtonArrowColourId, AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    addAndMakeVisible(keyboardComponent);

    // Octave controls
    lblOctave.setText("OCT: C2", juce::dontSendNotification);
    lblOctave.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 11.0f, juce::Font::bold));
    lblOctave.setColour(juce::Label::textColourId, AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    lblOctave.setJustificationType(juce::Justification::centred);
    addAndMakeVisible(lblOctave);

    btnOctaveDown.setColour(juce::TextButton::buttonColourId, AllyGUI::BrutalistLookAndFeel::concretePanel);
    btnOctaveDown.setColour(juce::TextButton::textColourOffId, AllyGUI::BrutalistLookAndFeel::industrialText);
    btnOctaveDown.setWantsKeyboardFocus(false);
    btnOctaveDown.onClick = [this]() {
        releaseAllPressedKeys();
        currentOctave = juce::jmax(0, currentOctave - 1);
        updateKeyboardRange();
        repaint();
    };
    addAndMakeVisible(btnOctaveDown);

    btnOctaveUp.setColour(juce::TextButton::buttonColourId, AllyGUI::BrutalistLookAndFeel::concretePanel);
    btnOctaveUp.setColour(juce::TextButton::textColourOffId, AllyGUI::BrutalistLookAndFeel::industrialText);
    btnOctaveUp.setWantsKeyboardFocus(false);
    btnOctaveUp.onClick = [this]() {
        releaseAllPressedKeys();
        currentOctave = juce::jmin(5, currentOctave + 1);
        updateKeyboardRange();
        repaint();
    };
    addAndMakeVisible(btnOctaveUp);

    // Request keyboard focus for QWERTY musical typing
    setWantsKeyboardFocus(true);

    // Resizability
    setResizable(true, true);
    setResizeLimits(680, 500, 1200, 850);
    setSize(780, 580);
}

AllyBassAudioProcessorEditor::~AllyBassAudioProcessorEditor()
{
    releaseAllPressedKeys();
    setLookAndFeel(nullptr);
}

void AllyBassAudioProcessorEditor::focusGained(juce::Component::FocusChangeType)
{
}

void AllyBassAudioProcessorEditor::focusLost(juce::Component::FocusChangeType)
{
    releaseAllPressedKeys();
}

void AllyBassAudioProcessorEditor::updateKeyboardRange()
{
    int lowNote = (currentOctave + 1) * 12; // e.g. Octave 2 -> MIDI 36 (C2)
    int highNote = juce::jmin(120, lowNote + 36); // 3 volledige octaven (22 witte toetsen)
    keyboardComponent.setAvailableRange(lowNote, highNote);
    lblOctave.setText("OCT: C" + juce::String(currentOctave), juce::dontSendNotification);

    if (keyboardComponent.getWidth() > 0)
        keyboardComponent.setKeyWidth(static_cast<float>(keyboardComponent.getWidth()) / 22.0f);
}

void AllyBassAudioProcessorEditor::releaseQwertyNotesOnly()
{
    for (auto& pair : activeNotesByKey)
    {
        audioProcessor.getKeyboardState().noteOff(1, pair.second, 0.0f);
    }
    activeNotesByKey.clear();
}

void AllyBassAudioProcessorEditor::releaseAllPressedKeys()
{
    releaseQwertyNotesOnly();
    audioProcessor.getKeyboardState().allNotesOff(1);
}

void AllyBassAudioProcessorEditor::modifierKeysChanged(const juce::ModifierKeys& modifiers)
{
    if (modifiers.isCommandDown() || modifiers.isCtrlDown())
    {
        releaseQwertyNotesOnly();
    }
}

bool AllyBassAudioProcessorEditor::keyPressed(const juce::KeyPress& key)
{
    if (key.getModifiers().isCommandDown() || key.getModifiers().isCtrlDown())
        return false;

    const int upperKey = std::toupper(key.getKeyCode());

    // Octave Shift: Z / X
    if (upperKey == 'Z')
    {
        releaseAllPressedKeys();
        currentOctave = juce::jmax(0, currentOctave - 1);
        updateKeyboardRange();
        repaint();
        return true;
    }
    if (upperKey == 'X')
    {
        releaseAllPressedKeys();
        currentOctave = juce::jmin(5, currentOctave + 1);
        updateKeyboardRange();
        repaint();
        return true;
    }

    // Consume all mapped musical keys (including macOS key-repeats) so OS alert beep never fires
    for (const char c : std::string_view("AWSEDFTGYHUJKOLP"))
    {
        if (upperKey == static_cast<int>(c))
            return true;
    }

    return false;
}

bool AllyBassAudioProcessorEditor::keyStateChanged(bool isKeyDown)
{
    juce::ignoreUnused(isKeyDown);

    const auto mods = juce::ModifierKeys::getCurrentModifiersRealtime();
    const bool cmdOrCtrlDown = mods.isCommandDown() || mods.isCtrlDown();

    if (cmdOrCtrlDown)
    {
        releaseQwertyNotesOnly();
        return false;
    }

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

    bool anyHandled = false;

    for (const auto& k : map)
    {
        bool down = juce::KeyPress::isKeyCurrentlyDown(k.keyCode) ||
                    juce::KeyPress::isKeyCurrentlyDown(std::tolower(k.keyCode));

        auto it = activeNotesByKey.find(k.keyCode);
        bool wasDown = (it != activeNotesByKey.end());

        if (down && !wasDown)
        {
            int note = (currentOctave + 1) * 12 + k.noteOffset;
            activeNotesByKey[k.keyCode] = note;
            audioProcessor.getKeyboardState().noteOn(1, note, 0.9f);
            anyHandled = true;
        }
        else if (!down && wasDown)
        {
            int note = it->second;
            activeNotesByKey.erase(it);
            audioProcessor.getKeyboardState().noteOff(1, note, 0.0f);
            anyHandled = true;
        }
    }

    // Veiligheid: als geen enkele toets meer ingedrukt is maar er staan nog actieve keys in de map, stop ze
    if (!activeNotesByKey.empty())
    {
        for (auto it = activeNotesByKey.begin(); it != activeNotesByKey.end(); )
        {
            bool down = juce::KeyPress::isKeyCurrentlyDown(it->first) ||
                        juce::KeyPress::isKeyCurrentlyDown(std::tolower(it->first));
            if (!down)
            {
                int noteToStop = it->second;
                it = activeNotesByKey.erase(it);
                audioProcessor.getKeyboardState().noteOff(1, noteToStop, 0.0f);
                anyHandled = true;
            }
            else
            {
                ++it;
            }
        }
    }

    return anyHandled;
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

    // Logo Typography: ALLY - 1300 SUB SYNTH
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 17.5f, juce::Font::bold));
    g.setColour(AllyGUI::BrutalistLookAndFeel::weerwaterTeal);
    g.drawText("ALLY - 1300 SUB SYNTH", 16, 10, 260, 22, juce::Justification::left);

    // Subtitle & Postcode badge
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.setColour(AllyGUI::BrutalistLookAndFeel::industrialText);
    g.drawText("1300 ALMERE // -4.5m NAP", 18, 34, 240, 14, juce::Justification::left);

    // Header Info
    g.setFont(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), 9.5f, juce::Font::plain));
    g.setColour(AllyGUI::BrutalistLookAndFeel::mutedText);
    g.drawText("NETNUMMER: 036", getWidth() - 170, 18, 150, 14, juce::Justification::right);

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
    int presetWidth = 228;
    int centerX = (280 + (getWidth() - 125)) / 2;
    presetLabel.setBounds(centerX - presetWidth / 2 - 22, 16, 52, 24);
    presetBox.setBounds(centerX - presetWidth / 2 + 32, 16, presetWidth, 24);

    // Footer area (30px)
    area.removeFromBottom(30);

    // Bottom Keyboard area (58px) verdeeld over de volledige breedte met octaaf controls
    auto keyboardRow = area.removeFromBottom(58).reduced(14, 2);
    auto octArea = keyboardRow.removeFromLeft(100);
    btnOctaveDown.setBounds(octArea.getX(), octArea.getY() + 14, 26, 28);
    lblOctave.setBounds(octArea.getX() + 28, octArea.getY() + 14, 44, 28);
    btnOctaveUp.setBounds(octArea.getX() + 74, octArea.getY() + 14, 26, 28);

    keyboardRow.removeFromLeft(8); // kleine marge
    keyboardComponent.setBounds(keyboardRow);
    keyboardComponent.setKeyWidth(static_cast<float>(keyboardRow.getWidth()) / 22.0f);

    // Middle Visualizer Area (~140px)
    auto visualizerArea = area.removeFromTop(140).reduced(14, 4);
    visualizer.setBounds(visualizerArea);

    // Controls Section
    auto controlsArea = area.reduced(14, 4);

    // 1. Secondary DSP Console Bar (Stadsdeel DSP | Attack & Release Mini Rings | 036 Overdrive | 1300 Mono Lock)
    const int secondaryH = juce::jlimit(62, 82, juce::roundToInt(static_cast<float>(controlsArea.getHeight()) * 0.23f));
    auto secondaryArea = controlsArea.removeFromBottom(secondaryH);
    secondaryDspPanel->setBounds(secondaryArea.reduced(0, 1));

    // 2. Top Main Knobs Row: Engineered Depth chamfered chassis with 5 uniform Depth Rings
    primaryKnobsPanel->setBounds(controlsArea.reduced(0, 2));
}
