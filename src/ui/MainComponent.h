#pragma once

#include <optional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MainLayoutGeometry.h"
#include "core/ProjectController.h"
#include "hermes/ComposerAssistantConnector.h"
#include "hermes/HermesJobRunner.h"
#include "hermes/HermesProjectResult.h"

namespace dawhermes::ui {

class MainComponent final : public juce::Component,
                            private juce::MenuBarModel,
                            private juce::ListBoxModel {
public:
    explicit MainComponent(juce::ApplicationProperties& applicationProperties);
    ~MainComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;
    void mouseMove(const juce::MouseEvent& event) override;
    void mouseExit(const juce::MouseEvent& event) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    enum class ActiveSplitter {
        none,
        leftVertical,
        rightVertical,
        horizontal
    };

    enum CommandId {
        commandNewProject = 1000,
        commandExit,
        commandUndo,
        commandRedo,
        commandResetPanelLayout,
        commandAddAudioTrack,
        commandAddMidiTrack,
        commandImportMidiTrack,
        commandAssignAudioFile,
        commandDeleteSelectedTrack,
        commandAbout,
        commandHermesDrumsMakeMidi,
        commandHermesDrumMapping,
        commandHermesBassRepair,
        commandHermesSynchronize,
        commandHermesSetFixBpm,
        commandClearHermesCache,
        commandComposerAssistantSettings,
        commandComposerAssistantProbe
    };

    juce::StringArray getMenuBarNames() override;
    juce::PopupMenu getMenuForIndex(int topLevelMenuIndex, const juce::String& menuName) override;
    void menuItemSelected(int menuItemID, int topLevelMenuIndex) override;

    int getNumRows() override;
    void paintListBoxItem(
        int rowNumber,
        juce::Graphics& g,
        int width,
        int height,
        bool rowIsSelected) override;
    void listBoxItemClicked(int row, const juce::MouseEvent& event) override;

    void executeCommand(int commandId);

    juce::PopupMenu buildHermesMenu() const;
    juce::PopupMenu buildTrackContextMenu() const;

    std::optional<hermes::HermesTrackContext> selectedTrackContext() const;
    std::optional<hermes::HermesAudioMidiPairContext> selectedAudioMidiPairContext() const;
    std::vector<core::Track> selectedTracks() const;
    std::optional<core::Track> selectedTrack() const;

    void showTrackContextMenu();
    void refreshTrackView();
    void updateStatusForSelection();

    void addAudioTrack();
    void addMidiTrack();
    void importMidiTrack();
    void assignAudioFileToSelectedTrack();
    void deleteSelectedTrack();

    void runHermesDrumsMakeMidi();
    void runHermesDrumMapping();
    void runHermesBassRepair();
    void runHermesSynchronize();
    void runHermesSetFixBpm();
    void runUndo();
    void runRedo();
    void runResetPanelLayout();
    void runClearHermesCache();
    void runComposerAssistantSettings();
    void runComposerAssistantProbe();

    bool canUndo() const;
    bool canRedo() const;

    ActiveSplitter splitterAt(juce::Point<int> position) const;
    void updateCursorForSplitters(juce::Point<int> position);
    void applySplitterDrag(juce::Point<int> position);
    void loadPanelLayoutState();
    void savePanelLayoutState() const;

    bool isHermesJobRunning() const;
    void completeHermesJob(hermes::HermesJobResult result);
    void startHermesJob(
        const hermes::HermesJobRequest& request,
        const juce::String& runningStatus,
        const juce::String& operationLabel);
    juce::String describeActiveOperation() const;
    void cleanupStaleHermesCacheOnStartup();

    void loadComposerSettings();
    void saveComposerSettings() const;

    void resetProject();
    void showAbout();

    juce::ApplicationProperties& applicationProperties_;
    std::unique_ptr<hermes::HermesJobRunner> hermesJobRunner_;
    hermes::ComposerAssistantConnector composerConnector_;
    hermes::ComposerAssistantSettings composerSettings_;
    std::optional<hermes::AppliedHermesResult> undoResult_;
    std::optional<hermes::AppliedHermesResult> redoResult_;
    std::optional<hermes::HermesOperationKind> activeOperation_;

    core::ProjectModel projectModel_;
    core::SelectionState selectionState_;
    core::ProjectController projectController_;

    juce::MenuBarComponent menuBar_;

    juce::Label transportLabel_;
    juce::Label tracksHeaderLabel_;
    juce::ListBox trackList_;
    juce::Label timelineLabel_;
    juce::Label midiEditorLabel_;
    juce::Label aiAssistantLabel_;
    juce::Label statusLabel_;

    core::MainPanelLayoutState panelLayoutState_;
    core::MainLayoutGeometry lastLayout_;
    core::MainLayoutGeometry dragStartLayout_;
    core::MainPanelLayoutState dragStartPanelLayoutState_;
    juce::Point<int> dragStartPosition_;
    ActiveSplitter activeSplitter_ { ActiveSplitter::none };
};

}  // namespace dawhermes::ui
