#include "SavePresetOverlay.h"

namespace AllyGUI
{

SavePresetOverlay::SavePresetOverlay(SaveCallback onSavePreset)
    : EngineeredDepthModalOverlay("PRESET OPSLAAN", "USER PRESET (.ALLYPRESET)"),
      saveCallback(std::move(onSavePreset)),
      targetDirectory(AllyPresets::getDefaultUserPresetsDirectory())
{
    setName("SavePresetOverlay");

    nameEditor.setName("savePresetNameEditor");
    nameEditor.setMultiLine(false);
    nameEditor.setReturnKeyStartsNewLine(false);
    nameEditor.setInputRestrictions(64);
    nameEditor.setColour(juce::TextEditor::backgroundColourId,
                         EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.35f));
    nameEditor.setColour(juce::TextEditor::textColourId, EngineeredDepthTheme::Colors::textPrimary);
    nameEditor.setColour(juce::TextEditor::outlineColourId, EngineeredDepthTheme::Colors::stroke);
    nameEditor.setColour(juce::TextEditor::focusedOutlineColourId, EngineeredDepthTheme::Colors::cyan);
    nameEditor.setColour(juce::TextEditor::highlightColourId, EngineeredDepthTheme::Colors::cyan.withAlpha(0.28f));
    nameEditor.setColour(juce::CaretComponent::caretColourId, EngineeredDepthTheme::Colors::cyanBright);
    nameEditor.setFont(EngineeredDepthTheme::makeDataFont(13.0f, false));
    nameEditor.setTextToShowWhenEmpty("Bijv. Almere_036_Sub", EngineeredDepthTheme::Colors::textMuted);

    nameEditor.onTextChange = [this]() { resetConflictState(); };
    nameEditor.onReturnKey  = [this]() { attemptSave(); };
    nameEditor.onEscapeKey  = [this]() { closeModal(); };
    addAndMakeVisible(nameEditor);

    browseButton.onClick = [this]() { chooseFolderAsync(); };
    cancelButton.onClick = [this]() { closeModal(); };
    saveButton.onClick   = [this]() { attemptSave(); };

    addAndMakeVisible(browseButton);
    addAndMakeVisible(cancelButton);
    addAndMakeVisible(saveButton);
}

void SavePresetOverlay::resetConflictState()
{
    overwriteConfirmedName.clear();
    overwriteConfirmedFile = juce::File();
    saveButton.setButtonText("OPSLAAN");
    saveButton.setStyle(EngineeredDepthActionButton::Style::Primary);
    statusMessage.clear();
    statusIsError = false;
    repaint();
}

void SavePresetOverlay::setTargetDirectory(const juce::File& dir)
{
    if (dir != juce::File())
    {
        targetDirectory = dir;
        resetConflictState();
        repaint();
    }
}

juce::String SavePresetOverlay::buildUniqueDefaultName(const AllyPresets::PresetStateInfo& currentState) const
{
    juce::String baseName = "Almere_Sub_Custom";
    if (currentState.kind == AllyPresets::PresetKind::User && currentState.displayName.isNotEmpty())
        baseName = currentState.displayName;
    else if (currentState.kind == AllyPresets::PresetKind::Factory && currentState.displayName.isNotEmpty())
        baseName = currentState.displayName + "_Edit";

    baseName = AllyPresets::sanitizePresetFilename(baseName);

    // Propose an unused name first so users don't accidentally hit an existing file immediately
    for (int suffix = 1; suffix <= 99; ++suffix)
    {
        const juce::String candidate = (suffix == 1)
            ? baseName
            : (baseName + "_" + juce::String(suffix));
        const auto file = targetDirectory.getChildFile(candidate + AllyPresets::kPresetFileExtension);
        if (!file.existsAsFile())
            return candidate;
    }

    return baseName + "_" + juce::String(juce::Time::currentTimeMillis() % 10000);
}

void SavePresetOverlay::prepareForSave(const AllyPresets::PresetStateInfo& currentState)
{
    if (currentState.kind == AllyPresets::PresetKind::User && currentState.filePath.isNotEmpty())
    {
        const juce::File currentFile(currentState.filePath);
        if (currentFile.getParentDirectory().isDirectory())
            targetDirectory = currentFile.getParentDirectory();
    }
    else if (targetDirectory == juce::File())
    {
        targetDirectory = AllyPresets::getDefaultUserPresetsDirectory();
    }

    nameEditor.setText(buildUniqueDefaultName(currentState), juce::dontSendNotification);
    resetConflictState();
}

void SavePresetOverlay::onModalOpened()
{
    nameEditor.grabKeyboardFocus();
    nameEditor.selectAll();
}

juce::File SavePresetOverlay::buildTargetFileForInput() const
{
    const auto safeFilename = AllyPresets::sanitizePresetFilename(nameEditor.getText());
    return targetDirectory.getChildFile(safeFilename + AllyPresets::kPresetFileExtension);
}

void SavePresetOverlay::chooseFolderAsync()
{
    const auto startDir = targetDirectory.isDirectory()
        ? targetDirectory
        : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);

    folderChooser = std::make_unique<juce::FileChooser>("Kies doelmap voor user preset",
                                                        startDir,
                                                        juce::String());
    auto flags = juce::FileBrowserComponent::openMode
               | juce::FileBrowserComponent::canSelectDirectories;

    juce::Component::SafePointer<SavePresetOverlay> safeThis(this);
    folderChooser->launchAsync(flags, [safeThis](const juce::FileChooser& chooser)
    {
        if (safeThis == nullptr)
            return;

        const auto chosen = chooser.getResult();
        if (chosen.isDirectory())
            safeThis->setTargetDirectory(chosen);
    });
}

