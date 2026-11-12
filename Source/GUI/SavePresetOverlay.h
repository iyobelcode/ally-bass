#pragma once

#include "EngineeredDepthModal.h"
#include "../Presets/PresetManager.h"

namespace AllyGUI
{

/**
 * In-editor Engineered Depth Save Preset dialog for creating/updating .allypreset files.
 * Enforces factory preset protection, empty-name checks, and two-step overwrite confirmation.
 */
class SavePresetOverlay : public EngineeredDepthModalOverlay
{
public:
    using SaveCallback = std::function<bool(const juce::File& targetFile,
                                            const juce::String& presetName,
                                            bool allowOverwrite,
                                            juce::String& outError)>;

    explicit SavePresetOverlay(SaveCallback onSavePreset);
    ~SavePresetOverlay() override = default;

    void prepareForSave(const AllyPresets::PresetStateInfo& currentState);
    void setTargetDirectory(const juce::File& dir);
    const juce::File& getTargetDirectory() const noexcept { return targetDirectory; }

    bool attemptSave();
    bool isOverwritePending() const noexcept { return overwriteConfirmedName.isNotEmpty(); }
    const juce::String& getStatusMessage() const noexcept { return statusMessage; }

    juce::TextEditor& getNameEditor() noexcept { return nameEditor; }
    EngineeredDepthActionButton& getSaveButton() noexcept { return saveButton; }

    void paint(juce::Graphics& g) override;

protected:
    juce::Point<int> computePreferredPanelSize(int hostWidth, int hostHeight) const override;
    void layoutModalContent(juce::Rectangle<int> contentArea) override;
    void onModalOpened() override;

private:
    void resetConflictState();
    void chooseFolderAsync();
    juce::File buildTargetFileForInput() const;
    juce::String buildUniqueDefaultName(const AllyPresets::PresetStateInfo& currentState) const;

    SaveCallback saveCallback;
    juce::File targetDirectory;
    std::unique_ptr<juce::FileChooser> folderChooser;

    juce::TextEditor nameEditor;
    EngineeredDepthActionButton browseButton { "KIES MAP...", EngineeredDepthActionButton::Style::Secondary, "saveBrowseDir" };
    EngineeredDepthActionButton cancelButton { "ANNULEREN", EngineeredDepthActionButton::Style::Secondary, "saveCancel" };
    EngineeredDepthActionButton saveButton   { "OPSLAAN", EngineeredDepthActionButton::Style::Primary, "saveConfirm" };

    juce::Rectangle<int> nameLabelBounds;
    juce::Rectangle<int> folderLabelBounds;
    juce::Rectangle<int> folderPathBounds;
    juce::Rectangle<int> statusBounds;

    juce::String overwriteConfirmedName;
    juce::File overwriteConfirmedFile;
    juce::String statusMessage;
    bool statusIsError = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SavePresetOverlay)
};

} // namespace AllyGUI
