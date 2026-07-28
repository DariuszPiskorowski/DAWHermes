#pragma once

#include <optional>
#include <memory>
#include <vector>

#include <juce_gui_basics/juce_gui_basics.h>

#include "audio/MidiAuditionEngine.h"
#include "audio/WavBpmDetector.h"
#include "core/MidiComparisonModel.h"
#include "core/MidiNoteSelectionState.h"
#include "core/MidiTimeMap.h"
#include "core/MainLayoutGeometry.h"
#include "core/ProjectController.h"
#include "core/ProjectHistory.h"
#include "core/TimelineGeometry.h"
#include "core/TimelineViewport.h"
#include "hermes/ComposerAssistantConnector.h"
#include "hermes/HermesJobRunner.h"
#include "hermes/HermesProjectResult.h"
#include "ui/MidiComparisonLegend.h"
#include "ui/PianoKeyboardView.h"
#include "ui/PianoRollView.h"
#include "ui/TimelineView.h"
#include "ui/TimeRulerView.h"

namespace dawhermes::ui {

class MainComponent final : public juce::Component,
                            private juce::MenuBarModel,
                            private juce::ListBoxModel,
                            private juce::Timer {
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
    bool keyPressed(const juce::KeyPress& key) override;

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
        commandToggleMidiComparison,
        commandAddMidiTrack,
        commandImportAudioTrack,
        commandImportMidiTrack,
        commandExportSelectedMidiTrack,
        commandDeleteSelectedTrack,
        commandDeleteSelectedNotes,
        commandQuantizeSelectedNotes,
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

    void addMidiTrack();
    void importAudioTracks();
    void importMidiTrack();
    bool canExportSelectedMidiTrack() const;
    void exportSelectedMidiTrack();
    void deleteSelectedTrack();
    bool canDeleteSelectedMidiNotes() const;
    bool canEditSelectedMidiNotes() const;
    void deleteSelectedMidiNotes(bool fromKeyboard);
    void createMidiNoteAt(double beat, int pitch);
    void moveSelectedMidiNotes(
        std::vector<std::uint64_t> selectedNoteIds,
        double deltaBeats,
        int deltaSemitones,
        juce::String statusPrefix,
        bool snapEnabled);
    void resizeSelectedMidiNotes(std::uint64_t anchorNoteId, std::vector<std::uint64_t> selectedNoteIds, double requestedAnchorEndBeat);
    void nudgeSelectedMidiNotes(int deltaSemitones, double deltaBeats);
    void applyVelocityEditorValue();
    void applyVelocityToSelectedMidiNotes(int velocity);
    void quantizeSelectedMidiNotesToGrid();
    void selectMidiNote(std::uint64_t noteId, bool additive);
    void clearMidiNoteSelectionFromEmptyClick(bool additive);
    void applyMidiNoteMarqueeSelection(std::vector<std::uint64_t> noteIds, bool additive);
    void refreshAfterMidiNoteMutation();
    void synchronizeMidiNoteSelectionWithEditableTrack();
    void updateStatusForMidiNoteSelection();
    void startSelectedMidiPlayback();
    void pausePlayback();
    void stopMidiPlayback();
    void panicMidiPlayback();
    void seekPlayback(double deltaSeconds);
    void refreshTransportSelectionState();
    void requestSelectedWavBpmIfNeeded();
    void processCompletedWavBpmAnalysis();
    void updateTransportDisplays();
    void updatePlaybackPlayhead(bool ensureVisible);
    void updateHorizontalViewportViews();
    void updateTransportControlState();
    void timerCallback() override;
    void updateVelocityControlState();
    std::optional<int> primarySelectedMidiNoteVelocity() const;
    std::optional<std::uint64_t> editableMidiTrackIdForPianoRoll() const;
    core::Track* editableMidiTrackForPianoRoll();
    const core::Track* editableMidiTrackForPianoRoll() const;
    bool shouldIgnoreKeyboardDeletion() const;
    bool shouldIgnoreKeyboardMidiEditing() const;

    void runHermesDrumsMakeMidi();
    void runHermesDrumMapping();
    void runHermesBassRepair();
    void runHermesSynchronize();
    void runHermesSetFixBpm();
    void runUndo();
    void runRedo();
    void runResetPanelLayout();
    void runToggleMidiComparison();
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
    void loadMidiEditingSettings();
    void saveMidiEditingSettings() const;

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

    void initializeVisualWorkspace();
    void updateVisualWorkspace();
    void updateHorizontalScrollRange();
    double estimateHorizontalContentEndBeat() const;
    void applyHorizontalZoom(double zoomFactor);
    void fitHorizontalToProject();
    void applyPitchZoom(double zoomFactor);
    void fitPitchToActiveNotes();
    double estimateProjectDurationBeats() const;
    std::optional<std::pair<core::Track, core::Track>> selectedMidiComparisonPair() const;
    std::optional<core::Track> activeMidiTrackForPianoRoll() const;

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
    core::MidiNoteSelectionState midiNoteSelectionState_;
    core::ProjectHistory projectHistory_;
    core::ProjectController projectController_;
    audio::MidiAuditionEngine midiAuditionEngine_;
    audio::WavBpmAnalysisService wavBpmAnalysisService_;

    juce::MenuBarComponent menuBar_;

    juce::Label transportLabel_;
    juce::TextButton rewindButton_ { "<<" };
    juce::TextButton playButton_ { "Play" };
    juce::TextButton pauseButton_ { "Pause" };
    juce::TextButton stopButton_ { "Stop" };
    juce::TextButton fastForwardButton_ { ">>" };
    juce::Label timeCounterLabel_;
    juce::Label bpmLabel_;
    juce::TextButton panicButton_ { "Panic" };
    juce::Label volumeLabel_;
    juce::Slider volumeSlider_;
    juce::Label tracksHeaderLabel_;
    juce::ListBox trackList_;
    juce::ComboBox gridCombo_;
    juce::ToggleButton snapToggle_ { "Snap" };
    juce::TextButton horizontalZoomOutButton_ { "-" };
    juce::TextButton horizontalZoomInButton_ { "+" };
    juce::TextButton horizontalFitButton_ { "Fit" };
    juce::Slider horizontalScrollSlider_;
    TimeRulerView timeRulerView_;
    TimelineView timelineView_;
    juce::TextButton pitchZoomOutButton_ { "Pitch -" };
    juce::TextButton pitchZoomInButton_ { "Pitch +" };
    juce::TextButton pitchFitButton_ { "Fit Notes" };
    juce::Label velocityLabel_;
    juce::TextEditor velocityEditor_;
    PianoKeyboardView pianoKeyboardView_;
    PianoRollView pianoRollView_;
    juce::Slider pianoPitchScrollSlider_;
    MidiComparisonLegend midiComparisonLegend_;
    juce::Label aiAssistantLabel_;
    juce::Label statusLabel_;

    core::TimelineViewportState timelineViewportState_;
    core::PitchViewportState pitchViewportState_;
    core::ResolvedMidiTimelineInfo resolvedTimelineInfo_;
    int gridDenominator_ { 16 };
    bool snapEnabled_ { true };
    bool suppressVelocityEditorCallbacks_ { false };
    bool midiComparisonEnabled_ { false };
    bool suppressViewportControlCallbacks_ { false };
    audio::SelectionPlaybackSummary transportSelectionSummary_;
    std::optional<audio::WavBpmAnalysisResult> selectedWavBpmResult_;
    std::optional<audio::WavFileFingerprint> requestedWavFingerprint_;
    std::uint64_t wavBpmRequestGeneration_ { 0 };
    audio::TransportMode previousTransportMode_ { audio::TransportMode::stopped };
    bool playheadFollowActive_ { false };
    std::optional<double> playbackPlayheadBeat_;
    core::MidiComparisonTolerance midiComparisonTolerance_;

    core::MainPanelLayoutState panelLayoutState_;
    core::MainLayoutGeometry lastLayout_;
    core::MainLayoutGeometry dragStartLayout_;
    core::MainPanelLayoutState dragStartPanelLayoutState_;
    juce::Point<int> dragStartPosition_;
    ActiveSplitter activeSplitter_ { ActiveSplitter::none };
};

}  // namespace dawhermes::ui
