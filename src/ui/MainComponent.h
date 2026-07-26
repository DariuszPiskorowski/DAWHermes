#pragma once

#include <atomic>
#include <optional>
#include <thread>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/MainLayoutGeometry.h"
#include "core/ProjectController.h"
#include "hermes/ComposerAssistantConnector.h"
#include "hermes/IHermesEngine.h"
#include "hermes/HermesProjectResult.h"

namespace dawhermes::ui {

class MainComponent final : public juce::Component,
                            private juce::MenuBarModel,
                            private juce::ListBoxModel {
public:
    MainComponent(
        hermes::IHermesEngine& hermesEngine,
        juce::ApplicationProperties& applicationProperties);
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

    struct DrumsJobCompletion {
        hermes::HermesTrackContext context;
        hermes::HermesDrumsOptions options;
        hermes::HermesOperationResult result;
        double durationMs { 0.0 };
    };

    enum CommandId {
        commandNewProject = 1000,
        commandExit,
        commandUndo,
        commandRedo,
        commandResetPanelLayout,
        commandAddAudioTrack,
        commandAddMidiTrack,
        commandAssignAudioFile,
        commandDeleteSelectedTrack,
        commandAbout,
        commandHermesDrumsMakeMidi,
        commandHermesDrumMapping,
        commandHermesBassRepair,
        commandHermesSynchronize,
        commandHermesSetFixBpm,
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
    std::optional<core::Track> selectedTrack() const;

    void showTrackContextMenu();
    void refreshTrackView();
    void updateStatusForSelection();

    void addAudioTrack();
    void addMidiTrack();
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
    void runComposerAssistantSettings();
    void runComposerAssistantProbe();

    bool canUndo() const;
    bool canRedo() const;

    ActiveSplitter splitterAt(juce::Point<int> position) const;
    void updateCursorForSplitters(juce::Point<int> position);
    void applySplitterDrag(juce::Point<int> position);
    void loadPanelLayoutState();
    void savePanelLayoutState() const;

    bool isDrumsJobRunning() const;
    void completeDrumsMakeMidi(DrumsJobCompletion completion);
    void finishDrumsWorkerThread();

    void loadComposerSettings();
    void saveComposerSettings() const;

    void resetProject();
    void showAbout();

    hermes::IHermesEngine& hermesEngine_;
    juce::ApplicationProperties& applicationProperties_;
    hermes::ComposerAssistantConnector composerConnector_;
    hermes::ComposerAssistantSettings composerSettings_;
    std::optional<hermes::AppliedHermesResult> undoResult_;
    std::optional<hermes::AppliedHermesResult> redoResult_;

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

    std::thread drumsWorker_;
    std::atomic<bool> drumsJobRunning_ { false };
};

}  // namespace dawhermes::ui
