#include "PluginEditor.h"
#include "Presets/PresetManager.h"

#include <cctype>
#include <string_view>

AllyBassAudioProcessorEditor::AllyBassAudioProcessorEditor(AllyBassAudioProcessor& p)
    : AudioProcessorEditor(&p),
      audioProcessor(p),
      visualizer(p)
{
    // 1. Central Visualizer (Weerwater Vector Scope)
    addAndMakeVisible(visualizer);

    // 2. Shared Engineered Depth Primary Knobs Panel + Post-Output Meter (Stap C.2 + C.4B)
    primaryKnobsPanel = std::make_unique<AllyGUI::PrimaryKnobsPanel>(audioProcessor.apvts, &audioProcessor);
    addAndMakeVisible(*primaryKnobsPanel);

    // 3. Shared Engineered Depth Secondary DSP Panel (STADSDEEL DSP, ATTACK/RELEASE Mini Rings, 036 OVERDRIVE, 1300 MONO LOCK)
    secondaryDspPanel = std::make_unique<AllyGUI::SecondaryDspPanel>(audioProcessor.apvts);
    addAndMakeVisible(*secondaryDspPanel);

    // 4. Engineered Depth Top Header, Branding, Preset Toolbar & Modals (Stap C.4A)
    setupHeaderAndModals();

    // 5. Engineered Depth Interactive Keyboard & Octave Controller (Stap C.4C)
    keyboardPanel = std::make_unique<AllyGUI::KeyboardPanel>(audioProcessor.getKeyboardState(), &audioProcessor);
    keyboardPanel->onOctaveStepRequested = [this](int delta)
    {
        setCurrentOctaveSafe(currentOctave + delta);
    };
    addAndMakeVisible(*keyboardPanel);
    updateKeyboardRange();

    // Request keyboard focus on editor only for QWERTY musical typing
    setWantsKeyboardFocus(true);

    // Resizability
    setResizable(true, true);
    setResizeLimits(680, 500, 1200, 850);
    if (resizableCorner != nullptr)
        resizableCorner->setAlpha(0.24f);
    setSize(780, 580);
}

AllyBassAudioProcessorEditor::~AllyBassAudioProcessorEditor()
{
    releaseAllPressedKeys();
}

void AllyBassAudioProcessorEditor::focusGained(juce::Component::FocusChangeType)
{
    repaintFooter();
}

void AllyBassAudioProcessorEditor::focusLost(juce::Component::FocusChangeType)
{
    releaseAllPressedKeys();
    repaintFooter();
}

void AllyBassAudioProcessorEditor::updateKeyboardRange()
{
    if (keyboardPanel != nullptr)
        keyboardPanel->setOctaveDisplay(currentOctave, kMinOctave, kMaxOctave);
}

