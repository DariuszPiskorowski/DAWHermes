#pragma once

#include <optional>

#include <juce_gui_basics/juce_gui_basics.h>

#include "core/ProjectController.h"
#include "hermes/IHermesEngine.h"

namespace dawhermes::ui {

class MainComponent final : public juce::Component,
                            private juce::MenuBarModel,
                            private juce::ListBoxModel {
public:
    explicit MainComponent(hermes::IHermesEngine& hermesEngine);
    ~MainComponent() override;

    void resized() override;
    void paint(juce::Graphics& g) override;

private:
    enum CommandId {
        commandNewProject = 1000,
        commandExit,
        commandUndo,
        commandRedo,
        commandAddAudioTrack,
        commandAddMidiTrack,
        commandDeleteSelectedTrack,
        commandAbout,
        commandHermesDrumsMakeMidi,
        commandHermesDrumMapping,
        commandHermesBassRepair,
        commandHermesSynchronize,
        commandHermesSetFixBpm
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
    void deleteSelectedTrack();

    void runHermesDrumsMakeMidi();
    void runHermesDrumMapping();
    void runHermesBassRepair();
    void runHermesSynchronize();
    void runHermesSetFixBpm();

    void resetProject();
    void showAbout();

    hermes::IHermesEngine& hermesEngine_;

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
};

}  // namespace dawhermes::ui