bool SavePresetOverlay::attemptSave()
{
    const auto cleanName = AllyPresets::sanitizePresetDisplayName(nameEditor.getText());
    if (cleanName.isEmpty())
    {
        statusMessage = "Voer eerst een geldige presetnaam in.";
        statusIsError = true;
        repaint();
        return false;
    }

    if (AllyPresets::isFactoryPresetName(cleanName))
    {
        statusMessage = "Factory presets zijn alleen-lezen. Kies een eigen presetnaam.";
        statusIsError = true;
        repaint();
        return false;
    }

    const auto targetFile = buildTargetFileForInput();
    const bool fileExists = targetFile.existsAsFile();
    const bool alreadyConfirmed = (fileExists
                                   && overwriteConfirmedName == cleanName
                                   && overwriteConfirmedFile == targetFile);

    if (fileExists && !alreadyConfirmed)
    {
        overwriteConfirmedName = cleanName;
        overwriteConfirmedFile = targetFile;
        statusMessage = "Bestand bestaat al: \"" + targetFile.getFileName()
                        + "\". Klik nogmaals op OVERSCHRIJVEN om te bevestigen.";
        statusIsError = true;
        saveButton.setButtonText("OVERSCHRIJVEN");
        saveButton.setStyle(EngineeredDepthActionButton::Style::Warning);
        repaint();
        return false;
    }

    juce::String error;
    const bool saved = saveCallback && saveCallback(targetFile, cleanName, alreadyConfirmed, error);
    if (!saved)
    {
        statusMessage = error.isNotEmpty() ? error : "Opslaan mislukt.";
        statusIsError = true;
        repaint();
        return false;
    }

    resetConflictState();
    closeModal();
    return true;
}

juce::Point<int> SavePresetOverlay::computePreferredPanelSize(int hostWidth, int hostHeight) const
{
    return { juce::jlimit(400, hostWidth - 36, 510),
             juce::jlimit(250, hostHeight - 36, 286) };
}

void SavePresetOverlay::layoutModalContent(juce::Rectangle<int> contentArea)
{
    auto area = contentArea;

    nameLabelBounds = area.removeFromTop(20);
    area.removeFromTop(4);
    nameEditor.setBounds(area.removeFromTop(30));

    area.removeFromTop(10);
    folderLabelBounds = area.removeFromTop(18);
    area.removeFromTop(4);

    auto folderRow = area.removeFromTop(28);
    browseButton.setBounds(folderRow.removeFromRight(106));
    folderRow.removeFromRight(8);
    folderPathBounds = folderRow;

    area.removeFromTop(8);
    auto buttonsRow = area.removeFromBottom(32);
    saveButton.setBounds(buttonsRow.removeFromRight(132).reduced(0, 1));
    buttonsRow.removeFromRight(8);
    cancelButton.setBounds(buttonsRow.removeFromRight(104).reduced(0, 1));

    area.removeFromBottom(6);
    statusBounds = area;
}

void SavePresetOverlay::paint(juce::Graphics& g)
{
    EngineeredDepthModalOverlay::paint(g);

    if (nameLabelBounds.isEmpty())
        return;

    g.setFont(EngineeredDepthTheme::makeUiFont(11.0f, true));
    g.setColour(EngineeredDepthTheme::Colors::textSecondary);
    g.drawText("PRESETNAAM", nameLabelBounds, juce::Justification::centredLeft);
    g.drawText("DOELMAP (.ALLYPRESET)", folderLabelBounds, juce::Justification::centredLeft);

    if (!folderPathBounds.isEmpty())
    {
        const auto pf = folderPathBounds.toFloat();
        g.setColour(EngineeredDepthTheme::Colors::black.interpolatedWith(EngineeredDepthTheme::Colors::surface1, 0.30f));
        g.fillRoundedRectangle(pf, 5.0f);
        g.setColour(EngineeredDepthTheme::Colors::strokeSubtle);
        g.drawRoundedRectangle(pf, 5.0f, 1.0f);

        g.setColour(EngineeredDepthTheme::Colors::textSecondary);
        g.setFont(EngineeredDepthTheme::makeDataFont(10.0f, false));
        g.drawFittedText(targetDirectory.getFullPathName(),
                         folderPathBounds.reduced(8, 2),
                         juce::Justification::centredLeft,
                         1,
                         0.85f);
    }

    if (!statusBounds.isEmpty() && statusMessage.isNotEmpty())
    {
        const auto sf = statusBounds.toFloat();
        const auto accent = statusIsError ? EngineeredDepthTheme::Colors::warning
                                          : EngineeredDepthTheme::Colors::cyan;
        g.setColour(accent.withAlpha(0.11f));
        g.fillRoundedRectangle(sf, 5.0f);
        g.setColour(accent.withAlpha(0.65f));
        g.drawRoundedRectangle(sf, 5.0f, 1.0f);

        g.setColour(accent.brighter(0.25f));
        g.setFont(EngineeredDepthTheme::makeUiFont(10.5f, false));
        g.drawFittedText(statusMessage,
                         statusBounds.reduced(8, 3),
                         juce::Justification::centredLeft,
                         2,
                         0.88f);
    }
}

} // namespace AllyGUI