void AllyBassAudioProcessorEditor::setCurrentOctaveSafe(int targetOctave)
{
    releaseAllPressedKeys();
    currentOctave = juce::jlimit(kMinOctave, kMaxOctave, targetOctave);
    updateKeyboardRange();
    if (settingsOverlay != nullptr)
        settingsOverlay->setCurrentOctave(currentOctave);
    repaintFooter();
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
    audioProcessor.releaseGuiHeldKeysOnKeyboardState();
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

    if (isModalOverlayVisible())
    {
        if (key == juce::KeyPress::escapeKey)
        {
            closeModalOverlays();
            return true;
        }
        return false;
    }

    const int upperKey = std::toupper(key.getKeyCode());

    // Octave Shift: Z / X (synchronized identically with KeyboardPanel - / + buttons)
    if (upperKey == 'Z')
    {
        setCurrentOctaveSafe(currentOctave - 1);
        return true;
    }
    if (upperKey == 'X')
    {
        setCurrentOctaveSafe(currentOctave + 1);
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

    if (isModalOverlayVisible())
    {
        if (!activeNotesByKey.empty())
            releaseQwertyNotesOnly();
        return false;
    }

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

    if (anyHandled)
        repaintFooter();

    return anyHandled;
}

bool AllyBassAudioProcessorEditor::isModalOverlayVisible() const noexcept
{
    return (settingsOverlay != nullptr && settingsOverlay->isVisible())
        || (savePresetOverlay != nullptr && savePresetOverlay->isVisible());
}

void AllyBassAudioProcessorEditor::closeModalOverlays()
{
    releaseAllPressedKeys();
    if (settingsOverlay != nullptr)
        settingsOverlay->setVisible(false);
    if (savePresetOverlay != nullptr)
        savePresetOverlay->setVisible(false);
    grabKeyboardFocus();
    repaintFooter();
}

void AllyBassAudioProcessorEditor::openSettingsModal()
{
    releaseAllPressedKeys();
    if (savePresetOverlay != nullptr)
        savePresetOverlay->setVisible(false);

    if (settingsOverlay != nullptr)
    {
        settingsOverlay->setCurrentOctave(currentOctave);
        if (headerPanel != nullptr)
            settingsOverlay->setActivePresetSummary(headerPanel->getDisplayedPresetText());
        settingsOverlay->openModal();
    }
    repaintFooter();
}

void AllyBassAudioProcessorEditor::openSavePresetModal()
{
    releaseAllPressedKeys();
    if (settingsOverlay != nullptr)
        settingsOverlay->setVisible(false);

    if (savePresetOverlay != nullptr)
    {
        savePresetOverlay->prepareForSave(audioProcessor.getPresetStateInfo());
        savePresetOverlay->openModal();
    }
    repaintFooter();
}

void AllyBassAudioProcessorEditor::openUserPresetAsync()
{
    releaseAllPressedKeys();

    const auto defaultDir = AllyPresets::getDefaultUserPresetsDirectory();
    const auto startDir = defaultDir.isDirectory()
        ? defaultDir
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    openPresetChooser = std::make_unique<juce::FileChooser>("Open ALLY 1300 Preset (.allypreset)",
                                                            startDir,
                                                            AllyPresets::kPresetFileWildcard);

    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectFiles;

    juce::Component::SafePointer<AllyBassAudioProcessorEditor> safeThis(this);
    openPresetChooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        safeThis->releaseAllPressedKeys();
        const auto chosen = chooser.getResult();
        if (chosen == juce::File())
            return;

        juce::String error;
        if (!safeThis->audioProcessor.loadUserPresetFromFile(chosen, error))
        {
            juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                                                   "Preset laden mislukt",
                                                   error.isNotEmpty() ? error : "Ongeldig presetbestand.");
        }

        if (safeThis->headerPanel != nullptr)
            safeThis->headerPanel->refreshFromProcessor();
    });
}

void AllyBassAudioProcessorEditor::setupHeaderAndModals()
{
    headerPanel = std::make_unique<AllyGUI::HeaderPanel>(audioProcessor);
    headerPanel->onBeforeInteractiveAction = [this]() { releaseAllPressedKeys(); };
    headerPanel->onOpenPresetRequested     = [this]() { openUserPresetAsync(); };
    headerPanel->onSavePresetRequested     = [this]() { openSavePresetModal(); };
    headerPanel->onSettingsRequested       = [this]() { openSettingsModal(); };
    addAndMakeVisible(*headerPanel);

    settingsOverlay = std::make_unique<AllyGUI::SettingsOverlay>();
    settingsOverlay->onDismissRequested = [this]()
    {
        releaseAllPressedKeys();
        if (isShowing())
            grabKeyboardFocus();
        repaintFooter();
    };
    addChildComponent(*settingsOverlay);

    savePresetOverlay = std::make_unique<AllyGUI::SavePresetOverlay>(
        [this](const juce::File& targetFile,
               const juce::String& presetName,
               bool allowOverwrite,
               juce::String& outError) -> bool
        {
            const bool ok = audioProcessor.saveUserPresetToFile(targetFile, presetName, allowOverwrite, outError);
            if (ok && headerPanel != nullptr)
                headerPanel->refreshFromProcessor();
            return ok;
        });
    savePresetOverlay->onDismissRequested = [this]()
    {
        releaseAllPressedKeys();
        if (isShowing())
            grabKeyboardFocus();
        repaintFooter();
    };
    addChildComponent(*savePresetOverlay);
}

AllyBassAudioProcessorEditor::LayoutMetrics AllyBassAudioProcessorEditor::computeLayoutMetrics() const noexcept
{
    const float w = static_cast<float>(juce::jmax(1, getWidth()));
    const float h = static_cast<float>(juce::jmax(1, getHeight()));

    LayoutMetrics m;
    m.sideMargin     = juce::jlimit(12, 20, juce::roundToInt(w * 0.018f));
    m.headerHeight   = juce::jlimit(60, 86, juce::roundToInt(h * 0.120f));
    m.footerHeight   = juce::jlimit(24, 32, juce::roundToInt(h * 0.045f));
    m.keyboardHeight = juce::jlimit(62, 94, juce::roundToInt(h * 0.120f));
    m.scopeHeight    = juce::jlimit(122, 212, juce::roundToInt(h * 0.242f));
    return m;
}

juce::Rectangle<int> AllyBassAudioProcessorEditor::getFooterBounds() const noexcept
{
    const auto m = computeLayoutMetrics();
    return { 0, getHeight() - m.footerHeight, getWidth(), m.footerHeight };
}

juce::String AllyBassAudioProcessorEditor::getFooterStatusText() const
{
    const juce::String octText = "OCTAVE: C" + juce::String(currentOctave) + " (Z/X) // ";
    if (isModalOverlayVisible())
        return octText + "QWERTY PAUSED (MODAL)";
    if (!activeNotesByKey.empty())
        return octText + "KEYS [A-P] HELD (" + juce::String(static_cast<int>(activeNotesByKey.size())) + ")";
    if (hasKeyboardFocus(true))
        return octText + "KEYS [A-P] READY";
    return octText + "KEYS [A-P] STANDBY";
}

juce::String AllyBassAudioProcessorEditor::getFooterCreditsText() const
{
    return "JUICED UNICORN / IZHIA ARTS / 42GRAM";
}

void AllyBassAudioProcessorEditor::repaintFooter()
{
    const auto fb = getFooterBounds();
    if (!fb.isEmpty())
        repaint(fb);
}

void AllyBassAudioProcessorEditor::paint(juce::Graphics& g)
{
    using Theme = AllyGUI::EngineeredDepthTheme;

    const auto m = computeLayoutMetrics();
    const auto footerArea = getFooterBounds().toFloat();

    // 1. Cached full-width window backdrop + footer gradient (avoids gradient dither stretching)
    const int w = getWidth();
    const int h = getHeight();
    const int fh = getFooterBounds().getHeight();
    const float scale = juce::jmax(1.0f, g.getInternalContext().getPhysicalPixelScaleFactor());
    if (!cachedBackdropStrip.isValid()
        || cachedBackdropW != w
        || cachedBackdropH != h
        || cachedFooterH != fh
        || std::abs(cachedBackdropScale - scale) > 0.01f)
    {
        const int rw = juce::jmax(1, juce::roundToInt(static_cast<float>(w) * scale));
        const int rh = juce::jmax(1, juce::roundToInt(static_cast<float>(h) * scale));
        cachedBackdropStrip = juce::Image(juce::Image::ARGB, rw, rh, true);
        juce::Graphics cacheG(cachedBackdropStrip);
        cacheG.addTransform(juce::AffineTransform::scale(scale));

        const float wf = static_cast<float>(w);
        const float hf = static_cast<float>(h);
        juce::ColourGradient bgGrad(
            Theme::Colors::bg, wf * 0.5f, 0.0f,
            juce::Colour(0xff040a0e), wf * 0.5f, hf,
            false);
        bgGrad.addColour(0.38, juce::Colour(0xff081822));
        cacheG.setGradientFill(bgGrad);
        cacheG.fillRect(0.0f, 0.0f, wf, hf);

        if (!footerArea.isEmpty())
        {
            juce::ColourGradient footerGrad(
                juce::Colour(0xff050e15), footerArea.getCentreX(), footerArea.getY(),
                juce::Colour(0xff03080c), footerArea.getCentreX(), footerArea.getBottom(),
                false);
            cacheG.setGradientFill(footerGrad);
            cacheG.fillRect(footerArea);

            // Metallic top divider & specular micro-hairline
            cacheG.setColour(Theme::Colors::strokeSubtle.withAlpha(0.74f));
            cacheG.drawHorizontalLine(juce::roundToInt(footerArea.getY()), 0.0f, wf);
            cacheG.setColour(Theme::Colors::metalRimLight.withAlpha(0.09f));
            cacheG.drawHorizontalLine(juce::roundToInt(footerArea.getY()) + 1, 0.0f, wf);
        }

        cachedBackdropW = w;
        cachedBackdropH = h;
        cachedFooterH   = fh;
        cachedBackdropScale = scale;
    }

    g.drawImage(cachedBackdropStrip,
                0, 0, w, h,
                0, 0, cachedBackdropStrip.getWidth(), cachedBackdropStrip.getHeight());

    // 2. Crisp Engineered Depth Footer Bay text & status LED
    if (footerArea.isEmpty())
        return;

    const auto content = footerArea.reduced(static_cast<float>(m.sideMargin), 0.0f);
    const float fontPt = juce::jlimit(8.8f, 11.2f, footerArea.getHeight() * 0.35f);
    const float ledR = 2.2f;
    const float ledCx = content.getX() + ledR + 1.0f;
    const float ledCy = content.getCentreY();

    const bool modalOpen = isModalOverlayVisible();
    const bool heldKeys = !activeNotesByKey.empty();
    const bool focused = hasKeyboardFocus(true);

    const auto statusCol = modalOpen
        ? Theme::Colors::warning
        : (heldKeys ? Theme::Colors::cyan
                    : (focused ? Theme::Colors::cyanFocus : Theme::Colors::cyanDim.withAlpha(0.76f)));

    // Subtle status indicator dot + factual keyboard/octave readout
    g.setColour(statusCol.withAlpha(heldKeys ? 0.30f : 0.14f));
    g.fillEllipse(ledCx - ledR * 2.0f, ledCy - ledR * 2.0f, ledR * 4.0f, ledR * 4.0f);
    g.setColour(statusCol);
    g.fillEllipse(ledCx - ledR, ledCy - ledR, ledR * 2.0f, ledR * 2.0f);

    const auto leftTextArea = content.withTrimmedLeft(ledR * 2.0f + 7.0f)
                                     .withRight(content.getCentreX() + content.getWidth() * 0.10f);
    g.setFont(Theme::makeDataFont(fontPt, true));
    g.setColour(statusCol);
    g.drawText(getFooterStatusText(), leftTextArea, juce::Justification::centredLeft, true);

    // Footer Right: Master Credits (kept clear of bottom-right resize handle)
    const auto rightTextArea = content.withLeft(content.getCentreX() - content.getWidth() * 0.04f)
                                      .withTrimmedRight(6.0f);
    g.setFont(Theme::makeDataFont(fontPt, false));
    g.setColour(Theme::Colors::textSecondary.withAlpha(0.78f));
    g.drawText(getFooterCreditsText(), rightTextArea, juce::Justification::centredRight, true);
}

void AllyBassAudioProcessorEditor::resized()
{
    const auto m = computeLayoutMetrics();
    auto area = getLocalBounds();

    // 1. Engineered Depth Top Header Panel (Stap C.4A)
    auto headerBounds = area.removeFromTop(m.headerHeight).reduced(m.sideMargin, 3);
    if (headerPanel != nullptr)
        headerPanel->setBounds(headerBounds);

    // 2. Engineered Depth Footer Bay (painted directly)
    area.removeFromBottom(m.footerHeight);

    // 3. Engineered Depth Bottom Keyboard & Octave Chassis (Stap C.4C)
    auto keyboardArea = area.removeFromBottom(m.keyboardHeight).reduced(m.sideMargin, 3);
    if (keyboardPanel != nullptr)
        keyboardPanel->setBounds(keyboardArea);

    // 4. Weerwater Vector Scope Bay (Stap C.4B)
    auto visualizerArea = area.removeFromTop(m.scopeHeight).reduced(m.sideMargin, 4);
    visualizer.setBounds(visualizerArea);

    // 5. Controls Bay: Primary Knobs (+ Output Meter) above Secondary DSP Bar (aligned on exact side rails)
    auto controlsArea = area.reduced(m.sideMargin, 4);
    const int secondaryH = juce::jlimit(60, 100, juce::roundToInt(static_cast<float>(controlsArea.getHeight()) * 0.255f));
    auto secondaryArea = controlsArea.removeFromBottom(secondaryH);
    const int bayGap = juce::jlimit(3, 6, juce::roundToInt(static_cast<float>(getHeight()) * 0.007f));
    controlsArea.removeFromBottom(bayGap);

    if (secondaryDspPanel != nullptr)
        secondaryDspPanel->setBounds(secondaryArea);
    if (primaryKnobsPanel != nullptr)
        primaryKnobsPanel->setBounds(controlsArea);

    // Full-editor modal overlays sit cleanly centered above all bays when active
    const auto fullBounds = getLocalBounds();
    if (settingsOverlay != nullptr)
        settingsOverlay->setBounds(fullBounds);
    if (savePresetOverlay != nullptr)
        savePresetOverlay->setBounds(fullBounds);
    cachedBackdropH = 0;
}

size_t AllyBassAudioProcessorEditor::getEstimatedGraphicCacheBytes() const noexcept
{
    size_t bytes = cachedBackdropStrip.isValid()
        ? static_cast<size_t>(cachedBackdropStrip.getWidth()) * static_cast<size_t>(cachedBackdropStrip.getHeight()) * 4u
        : 0u;
    bytes += visualizer.getEstimatedCacheBytes();
    if (headerPanel != nullptr)
        bytes += headerPanel->getEstimatedCacheBytes();
    if (primaryKnobsPanel != nullptr)
        bytes += primaryKnobsPanel->getEstimatedCacheBytes();
    if (secondaryDspPanel != nullptr)
        bytes += secondaryDspPanel->getEstimatedCacheBytes();
    if (keyboardPanel != nullptr)
        bytes += keyboardPanel->getEstimatedCacheBytes();
    return bytes;
}
