#include "ui/MainComponent.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <utility>

#include "app/AppLogger.h"
#include "core/MidiNoteEditing.h"
#include "hermes/HermesCache.h"
#include "hermes/HermesCommandAvailability.h"
#include "hermes/HermesValidation.h"
#include "midi/MidiTrackExporter.h"
#include "ui/HermesDialogs.h"
#include "ui/MidiImportParser.h"

namespace dawhermes::ui {

namespace {

juce::String trackTypeLabel(core::TrackType type)
{
    switch (type) {
    case core::TrackType::audio:
        return "Audio";
    case core::TrackType::midi:
        return "MIDI";
    case core::TrackType::group:
        return "Group";
    default:
        return "Unknown";
    }
}

juce::String basenameForPath(const std::string& path)
{
    if (path.empty()) {
        return {};
    }

    const std::filesystem::path fsPath(path);
    return juce::String(fsPath.filename().string());
}

juce::String describeTrack(const core::Track& track)
{
    juce::String line = juce::String(track.name) + "  [" + trackTypeLabel(track.type) + "]";
    if (track.type == core::TrackType::audio) {
        if (track.audioSourcePath.empty()) {
            line << "  (No WAV source)";
        } else {
            line << "  (" << basenameForPath(track.audioSourcePath) << ")";
        }
    } else if (track.type == core::TrackType::midi) {
        line << "  (notes: " << static_cast<int>(track.midiNotes.size()) << ")";
    } else {
        line << "  (folder)";
    }

    return line;
}

int trackDepth(const core::ProjectModel& project, const core::Track& track)
{
    int depth = 0;
    auto parentTrackId = track.parentTrackId;
    while (parentTrackId != 0 && depth < 32) {
        const auto* parent = project.findTrackById(parentTrackId);
        if (parent == nullptr || parent->parentTrackId == parentTrackId) {
            break;
        }

        ++depth;
        parentTrackId = parent->parentTrackId;
    }

    return depth;
}

constexpr const char* kPanelLayoutSettingsKey = "layout.panelStateV1";
constexpr const char* kMidiSnapEnabledSettingsKey = "midiEditing.snapEnabled";

juce::String gridLabelForDenominator(int denominator)
{
    switch (denominator) {
    case 4:
        return "1/4";
    case 8:
        return "1/8";
    case 16:
        return "1/16";
    case 32:
        return "1/32";
    default:
        return "1/16";
    }
}

juce::String buildHermesGroupId()
{
    return juce::String("hermes-") + juce::Uuid().toString();
}

juce::String operationTitle(hermes::HermesOperationKind kind)
{
    switch (kind) {
    case hermes::HermesOperationKind::drumsExtraction:
        return "Hermes Drums";
    case hermes::HermesOperationKind::bassRepair:
        return "Hermes Bass";
    case hermes::HermesOperationKind::midiWavSynchronization:
        return "Hermes Synchronize";
    default:
        return "Hermes";
    }
}

void addHermesMenuItem(
    juce::PopupMenu& menu,
    int itemId,
    juce::String title,
    hermes::HermesCommand command,
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    bool forceDisabled = false,
    const juce::String& forceDisabledReason = {})
{
    const auto availability = hermes::getHermesCommandAvailability(command, project, selection);
    auto reason = hermes::describeAvailability(command, availability);
    bool enabled = availability == hermes::HermesCommandAvailability::enabled;

    if (forceDisabled) {
        enabled = false;
        if (forceDisabledReason.isNotEmpty()) {
            reason = forceDisabledReason.toStdString();
        }
    }

    if (!enabled && !reason.empty()) {
        title << " (" << reason << ")";
    }

    menu.addItem(itemId, title, enabled);
}

int sanitizeGridDenominator(int value)
{
    switch (value) {
    case 4:
    case 8:
    case 16:
    case 32:
        return value;
    default:
        return 16;
    }
}

juce::String noteNameForPitch(int pitch)
{
    static constexpr std::array<const char*, 12> names {
        "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"
    };

    const auto clamped = std::clamp(pitch, 0, 127);
    const auto pitchClass = clamped % 12;
    const auto octave = (clamped / 12) - 1;
    return juce::String(names[static_cast<std::size_t>(pitchClass)]) + juce::String(octave);
}

bool trackContainsNoteId(const core::Track& track, std::uint64_t noteId)
{
    return std::any_of(track.midiNotes.begin(), track.midiNotes.end(), [noteId](const core::MidiNote& note) {
        return note.id == noteId;
    });
}

class CreateMidiNoteCommand final : public core::ProjectEditCommand {
public:
    CreateMidiNoteCommand(core::ProjectModel& project, std::uint64_t trackId, core::MidiNote note)
        : project_(project), trackId_(trackId), note_(note)
    {
    }

    std::string label() const override { return "Create MIDI Note"; }

    bool undo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr || track->type != core::TrackType::midi) {
            return false;
        }

        return core::deleteSelectedNotes(track->midiNotes, { note_.id }) == 1;
    }

    bool redo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr || track->type != core::TrackType::midi || trackContainsNoteId(*track, note_.id)) {
            return false;
        }

        track->midiNotes.push_back(note_);
        core::sortMidiNotesByStart(track->midiNotes);
        return true;
    }

private:
    core::ProjectModel& project_;
    std::uint64_t trackId_ { 0 };
    core::MidiNote note_;
};

class DeleteMidiNotesCommand final : public core::ProjectEditCommand {
public:
    DeleteMidiNotesCommand(
        core::ProjectModel& project,
        std::uint64_t trackId,
        std::vector<std::uint64_t> noteIds,
        std::vector<core::MidiNote> deletedNotes)
        : project_(project),
          trackId_(trackId),
          noteIds_(std::move(noteIds)),
          deletedNotes_(std::move(deletedNotes))
    {
    }

    std::string label() const override { return deletedNotes_.size() == 1 ? "Delete MIDI Note" : "Delete MIDI Notes"; }

    bool undo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr || track->type != core::TrackType::midi) {
            return false;
        }

        for (const auto& note : deletedNotes_) {
            if (trackContainsNoteId(*track, note.id)) {
                return false;
            }
        }

        track->midiNotes.insert(track->midiNotes.end(), deletedNotes_.begin(), deletedNotes_.end());
        core::sortMidiNotesByStart(track->midiNotes);
        return true;
    }

    bool redo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr || track->type != core::TrackType::midi) {
            return false;
        }

        return core::deleteSelectedNotes(track->midiNotes, noteIds_) == deletedNotes_.size();
    }

private:
    core::ProjectModel& project_;
    std::uint64_t trackId_ { 0 };
    std::vector<std::uint64_t> noteIds_;
    std::vector<core::MidiNote> deletedNotes_;
};

class ReplaceMidiNotesCommand final : public core::ProjectEditCommand {
public:
    ReplaceMidiNotesCommand(
        core::ProjectModel& project,
        std::uint64_t trackId,
        std::string label,
        std::vector<core::MidiNote> beforeNotes,
        std::vector<core::MidiNote> afterNotes)
        : project_(project),
          trackId_(trackId),
          label_(std::move(label)),
          beforeNotes_(std::move(beforeNotes)),
          afterNotes_(std::move(afterNotes))
    {
    }

    std::string label() const override { return label_; }

    bool undo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr || track->type != core::TrackType::midi) {
            return false;
        }

        track->midiNotes = beforeNotes_;
        return true;
    }

    bool redo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr || track->type != core::TrackType::midi) {
            return false;
        }

        track->midiNotes = afterNotes_;
        return true;
    }

private:
    core::ProjectModel& project_;
    std::uint64_t trackId_ { 0 };
    std::string label_;
    std::vector<core::MidiNote> beforeNotes_;
    std::vector<core::MidiNote> afterNotes_;
};

}  // namespace

MainComponent::MainComponent(juce::ApplicationProperties& applicationProperties)
    : applicationProperties_(applicationProperties),
      hermesJobRunner_(std::make_unique<hermes::HermesJobRunner>()),
      panelLayoutState_(core::defaultMainPanelLayoutState()),
      dragStartPanelLayoutState_(core::defaultMainPanelLayoutState()),
      projectController_(projectModel_, selectionState_),
      menuBar_(this)
{
    setOpaque(true);
    setWantsKeyboardFocus(true);

    addAndMakeVisible(menuBar_);

    transportLabel_.setText(
        "Transport",
        juce::dontSendNotification);
    transportLabel_.setJustificationType(juce::Justification::centredLeft);
    transportLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2b323b));
    transportLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(transportLabel_);

    addAndMakeVisible(rewindButton_);
    addAndMakeVisible(playButton_);
    addAndMakeVisible(pauseButton_);
    addAndMakeVisible(stopButton_);
    addAndMakeVisible(fastForwardButton_);
    timeCounterLabel_.setText("00:00 / 00:00", juce::dontSendNotification);
    timeCounterLabel_.setJustificationType(juce::Justification::centred);
    timeCounterLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(timeCounterLabel_);
    bpmLabel_.setText("BPM --", juce::dontSendNotification);
    bpmLabel_.setJustificationType(juce::Justification::centred);
    bpmLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(bpmLabel_);
    addAndMakeVisible(panicButton_);
    volumeLabel_.setText("Vol", juce::dontSendNotification);
    volumeLabel_.setJustificationType(juce::Justification::centredRight);
    volumeLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(volumeLabel_);
    volumeSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    volumeSlider_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 46, 20);
    volumeSlider_.setRange(0.0, 100.0, 1.0);
    volumeSlider_.setValue(25.0, juce::dontSendNotification);
    volumeSlider_.setTextValueSuffix("%");
    addAndMakeVisible(volumeSlider_);

    tracksHeaderLabel_.setText("Tracks", juce::dontSendNotification);
    tracksHeaderLabel_.setJustificationType(juce::Justification::centredLeft);
    tracksHeaderLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff262b33));
    tracksHeaderLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(tracksHeaderLabel_);

    trackList_.setModel(this);
    trackList_.setRowHeight(30);
    trackList_.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1f2329));
    trackList_.setColour(juce::ListBox::textColourId, juce::Colours::white);
    addAndMakeVisible(trackList_);

    gridCombo_.addItem("1/4", 4);
    gridCombo_.addItem("1/8", 8);
    gridCombo_.addItem("1/16", 16);
    gridCombo_.addItem("1/32", 32);
    gridCombo_.setSelectedId(gridDenominator_, juce::dontSendNotification);
    addAndMakeVisible(gridCombo_);

    snapToggle_.setToggleState(snapEnabled_, juce::dontSendNotification);
    addAndMakeVisible(snapToggle_);

    addAndMakeVisible(horizontalZoomOutButton_);
    addAndMakeVisible(horizontalZoomInButton_);
    addAndMakeVisible(horizontalFitButton_);

    horizontalScrollSlider_.setSliderStyle(juce::Slider::LinearHorizontal);
    horizontalScrollSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    horizontalScrollSlider_.setRange(0.0, 64.0, 0.01);
    addAndMakeVisible(horizontalScrollSlider_);

    addAndMakeVisible(timeRulerView_);
    addAndMakeVisible(timelineView_);

    addAndMakeVisible(pitchZoomOutButton_);
    addAndMakeVisible(pitchZoomInButton_);
    addAndMakeVisible(pitchFitButton_);
    velocityLabel_.setText("Vel", juce::dontSendNotification);
    velocityLabel_.setJustificationType(juce::Justification::centredRight);
    velocityLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffc7ccd4));
    addAndMakeVisible(velocityLabel_);
    velocityEditor_.setInputRestrictions(3, "0123456789");
    velocityEditor_.setJustification(juce::Justification::centred);
    velocityEditor_.setEnabled(false);
    addAndMakeVisible(velocityEditor_);
    addAndMakeVisible(pianoKeyboardView_);
    addAndMakeVisible(pianoRollView_);

    pianoPitchScrollSlider_.setSliderStyle(juce::Slider::LinearVertical);
    pianoPitchScrollSlider_.setTextBoxStyle(juce::Slider::NoTextBox, false, 0, 0);
    pianoPitchScrollSlider_.setRange(12.0, 127.0, 1.0);
    addAndMakeVisible(pianoPitchScrollSlider_);

    addAndMakeVisible(midiComparisonLegend_);

    aiAssistantLabel_.setText("AI Assistant", juce::dontSendNotification);
    aiAssistantLabel_.setJustificationType(juce::Justification::centred);
    aiAssistantLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff242a31));
    aiAssistantLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(aiAssistantLabel_);

    statusLabel_.setText("No track selected.", juce::dontSendNotification);
    statusLabel_.setJustificationType(juce::Justification::centredLeft);
    statusLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff181b20));
    statusLabel_.setColour(juce::Label::textColourId, juce::Colour(0xffc7ccd4));
    addAndMakeVisible(statusLabel_);

    initializeVisualWorkspace();

    loadPanelLayoutState();
    loadMidiEditingSettings();
    loadComposerSettings();
    cleanupStaleHermesCacheOnStartup();
    updateVisualWorkspace();
    updateStatusForSelection();
    startTimerHz(30);
}

MainComponent::~MainComponent()
{
    stopTimer();
    wavBpmAnalysisService_.stop();
    midiAuditionEngine_.panic();

    if (hermesJobRunner_ != nullptr) {
        hermesJobRunner_->stop();
    }

    savePanelLayoutState();
    trackList_.setModel(nullptr);
}

void MainComponent::initializeVisualWorkspace()
{
    timelineViewportState_ = core::sanitizeTimelineViewportState(core::TimelineViewportState {});
    pitchViewportState_ = core::sanitizePitchViewportState(core::PitchViewportState {});

    resolvedTimelineInfo_.ticksPerQuarterNote = 960;
    resolvedTimelineInfo_.tempoMap = core::sanitizeTempoMap({});
    resolvedTimelineInfo_.timeSignatureMap = core::sanitizeTimeSignatureMap({});

    rewindButton_.onClick = [this]() { seekPlayback(-audio::kTransportSeekSeconds); };
    playButton_.onClick = [this]() { startSelectedMidiPlayback(); };
    pauseButton_.onClick = [this]() { pausePlayback(); };
    stopButton_.onClick = [this]() { stopMidiPlayback(); };
    fastForwardButton_.onClick = [this]() { seekPlayback(audio::kTransportSeekSeconds); };
    panicButton_.onClick = [this]() { panicMidiPlayback(); };
    volumeSlider_.onValueChange = [this]() {
        midiAuditionEngine_.setVolume(static_cast<float>(volumeSlider_.getValue() / 100.0));
    };
    midiAuditionEngine_.setVolume(static_cast<float>(volumeSlider_.getValue() / 100.0));

    gridCombo_.onChange = [this]() {
        const auto selected = sanitizeGridDenominator(gridCombo_.getSelectedId());
        if (selected == gridDenominator_) {
            return;
        }

        gridDenominator_ = selected;
        updateVisualWorkspace();
    };

    snapToggle_.onClick = [this]() {
        snapEnabled_ = snapToggle_.getToggleState();
        saveMidiEditingSettings();
        pianoRollView_.setSnapEnabled(snapEnabled_);
        statusLabel_.setText(snapEnabled_ ? "Snap: On" : "Snap: Off", juce::dontSendNotification);
    };

    horizontalZoomOutButton_.onClick = [this]() { applyHorizontalZoom(0.80); };
    horizontalZoomInButton_.onClick = [this]() { applyHorizontalZoom(1.25); };
    horizontalFitButton_.onClick = [this]() { fitHorizontalToProject(); };

    horizontalScrollSlider_.onValueChange = [this]() {
        if (suppressViewportControlCallbacks_) {
            return;
        }

        timelineViewportState_.startBeat = horizontalScrollSlider_.getValue();
        timelineViewportState_ = core::sanitizeTimelineViewportState(timelineViewportState_);
        updateVisualWorkspace();
    };

    pitchZoomOutButton_.onClick = [this]() { applyPitchZoom(0.85); };
    pitchZoomInButton_.onClick = [this]() { applyPitchZoom(1.20); };
    pitchFitButton_.onClick = [this]() { fitPitchToActiveNotes(); };

    velocityEditor_.onReturnKey = [this]() {
        applyVelocityEditorValue();
    };
    velocityEditor_.onFocusLost = [this]() {
        applyVelocityEditorValue();
    };

    pianoPitchScrollSlider_.onValueChange = [this]() {
        if (suppressViewportControlCallbacks_) {
            return;
        }

        pitchViewportState_.highestVisiblePitch = pianoPitchScrollSlider_.getValue();
        pitchViewportState_ = core::sanitizePitchViewportState(pitchViewportState_);
        updateVisualWorkspace();
    };

    timelineView_.setTrackRowHeight(trackList_.getRowHeight());
    timelineView_.setGridDenominator(gridDenominator_);
    timeRulerView_.setGridDenominator(gridDenominator_);
    pianoRollView_.setGridDenominator(gridDenominator_);
    pianoRollView_.setSnapEnabled(snapEnabled_);
    midiComparisonLegend_.setComparisonEnabled(false);

    pianoRollView_.onPrimaryNoteClicked = [this](std::uint64_t noteId, bool additive) {
        selectMidiNote(noteId, additive);
    };
    pianoRollView_.onEmptySpaceClicked = [this](bool additive) {
        clearMidiNoteSelectionFromEmptyClick(additive);
    };
    pianoRollView_.onMarqueeSelectionFinished = [this](std::vector<std::uint64_t> noteIds, bool additive) {
        applyMidiNoteMarqueeSelection(std::move(noteIds), additive);
    };
    pianoRollView_.onCreateNoteRequested = [this](double beat, int pitch) {
        createMidiNoteAt(beat, pitch);
    };
    pianoRollView_.onDeleteRequested = [this]() {
        deleteSelectedMidiNotes(true);
    };
    pianoRollView_.onMoveNotesRequested = [this](
        std::vector<std::uint64_t> selectedNoteIds,
        double deltaBeats,
        int deltaSemitones) {
        moveSelectedMidiNotes(std::move(selectedNoteIds), deltaBeats, deltaSemitones, "Moved MIDI notes", snapEnabled_);
    };
    pianoRollView_.onResizeNotesRequested = [this](
        std::uint64_t anchorNoteId,
        std::vector<std::uint64_t> selectedNoteIds,
        double requestedAnchorEndBeat) {
        resizeSelectedMidiNotes(anchorNoteId, std::move(selectedNoteIds), requestedAnchorEndBeat);
    };
    pianoRollView_.onNudgeRequested = [this](int deltaSemitones, double deltaBeats) {
        nudgeSelectedMidiNotes(deltaSemitones, deltaBeats);
    };
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15181d));

    const auto paintSplitter = [this, &g](const core::IntRect& rect, ActiveSplitter splitter) {
        if (rect.width <= 0 || rect.height <= 0) {
            return;
        }

        const auto isActive = activeSplitter_ == splitter;
        g.setColour(isActive ? juce::Colour(0xff5f7b98) : juce::Colour(0xff2b323a));
        g.fillRect(rect.x, rect.y, rect.width, rect.height);
    };

    paintSplitter(lastLayout_.leftVerticalSplitter, ActiveSplitter::leftVertical);
    paintSplitter(lastLayout_.rightVerticalSplitter, ActiveSplitter::rightVertical);
    paintSplitter(lastLayout_.horizontalSplitter, ActiveSplitter::horizontal);
}

void MainComponent::resized()
{
    lastLayout_ = core::computeMainLayoutGeometry(getWidth(), getHeight(), panelLayoutState_);
    const auto& layout = lastLayout_;

    menuBar_.setBounds(layout.menuBar.x, layout.menuBar.y, layout.menuBar.width, layout.menuBar.height);
    transportLabel_.setBounds(
        layout.transportBar.x,
        layout.transportBar.y,
        layout.transportBar.width,
        layout.transportBar.height);
    auto transportControls = juce::Rectangle<int>(
        layout.transportBar.x,
        layout.transportBar.y,
        layout.transportBar.width,
        layout.transportBar.height).reduced(6, 3);
    transportLabel_.setBounds(transportControls.removeFromLeft(std::min(92, transportControls.getWidth())));
    transportControls.removeFromLeft(5);
    rewindButton_.setBounds(transportControls.removeFromLeft(std::min(36, transportControls.getWidth())));
    transportControls.removeFromLeft(3);
    playButton_.setBounds(transportControls.removeFromLeft(std::min(52, transportControls.getWidth())));
    transportControls.removeFromLeft(3);
    pauseButton_.setBounds(transportControls.removeFromLeft(std::min(58, transportControls.getWidth())));
    transportControls.removeFromLeft(3);
    stopButton_.setBounds(transportControls.removeFromLeft(std::min(52, transportControls.getWidth())));
    transportControls.removeFromLeft(3);
    fastForwardButton_.setBounds(transportControls.removeFromLeft(std::min(36, transportControls.getWidth())));
    transportControls.removeFromLeft(6);
    timeCounterLabel_.setBounds(transportControls.removeFromLeft(std::min(168, transportControls.getWidth())));
    transportControls.removeFromLeft(5);
    bpmLabel_.setBounds(transportControls.removeFromLeft(std::min(102, transportControls.getWidth())));
    transportControls.removeFromLeft(5);
    panicButton_.setBounds(transportControls.removeFromLeft(std::min(58, transportControls.getWidth())));
    transportControls.removeFromLeft(6);
    volumeLabel_.setBounds(transportControls.removeFromLeft(std::min(28, transportControls.getWidth())));
    volumeSlider_.setBounds(transportControls.removeFromLeft(std::min(150, transportControls.getWidth())));
    tracksHeaderLabel_.setBounds(
        layout.tracksHeader.x,
        layout.tracksHeader.y,
        layout.tracksHeader.width,
        layout.tracksHeader.height);
    trackList_.setBounds(layout.trackList.x, layout.trackList.y, layout.trackList.width, layout.trackList.height);

    auto timelineBounds = juce::Rectangle<int>(layout.timeline.x, layout.timeline.y, layout.timeline.width, layout.timeline.height);
    const auto headerHeight = std::max(24, layout.trackList.y - layout.timeline.y);
    auto timelineHeader = timelineBounds.removeFromTop(std::min(headerHeight, timelineBounds.getHeight()));
    const auto controlsWidth = std::min(410, timelineHeader.getWidth());
    auto controlsArea = timelineHeader.removeFromLeft(controlsWidth).reduced(2);

    gridCombo_.setBounds(controlsArea.removeFromLeft(58));
    controlsArea.removeFromLeft(4);
    snapToggle_.setBounds(controlsArea.removeFromLeft(58));
    controlsArea.removeFromLeft(4);
    horizontalZoomOutButton_.setBounds(controlsArea.removeFromLeft(28));
    controlsArea.removeFromLeft(2);
    horizontalZoomInButton_.setBounds(controlsArea.removeFromLeft(28));
    controlsArea.removeFromLeft(2);
    horizontalFitButton_.setBounds(controlsArea.removeFromLeft(42));
    controlsArea.removeFromLeft(6);
    horizontalScrollSlider_.setBounds(controlsArea);

    timeRulerView_.setBounds(timelineHeader.reduced(2));

    const auto timelineContentY = std::max(layout.trackList.y, timelineBounds.getY());
    const auto timelineContentHeight = std::max(0, timelineBounds.getBottom() - timelineContentY);
    timelineView_.setBounds(layout.timeline.x, timelineContentY, layout.timeline.width, timelineContentHeight);

    aiAssistantLabel_.setBounds(
        layout.aiAssistant.x,
        layout.aiAssistant.y,
        layout.aiAssistant.width,
        layout.aiAssistant.height);

    auto midiBounds = juce::Rectangle<int>(layout.midiEditor.x, layout.midiEditor.y, layout.midiEditor.width, layout.midiEditor.height);
    auto midiControls = midiBounds.removeFromTop(std::min(26, midiBounds.getHeight()));
    auto midiLegend = midiBounds.removeFromBottom(std::min(34, midiBounds.getHeight()));

    auto pitchControls = midiControls.reduced(2);
    pitchZoomOutButton_.setBounds(pitchControls.removeFromLeft(60));
    pitchControls.removeFromLeft(4);
    pitchZoomInButton_.setBounds(pitchControls.removeFromLeft(60));
    pitchControls.removeFromLeft(4);
    pitchFitButton_.setBounds(pitchControls.removeFromLeft(84));
    pitchControls.removeFromLeft(8);
    velocityLabel_.setBounds(pitchControls.removeFromLeft(28));
    pitchControls.removeFromLeft(4);
    velocityEditor_.setBounds(pitchControls.removeFromLeft(46));

    midiComparisonLegend_.setBounds(midiLegend);

    auto midiContent = midiBounds;
    const auto keyboardWidth = std::min(74, std::max(46, midiContent.getWidth() / 8));
    const auto scrollWidth = 16;

    pianoKeyboardView_.setBounds(midiContent.removeFromLeft(keyboardWidth));
    midiContent.removeFromRight(2);
    pianoPitchScrollSlider_.setBounds(midiContent.removeFromRight(std::min(scrollWidth, midiContent.getWidth())));
    pianoRollView_.setBounds(midiContent);

    statusLabel_.setBounds(layout.statusBar.x, layout.statusBar.y, layout.statusBar.width, layout.statusBar.height);

    updateVisualWorkspace();
}

void MainComponent::mouseMove(const juce::MouseEvent& event)
{
    if (activeSplitter_ == ActiveSplitter::none) {
        updateCursorForSplitters(event.getPosition());
    }
}

void MainComponent::mouseExit(const juce::MouseEvent&)
{
    if (activeSplitter_ == ActiveSplitter::none) {
        setMouseCursor(juce::MouseCursor::NormalCursor);
    }
}

void MainComponent::mouseDown(const juce::MouseEvent& event)
{
    activeSplitter_ = splitterAt(event.getPosition());
    if (activeSplitter_ == ActiveSplitter::none) {
        return;
    }

    dragStartPosition_ = event.getPosition();
    dragStartLayout_ = lastLayout_;
    dragStartPanelLayoutState_ = panelLayoutState_;
}

void MainComponent::mouseDrag(const juce::MouseEvent& event)
{
    if (activeSplitter_ == ActiveSplitter::none) {
        return;
    }

    applySplitterDrag(event.getPosition());
}

void MainComponent::mouseUp(const juce::MouseEvent& event)
{
    if (activeSplitter_ == ActiveSplitter::none) {
        return;
    }

    savePanelLayoutState();
    activeSplitter_ = ActiveSplitter::none;
    updateCursorForSplitters(event.getPosition());
}

bool MainComponent::keyPressed(const juce::KeyPress& key)
{
    if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey) {
        deleteSelectedMidiNotes(true);
        return true;
    }

    if (key == juce::KeyPress::leftKey || key == juce::KeyPress::rightKey
        || key == juce::KeyPress::upKey || key == juce::KeyPress::downKey) {
        const auto gridStep = core::gridStepBeats(gridDenominator_);
        if (key == juce::KeyPress::leftKey) {
            nudgeSelectedMidiNotes(0, -gridStep);
        } else if (key == juce::KeyPress::rightKey) {
            nudgeSelectedMidiNotes(0, gridStep);
        } else {
            const auto octave = key.getModifiers().isShiftDown() ? 12 : 1;
            nudgeSelectedMidiNotes(key == juce::KeyPress::upKey ? octave : -octave, 0.0);
        }
        return true;
    }

    return false;
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "View", "Track", "Tools", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    switch (topLevelMenuIndex) {
    case 0:
        menu.addItem(commandNewProject, "New Project");
        menu.addItem(commandImportMidiTrack, "Import MIDI as Track...");
        menu.addItem(commandExportSelectedMidiTrack, "Export Selected MIDI Track...", canExportSelectedMidiTrack());
        menu.addSeparator();
        menu.addItem(commandExit, "Exit");
        break;
    case 1:
        menu.addItem(commandUndo, "Undo", canUndo());
        menu.addItem(commandRedo, "Redo", canRedo());
        menu.addSeparator();
        menu.addItem(commandDeleteSelectedNotes, "Delete Selected Notes", canDeleteSelectedMidiNotes());
        menu.addItem(commandQuantizeSelectedNotes, "Quantize Selected Notes to Grid", canEditSelectedMidiNotes());
        break;
    case 2:
        menu.addItem(commandResetPanelLayout, "Reset Panel Layout");
        menu.addItem(
            commandToggleMidiComparison,
            "Compare Selected MIDI Tracks",
            selectedMidiComparisonPair().has_value(),
            midiComparisonEnabled_);
        break;
    case 3:
        menu.addItem(commandAddAudioTrack, "Add Audio Track");
        menu.addItem(commandAddMidiTrack, "Add MIDI Track");
        menu.addItem(commandAssignAudioFile, "Assign WAV Source to Selected Audio Track...");
        menu.addSeparator();
        menu.addItem(commandDeleteSelectedTrack, "Delete Selected Track", projectController_.canDeleteSelectedTrack());
        break;
    case 4:
        menu.addSubMenu("Hermes", buildHermesMenu());
        menu.addSeparator();
        menu.addItem(commandClearHermesCache, "Clear Hermes Cache");
        menu.addSeparator();
        menu.addItem(commandComposerAssistantSettings, "Composer Assistant Connector Settings...");
        menu.addItem(commandComposerAssistantProbe, "Test Composer Assistant Connection");
        break;
    case 5:
        menu.addItem(commandAbout, "About DAWHermes");
        break;
    default:
        break;
    }

    return menu;
}

void MainComponent::menuItemSelected(int menuItemID, int)
{
    if (menuItemID <= 0) {
        return;
    }

    executeCommand(menuItemID);
}

int MainComponent::getNumRows()
{
    return static_cast<int>(projectModel_.tracks().size());
}

void MainComponent::paintListBoxItem(
    int rowNumber,
    juce::Graphics& g,
    int width,
    int height,
    bool)
{
    if (rowNumber < 0 || rowNumber >= static_cast<int>(projectModel_.tracks().size())) {
        return;
    }

    const auto& track = projectModel_.tracks().at(static_cast<std::size_t>(rowNumber));
    const bool selected = selectionState_.isSelected(track.id);
    const auto depth = trackDepth(projectModel_, track);
    const auto indentation = depth * 16;

    g.fillAll(selected ? juce::Colour(0xff2f5f9a)
                       : ((rowNumber % 2 == 0) ? juce::Colour(0xff252a31) : juce::Colour(0xff21262d)));

    g.setColour(juce::Colours::white);
    g.drawText(
        describeTrack(track),
        8 + indentation,
        0,
        std::max(0, width - 16 - indentation),
        height,
        juce::Justification::centredLeft,
        true);
}

void MainComponent::listBoxItemClicked(int row, const juce::MouseEvent& event)
{
    if (row < 0 || row >= static_cast<int>(projectModel_.tracks().size())) {
        return;
    }

    const auto& track = projectModel_.tracks().at(static_cast<std::size_t>(row));
    const bool additiveSelection = event.mods.isCtrlDown() || event.mods.isCommandDown();

    if (event.mods.isPopupMenu()) {
        if (!selectionState_.isSelected(track.id)) {
            projectController_.selectTrack(track.id);
            refreshTrackView();
            updateStatusForSelection();
        }

        showTrackContextMenu();
        return;
    }

    if (additiveSelection) {
        projectController_.toggleTrackSelection(track.id);
    } else {
        projectController_.selectTrack(track.id);
    }

    refreshTrackView();
    updateStatusForSelection();
}

void MainComponent::executeCommand(int commandId)
{
    try {
        switch (commandId) {
        case commandNewProject:
            resetProject();
            break;
        case commandExit:
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
            break;
        case commandAddAudioTrack:
            addAudioTrack();
            break;
        case commandAddMidiTrack:
            addMidiTrack();
            break;
        case commandImportMidiTrack:
            importMidiTrack();
            break;
        case commandExportSelectedMidiTrack:
            exportSelectedMidiTrack();
            break;
        case commandAssignAudioFile:
            assignAudioFileToSelectedTrack();
            break;
        case commandDeleteSelectedTrack:
            deleteSelectedTrack();
            break;
        case commandDeleteSelectedNotes:
            deleteSelectedMidiNotes(false);
            break;
        case commandQuantizeSelectedNotes:
            quantizeSelectedMidiNotesToGrid();
            break;
        case commandUndo:
            runUndo();
            break;
        case commandRedo:
            runRedo();
            break;
        case commandResetPanelLayout:
            runResetPanelLayout();
            break;
        case commandToggleMidiComparison:
            runToggleMidiComparison();
            break;
        case commandHermesDrumsMakeMidi:
            runHermesDrumsMakeMidi();
            break;
        case commandHermesDrumMapping:
            runHermesDrumMapping();
            break;
        case commandHermesBassRepair:
            runHermesBassRepair();
            break;
        case commandHermesSynchronize:
            runHermesSynchronize();
            break;
        case commandHermesSetFixBpm:
            runHermesSetFixBpm();
            break;
        case commandClearHermesCache:
            runClearHermesCache();
            break;
        case commandComposerAssistantSettings:
            runComposerAssistantSettings();
            break;
        case commandComposerAssistantProbe:
            runComposerAssistantProbe();
            break;
        case commandAbout:
            showAbout();
            break;
        default:
            break;
        }
    } catch (const std::exception& ex) {
        app::AppLogger::log("Unexpected exception: " + juce::String(ex.what()));
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::WarningIcon,
            "DAWHermes",
            "An unexpected error occurred.");
    } catch (...) {
        app::AppLogger::log("Unexpected exception: unknown");
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::WarningIcon,
            "DAWHermes",
            "An unexpected error occurred.");
    }
}

MainComponent::ActiveSplitter MainComponent::splitterAt(juce::Point<int> position) const
{
    const auto point = juce::Point<float>(static_cast<float>(position.x), static_cast<float>(position.y));

    const auto inRect = [point](const core::IntRect& rect) {
        if (rect.width <= 0 || rect.height <= 0) {
            return false;
        }

        return juce::Rectangle<float>(
                   static_cast<float>(rect.x),
                   static_cast<float>(rect.y),
                   static_cast<float>(rect.width),
                   static_cast<float>(rect.height))
            .contains(point);
    };

    if (inRect(lastLayout_.leftVerticalSplitter)) {
        return ActiveSplitter::leftVertical;
    }

    if (inRect(lastLayout_.rightVerticalSplitter)) {
        return ActiveSplitter::rightVertical;
    }

    if (inRect(lastLayout_.horizontalSplitter)) {
        return ActiveSplitter::horizontal;
    }

    return ActiveSplitter::none;
}

void MainComponent::updateCursorForSplitters(juce::Point<int> position)
{
    const auto splitter = splitterAt(position);
    switch (splitter) {
    case ActiveSplitter::leftVertical:
    case ActiveSplitter::rightVertical:
        setMouseCursor(juce::MouseCursor::LeftRightResizeCursor);
        break;
    case ActiveSplitter::horizontal:
        setMouseCursor(juce::MouseCursor::UpDownResizeCursor);
        break;
    case ActiveSplitter::none:
    default:
        setMouseCursor(juce::MouseCursor::NormalCursor);
        break;
    }
}

void MainComponent::applySplitterDrag(juce::Point<int> position)
{
    auto updated = dragStartPanelLayoutState_;

    const auto totalTopWidth =
        dragStartLayout_.trackList.width + dragStartLayout_.timeline.width + dragStartLayout_.aiAssistant.width;
    const auto topRowHeight = std::max(
        dragStartLayout_.trackList.height,
        std::max(dragStartLayout_.timeline.height, dragStartLayout_.aiAssistant.height));
    const auto totalHeight = topRowHeight + dragStartLayout_.midiEditor.height;

    if ((activeSplitter_ == ActiveSplitter::leftVertical || activeSplitter_ == ActiveSplitter::rightVertical)
        && totalTopWidth > 0) {
        const auto deltaX = position.x - dragStartPosition_.x;
        if (activeSplitter_ == ActiveSplitter::leftVertical) {
            const auto leftWidth = dragStartLayout_.trackList.width + deltaX;
            updated.leftColumnRatio = static_cast<double>(leftWidth) / static_cast<double>(totalTopWidth);
        } else {
            const auto rightWidth = dragStartLayout_.aiAssistant.width - deltaX;
            updated.rightColumnRatio = static_cast<double>(rightWidth) / static_cast<double>(totalTopWidth);
        }
    }

    if (activeSplitter_ == ActiveSplitter::horizontal && totalHeight > 0) {
        const auto deltaY = position.y - dragStartPosition_.y;
        const auto updatedTopHeight = topRowHeight + deltaY;
        updated.topRowRatio = static_cast<double>(updatedTopHeight) / static_cast<double>(totalHeight);
    }

    panelLayoutState_ = core::sanitizeMainPanelLayoutState(updated);
    resized();
    repaint();
}

void MainComponent::loadPanelLayoutState()
{
    panelLayoutState_ = core::defaultMainPanelLayoutState();

    if (auto* settings = applicationProperties_.getUserSettings()) {
        const auto serialized = settings->getValue(kPanelLayoutSettingsKey);
        core::MainPanelLayoutState restored;
        if (serialized.isNotEmpty() && core::deserializeMainPanelLayoutState(serialized.toStdString(), restored)) {
            panelLayoutState_ = restored;
        }
    }

    panelLayoutState_ = core::sanitizeMainPanelLayoutState(panelLayoutState_);
}

void MainComponent::savePanelLayoutState() const
{
    if (auto* settings = applicationProperties_.getUserSettings()) {
        settings->setValue(
            kPanelLayoutSettingsKey,
            juce::String(core::serializeMainPanelLayoutState(panelLayoutState_)));
        settings->saveIfNeeded();
    }
}

void MainComponent::loadMidiEditingSettings()
{
    if (auto* settings = applicationProperties_.getUserSettings()) {
        snapEnabled_ = settings->getBoolValue(kMidiSnapEnabledSettingsKey, snapEnabled_);
    }

    snapToggle_.setToggleState(snapEnabled_, juce::dontSendNotification);
    pianoRollView_.setSnapEnabled(snapEnabled_);
}

void MainComponent::saveMidiEditingSettings() const
{
    if (auto* settings = applicationProperties_.getUserSettings()) {
        settings->setValue(kMidiSnapEnabledSettingsKey, snapEnabled_);
        settings->saveIfNeeded();
    }
}

bool MainComponent::isHermesJobRunning() const
{
    return hermesJobRunner_ != nullptr && hermesJobRunner_->isBusy();
}

juce::String MainComponent::describeActiveOperation() const
{
    if (!activeOperation_.has_value()) {
        return "Hermes processing";
    }

    return operationTitle(activeOperation_.value());
}

void MainComponent::cleanupStaleHermesCacheOnStartup()
{
    std::string error;
    const auto removedCount = hermes::cleanupStaleHermesCache(std::chrono::hours(72), error);
    if (!error.empty()) {
        app::AppLogger::log("Hermes cache cleanup warning: " + juce::String(error));
        return;
    }

    if (removedCount > 0) {
        app::AppLogger::log(
            "Hermes cache cleanup removed " + juce::String(static_cast<int>(removedCount)) + " stale job folder(s).");
    }
}

juce::PopupMenu MainComponent::buildHermesMenu() const
{
    juce::PopupMenu hermesMenu;

    const bool busy = isHermesJobRunning();
    const juce::String busyReason = "Hermes worker is busy";

    juce::PopupMenu drumsSubMenu;
    addHermesMenuItem(
        drumsSubMenu,
        commandHermesDrumsMakeMidi,
        "Make MIDI from WAV...",
        hermes::HermesCommand::drumsMakeMidiFromWav,
        projectModel_,
        selectionState_,
        busy,
        busyReason);

    addHermesMenuItem(
        drumsSubMenu,
        commandHermesDrumMapping,
        "Drum Mapping...",
        hermes::HermesCommand::drumsMapping,
        projectModel_,
        selectionState_);

    juce::PopupMenu bassSubMenu;
    addHermesMenuItem(
        bassSubMenu,
        commandHermesBassRepair,
        "Repair MIDI against WAV...",
        hermes::HermesCommand::bassMakeRepairMidiFromWav,
        projectModel_,
        selectionState_,
        busy,
        busyReason);

    hermesMenu.addSubMenu("Drums", drumsSubMenu);
    hermesMenu.addSubMenu("Bass", bassSubMenu);

    addHermesMenuItem(
        hermesMenu,
        commandHermesSynchronize,
        "Synchronize MIDI with WAV...",
        hermes::HermesCommand::synchronizeMidiWithWav,
        projectModel_,
        selectionState_,
        busy,
        busyReason);

    addHermesMenuItem(
        hermesMenu,
        commandHermesSetFixBpm,
        "Set / Fix BPM...",
        hermes::HermesCommand::setFixBpm,
        projectModel_,
        selectionState_,
        busy,
        busyReason);

    return hermesMenu;
}

juce::PopupMenu MainComponent::buildTrackContextMenu() const
{
    juce::PopupMenu menu;
    const auto track = selectedTrack();
    const bool isSelectedAudio = track.has_value() && track->type == core::TrackType::audio;
    menu.addItem(commandAssignAudioFile, "Assign WAV Source...", isSelectedAudio);
    menu.addItem(commandImportMidiTrack, "Import MIDI as Track...");
    menu.addSeparator();
    menu.addItem(commandDeleteSelectedTrack, "Delete Selected Track", projectController_.canDeleteSelectedTrack());
    menu.addSeparator();
    menu.addSubMenu("Hermes", buildHermesMenu());
    return menu;
}

std::optional<core::Track> MainComponent::selectedTrack() const
{
    const auto selectedId = projectController_.selectedTrackId();
    if (!selectedId.has_value()) {
        return std::nullopt;
    }

    const auto* track = projectModel_.findTrackById(selectedId.value());
    if (track == nullptr) {
        return std::nullopt;
    }

    return *track;
}

std::vector<core::Track> MainComponent::selectedTracks() const
{
    std::vector<core::Track> tracks;
    for (const auto trackId : projectController_.selectedTrackIds()) {
        const auto* track = projectModel_.findTrackById(trackId);
        if (track != nullptr) {
            tracks.push_back(*track);
        }
    }

    return tracks;
}

std::optional<hermes::HermesTrackContext> MainComponent::selectedTrackContext() const
{
    const auto track = selectedTrack();
    if (!track.has_value()) {
        return std::nullopt;
    }

    return hermes::HermesTrackContext { track->id, track->name, track->type, track->audioSourcePath };
}

std::optional<hermes::HermesAudioMidiPairContext> MainComponent::selectedAudioMidiPairContext() const
{
    auto selected = selectedTracks();
    if (selected.size() != 2) {
        return std::nullopt;
    }

    const core::Track* audioTrack = nullptr;
    const core::Track* midiTrack = nullptr;

    for (const auto& track : selected) {
        if (track.type == core::TrackType::audio && audioTrack == nullptr) {
            audioTrack = &track;
        } else if (track.type == core::TrackType::midi && midiTrack == nullptr) {
            midiTrack = &track;
        }
    }

    std::error_code ec;
    const auto audioPath = audioTrack == nullptr ? std::filesystem::path {} : std::filesystem::path(audioTrack->audioSourcePath);

    if (audioTrack == nullptr
        || midiTrack == nullptr
        || audioTrack->audioSourcePath.empty()
        || !std::filesystem::exists(audioPath, ec)
        || !std::filesystem::is_regular_file(audioPath, ec)
        || midiTrack->midiNotes.empty()) {
        return std::nullopt;
    }

    core::MidiSourceMetadata metadata;
    if (midiTrack->midiSourceMetadata.has_value()) {
        metadata = midiTrack->midiSourceMetadata.value();
    }

    if (metadata.sourceTrackName.empty()) {
        metadata.sourceTrackName = midiTrack->name;
    }

    if (metadata.ticksPerQuarterNote <= 0) {
        metadata.ticksPerQuarterNote = 960;
    }

    if (metadata.tempoMap.empty()) {
        metadata.tempoMap.push_back(core::MidiTempoEvent {});
    }

    if (metadata.timeSignatureMap.empty()) {
        metadata.timeSignatureMap.push_back(core::MidiTimeSignatureEvent {});
    }

    if (metadata.origin == core::MidiTrackOrigin::unknown) {
        metadata.origin = core::MidiTrackOrigin::imported;
    }

    hermes::HermesAudioMidiPairContext context;
    context.audioTrack = {
        audioTrack->id,
        audioTrack->name,
        audioTrack->type,
        audioTrack->audioSourcePath,
    };
    context.midiTrack = {
        midiTrack->id,
        midiTrack->name,
        midiTrack->type,
        {},
    };
    context.midiNotes = midiTrack->midiNotes;
    context.midiSourceMetadata = std::move(metadata);
    return context;
}

void MainComponent::showTrackContextMenu()
{
    const auto selection = selectedTrack();
    if (!selection.has_value()) {
        return;
    }

    const auto result = buildTrackContextMenu().show();
    if (result > 0) {
        executeCommand(result);
    }
}

void MainComponent::refreshTrackView()
{
    trackList_.updateContent();
    trackList_.repaint();
    updateVisualWorkspace();
    menuBar_.repaint();
}

double MainComponent::estimateProjectDurationBeats() const
{
    double maxBeat = 16.0;

    for (const auto& track : projectModel_.tracks()) {
        if (track.type != core::TrackType::midi) {
            continue;
        }

        for (const auto& note : track.midiNotes) {
            maxBeat = std::max(maxBeat, note.startBeat + std::max(1.0 / 960.0, note.durationBeats));
        }

        if (track.midiSourceMetadata.has_value()) {
            maxBeat = std::max(maxBeat, track.midiSourceMetadata->approximateDurationBeats);
        }
    }

    return std::max(4.0, maxBeat);
}

std::optional<std::pair<core::Track, core::Track>> MainComponent::selectedMidiComparisonPair() const
{
    const auto selectedIds = projectController_.selectedTrackIds();
    if (selectedIds.size() != 2) {
        return std::nullopt;
    }

    const auto* first = projectModel_.findTrackById(selectedIds[0]);
    const auto* second = projectModel_.findTrackById(selectedIds[1]);
    if (first == nullptr || second == nullptr) {
        return std::nullopt;
    }

    if (first->type != core::TrackType::midi || second->type != core::TrackType::midi) {
        return std::nullopt;
    }

    return std::make_pair(*first, *second);
}

std::optional<core::Track> MainComponent::activeMidiTrackForPianoRoll() const
{
    if (const auto pair = selectedMidiComparisonPair(); pair.has_value() && midiComparisonEnabled_) {
        return pair->first;
    }

    if (const auto selected = selectedTrack(); selected.has_value() && selected->type == core::TrackType::midi) {
        return selected;
    }

    for (const auto& track : projectModel_.tracks()) {
        if (track.type == core::TrackType::midi) {
            return track;
        }
    }

    return std::nullopt;
}

void MainComponent::updateHorizontalScrollRange()
{
    const auto projectEndBeat = estimateHorizontalContentEndBeat();
    const auto maxStartBeat = std::max(0.0, projectEndBeat - timelineViewportState_.visibleBeats);

    timelineViewportState_.startBeat = std::clamp(timelineViewportState_.startBeat, 0.0, maxStartBeat);

    suppressViewportControlCallbacks_ = true;
    horizontalScrollSlider_.setRange(0.0, maxStartBeat, 0.01);
    horizontalScrollSlider_.setValue(timelineViewportState_.startBeat, juce::dontSendNotification);
    pianoPitchScrollSlider_.setValue(pitchViewportState_.highestVisiblePitch, juce::dontSendNotification);
    gridCombo_.setSelectedId(gridDenominator_, juce::dontSendNotification);
    suppressViewportControlCallbacks_ = false;
}

double MainComponent::estimateHorizontalContentEndBeat() const
{
    auto endBeat = estimateProjectDurationBeats();
    const auto duration = midiAuditionEngine_.totalDurationSeconds();
    if (duration <= 0.0) {
        return endBeat;
    }

    if (const auto snapshot = midiAuditionEngine_.playbackSnapshot();
        snapshot != nullptr) {
        endBeat = std::max(
            endBeat,
            audio::selectionPlayheadBeat(duration, *snapshot));
    } else {
        endBeat = std::max(
            endBeat,
            audio::midiSecondsToBeat(
                duration,
                transportSelectionSummary_.tempoMap));
    }
    return endBeat;
}

void MainComponent::applyHorizontalZoom(double zoomFactor)
{
    const auto pivotBeat = timelineViewportState_.startBeat + (timelineViewportState_.visibleBeats * 0.5);
    timelineViewportState_ = core::zoomTimelineViewport(
        timelineViewportState_,
        pivotBeat,
        zoomFactor,
        estimateHorizontalContentEndBeat());
    updateVisualWorkspace();
}

void MainComponent::fitHorizontalToProject()
{
    double minBeat = 0.0;
    double maxBeat = 0.0;
    bool hasNotes = false;

    for (const auto& track : projectModel_.tracks()) {
        if (track.type != core::TrackType::midi) {
            continue;
        }

        for (const auto& note : track.midiNotes) {
            if (!hasNotes) {
                minBeat = note.startBeat;
                maxBeat = note.startBeat + note.durationBeats;
                hasNotes = true;
            } else {
                minBeat = std::min(minBeat, note.startBeat);
                maxBeat = std::max(maxBeat, note.startBeat + note.durationBeats);
            }
        }
    }

    if (!hasNotes) {
        timelineViewportState_ = core::fitTimelineViewport(0.0, estimateProjectDurationBeats(), 0.5, 1.0, 512.0);
    } else {
        timelineViewportState_ = core::fitTimelineViewport(minBeat, maxBeat, 0.5, 1.0, 512.0);
    }

    updateVisualWorkspace();
}

void MainComponent::applyPitchZoom(double zoomFactor)
{
    const auto pivotPitch = pitchViewportState_.highestVisiblePitch - (pitchViewportState_.visiblePitchSpan * 0.5);
    pitchViewportState_ = core::zoomPitchViewport(pitchViewportState_, pivotPitch, zoomFactor);
    updateVisualWorkspace();
}

void MainComponent::fitPitchToActiveNotes()
{
    const auto activeTrack = activeMidiTrackForPianoRoll();
    if (!activeTrack.has_value() || activeTrack->midiNotes.empty()) {
        pitchViewportState_ = core::sanitizePitchViewportState(core::PitchViewportState {});
    } else {
        pitchViewportState_ = core::fitPitchViewportToNotes(activeTrack->midiNotes, 2.0);
    }

    updateVisualWorkspace();
}

void MainComponent::updateVisualWorkspace()
{
    timelineViewportState_ = core::sanitizeTimelineViewportState(timelineViewportState_);
    pitchViewportState_ = core::sanitizePitchViewportState(pitchViewportState_);
    gridDenominator_ = sanitizeGridDenominator(gridDenominator_);
    synchronizeMidiNoteSelectionWithEditableTrack();

    std::vector<const core::Track*> prioritizedTracks;
    prioritizedTracks.reserve(projectController_.selectedTrackIds().size());
    for (const auto trackId : projectController_.selectedTrackIds()) {
        const auto* track = projectModel_.findTrackById(trackId);
        if (track != nullptr && track->type == core::TrackType::midi) {
            prioritizedTracks.push_back(track);
        }
    }

    resolvedTimelineInfo_ = core::resolveMidiTimelineInfo(prioritizedTracks, projectModel_.tracks());

    updateHorizontalScrollRange();

    timelineView_.setTrackRowHeight(trackList_.getRowHeight());
    timelineView_.setTracks(projectModel_.tracks());
    timelineView_.setSelectedTrackIds(projectController_.selectedTrackIds());
    timelineView_.setViewportState(timelineViewportState_);
    timelineView_.setGridDenominator(gridDenominator_);
    timelineView_.setTempoMap(resolvedTimelineInfo_.tempoMap);
    timelineView_.setVerticalScrollPixels(static_cast<int>(std::round(trackList_.getVerticalPosition())));
    timelineView_.setPlayheadBeat(playbackPlayheadBeat_);

    timeRulerView_.setViewportState(timelineViewportState_);
    timeRulerView_.setGridDenominator(gridDenominator_);
    timeRulerView_.setTimeSignatureMap(resolvedTimelineInfo_.timeSignatureMap);

    pianoKeyboardView_.setPitchViewportState(pitchViewportState_);
    pianoRollView_.setViewportState(timelineViewportState_);
    pianoRollView_.setPitchViewportState(pitchViewportState_);
    pianoRollView_.setGridDenominator(gridDenominator_);
    pianoRollView_.setSnapEnabled(snapEnabled_);
    pianoRollView_.setTimeSignatureMap(resolvedTimelineInfo_.timeSignatureMap);
    pianoRollView_.setPlayheadBeat(playbackPlayheadBeat_);

    std::vector<core::MidiNote> primaryNotes;
    juce::String primaryName;
    juce::String candidateName;
    core::MidiComparisonResult comparisonResult;

    bool comparisonActive = false;
    if (midiComparisonEnabled_) {
        const auto pair = selectedMidiComparisonPair();
        if (pair.has_value()) {
            comparisonResult = core::compareMidiNotes(pair->first.midiNotes, pair->second.midiNotes, midiComparisonTolerance_);
            primaryNotes = pair->first.midiNotes;
            primaryName = pair->first.name;
            candidateName = pair->second.name;
            comparisonActive = true;
            pianoRollView_.setComparisonNotes(pair->second.midiNotes, comparisonResult, true);
        } else {
            midiComparisonEnabled_ = false;
        }
    }

    if (!comparisonActive) {
        const auto activeTrack = activeMidiTrackForPianoRoll();
        if (activeTrack.has_value()) {
            primaryNotes = activeTrack->midiNotes;
            primaryName = activeTrack->name;
        }

        pianoRollView_.setComparisonNotes({}, core::MidiComparisonResult {}, false);
    }

    pianoRollView_.setPrimaryNotes(primaryNotes);
    pianoRollView_.setSelectedNoteIds(midiNoteSelectionState_.selectedNoteIds());

    midiComparisonLegend_.setComparisonEnabled(comparisonActive);
    midiComparisonLegend_.setTrackLabels(primaryName, candidateName);
    midiComparisonLegend_.setComparisonSummary(
        comparisonActive ? core::summarizeMidiComparison(comparisonResult) : core::MidiComparisonSummary {});

    snapToggle_.setToggleState(snapEnabled_, juce::dontSendNotification);
    updateVelocityControlState();
    menuBar_.repaint();
    refreshTransportSelectionState();
    updateTransportControlState();
}

void MainComponent::updateStatusForSelection()
{
    juce::String status = "No track selected.";

    const auto selectedIds = projectController_.selectedTrackIds();
    if (selectedIds.size() == 1) {
        if (const auto track = selectedTrack(); track.has_value()) {
            status = "Selected: " + juce::String(track->name) + " (" + trackTypeLabel(track->type) + ")";
            if (track->type == core::TrackType::audio) {
                if (track->audioSourcePath.empty()) {
                    status << " | WAV source: not assigned";
                } else {
                    status << " | WAV source: " << basenameForPath(track->audioSourcePath);
                }
            } else if (track->type == core::TrackType::midi) {
                status << " | MIDI notes: " << static_cast<int>(track->midiNotes.size());
            } else {
                status << " | Group folder";
            }
        }
    } else if (!selectedIds.empty()) {
        status = "Selected tracks: " + juce::String(static_cast<int>(selectedIds.size()));
    }

    const auto pairAvailability = hermes::getHermesCommandAvailability(
        hermes::HermesCommand::synchronizeMidiWithWav,
        projectModel_,
        selectionState_);
    const auto pairReason = hermes::describeAvailability(
        hermes::HermesCommand::synchronizeMidiWithWav,
        pairAvailability);

    if (pairAvailability == hermes::HermesCommandAvailability::enabled) {
        const auto pairContext = selectedAudioMidiPairContext();
        if (pairContext.has_value()) {
            const auto wavName = basenameForPath(pairContext->audioTrack.audioSourcePath);
            const auto midiName = pairContext->midiSourceMetadata.sourceFileName.empty()
                                      ? juce::String(pairContext->midiTrack.trackName)
                                      : juce::String(pairContext->midiSourceMetadata.sourceFileName);
            status << "  |  Hermes pair: " << wavName << " + " << midiName;
        }
    } else if (!pairReason.empty()) {
        status << "  |  " << pairReason;
    }

    if (isHermesJobRunning()) {
        status << "  |  " << describeActiveOperation() << " is running in background.";
    }

    if (midiComparisonEnabled_) {
        if (const auto pair = selectedMidiComparisonPair(); pair.has_value()) {
            status << "  |  Compare MIDI: " << juce::String(pair->first.name)
                   << " vs " << juce::String(pair->second.name);
        } else {
            status << "  |  Compare MIDI: waiting for two selected MIDI tracks";
        }
    }

    statusLabel_.setText(status, juce::dontSendNotification);
}

void MainComponent::addAudioTrack()
{
    const auto& track = projectController_.addTrack(core::TrackType::audio);
    projectController_.selectTrack(track.id);
    refreshTrackView();
    updateStatusForSelection();
}

void MainComponent::addMidiTrack()
{
    const auto& track = projectController_.addTrack(core::TrackType::midi);
    projectController_.selectTrack(track.id);
    refreshTrackView();
    updateStatusForSelection();
}

void MainComponent::importMidiTrack()
{
    juce::FileChooser chooser(
        "Import MIDI file",
        {},
        "*.mid;*.midi");

    if (!chooser.browseForFileToOpen()) {
        return;
    }

    const auto selectedFile = chooser.getResult();
    if (!selectedFile.existsAsFile()) {
        showValidationError(this, "Selected MIDI file does not exist.");
        return;
    }

    std::string parseError;
    const auto importDocument = parseMidiImportDocument(selectedFile.getFullPathName().toStdString(), parseError);
    if (!importDocument.has_value()) {
        showValidationError(this, parseError.empty() ? "Failed to parse MIDI file." : parseError);
        return;
    }

    if (importDocument->noteBearingTracks.empty()) {
        showValidationError(this, "The selected MIDI file did not contain note events.");
        return;
    }

    int selectedCandidateIndex = 0;
    if (importDocument->noteBearingTracks.size() > 1) {
        juce::StringArray trackItems;
        for (const auto& candidate : importDocument->noteBearingTracks) {
            trackItems.add(
                "Index " + juce::String(candidate.sourceTrackIndex)
                + " | " + juce::String(candidate.sourceTrackName)
                + " | notes: " + juce::String(static_cast<int>(candidate.notes.size())));
        }

        juce::AlertWindow dialog(
            "Import MIDI as Track",
            "Select a note-bearing source track to import.",
            juce::AlertWindow::NoIcon);
        dialog.addComboBox("sourceTrack", trackItems);
        dialog.getComboBoxComponent("sourceTrack")->setSelectedItemIndex(0);

        dialog.addButton("Cancel", 0, juce::KeyPress(juce::KeyPress::escapeKey));
        dialog.addButton("Import", 1, juce::KeyPress(juce::KeyPress::returnKey));

        if (dialog.runModalLoop() != 1) {
            return;
        }

        selectedCandidateIndex = dialog.getComboBoxComponent("sourceTrack")->getSelectedItemIndex();
        if (selectedCandidateIndex < 0
            || selectedCandidateIndex >= static_cast<int>(importDocument->noteBearingTracks.size())) {
            showValidationError(this, "Invalid source-track selection.");
            return;
        }
    }

    const auto& candidate = importDocument->noteBearingTracks.at(static_cast<std::size_t>(selectedCandidateIndex));
    std::string displayName = selectedFile.getFileNameWithoutExtension().toStdString();
    if (!candidate.sourceTrackName.empty()) {
        displayName = candidate.sourceTrackName;
    }

    const auto& createdTrack = projectController_.addTrack(core::TrackType::midi, displayName);
    if (!projectController_.replaceMidiNotesOnTrack(createdTrack.id, candidate.notes)) {
        showValidationError(this, "MIDI import did not create any tracks.");
        projectController_.deleteTrackById(createdTrack.id);
        return;
    }

    const auto metadata = makeImportedMidiSourceMetadata(importDocument.value(), candidate);
    if (!projectController_.setMidiSourceMetadata(createdTrack.id, metadata)) {
        showValidationError(this, "Failed to retain MIDI source metadata.");
        projectController_.deleteTrackById(createdTrack.id);
        return;
    }

    projectController_.selectTrack(createdTrack.id);

    refreshTrackView();
    updateStatusForSelection();

    statusLabel_.setText(
        "Imported MIDI track: " + juce::String(displayName)
            + " | source index " + juce::String(candidate.sourceTrackIndex)
            + " | notes: " + juce::String(static_cast<int>(candidate.notes.size())) + ".",
        juce::dontSendNotification);
}

bool MainComponent::canExportSelectedMidiTrack() const
{
    const auto track = selectedTrack();
    return track.has_value() && midi::canExportMidiTrack(track.value());
}

void MainComponent::exportSelectedMidiTrack()
{
    const auto track = selectedTrack();
    if (!track.has_value() || !midi::canExportMidiTrack(track.value())) {
        statusLabel_.setText("Select a non-empty MIDI track before exporting.", juce::dontSendNotification);
        return;
    }

    auto defaultName = juce::File::createLegalFileName(juce::String(track->name) + " - Edited.mid");
    if (!defaultName.endsWithIgnoreCase(".mid") && !defaultName.endsWithIgnoreCase(".midi")) {
        defaultName << ".mid";
    }

    juce::FileChooser chooser(
        "Export selected MIDI track",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory).getChildFile(defaultName),
        "*.mid;*.midi");

    if (!chooser.browseForFileToSave(true)) {
        return;
    }

    auto selectedFile = chooser.getResult();
    if (!selectedFile.hasFileExtension("mid;midi")) {
        selectedFile = selectedFile.withFileExtension(".mid");
    }

    const auto result = midi::exportMidiTrackToFile(
        track.value(),
        selectedFile.getFullPathName().toStdString());

    if (!result.ok) {
        statusLabel_.setText(
            "MIDI export failed: " + juce::String(result.message),
            juce::dontSendNotification);
        return;
    }

    statusLabel_.setText(
        "Exported selected MIDI track: " + selectedFile.getFileName(),
        juce::dontSendNotification);
}

void MainComponent::assignAudioFileToSelectedTrack()
{
    const auto track = selectedTrack();
    if (!track.has_value() || track->type != core::TrackType::audio) {
        showValidationError(this, "Select an audio track before assigning a WAV source file.");
        return;
    }

    juce::FileChooser chooser(
        "Select WAV source file",
        {},
        "*.wav;*.wave");

    if (!chooser.browseForFileToOpen()) {
        return;
    }

    const auto selectedFile = chooser.getResult();
    if (!selectedFile.existsAsFile()) {
        showValidationError(this, "Selected file does not exist.");
        return;
    }

    if (!projectController_.assignAudioSourceToTrack(track->id, selectedFile.getFullPathName().toStdString())) {
        showValidationError(this, "Unable to assign WAV source to selected track.");
        return;
    }

    refreshTrackView();
    updateStatusForSelection();
}

void MainComponent::deleteSelectedTrack()
{
    const auto selectedIds = projectController_.selectedTrackIds();
    if (selectedIds.empty()) {
        statusLabel_.setText("No selected track to delete.", juce::dontSendNotification);
        return;
    }

    bool removedAny = false;
    for (auto it = selectedIds.rbegin(); it != selectedIds.rend(); ++it) {
        removedAny = projectController_.deleteTrackById(*it) || removedAny;
    }

    if (!removedAny) {
        statusLabel_.setText("Selected track could not be deleted.", juce::dontSendNotification);
        return;
    }

    refreshTrackView();
    updateStatusForSelection();
}

std::optional<std::uint64_t> MainComponent::editableMidiTrackIdForPianoRoll() const
{
    if (const auto pair = selectedMidiComparisonPair(); pair.has_value() && midiComparisonEnabled_) {
        return pair->first.id;
    }

    const auto selectedIds = projectController_.selectedTrackIds();
    if (selectedIds.size() != 1) {
        return std::nullopt;
    }

    const auto* track = projectModel_.findTrackById(selectedIds.front());
    if (track == nullptr || track->type != core::TrackType::midi) {
        return std::nullopt;
    }

    return track->id;
}

core::Track* MainComponent::editableMidiTrackForPianoRoll()
{
    const auto trackId = editableMidiTrackIdForPianoRoll();
    return trackId.has_value() ? projectModel_.findTrackById(trackId.value()) : nullptr;
}

const core::Track* MainComponent::editableMidiTrackForPianoRoll() const
{
    const auto trackId = editableMidiTrackIdForPianoRoll();
    return trackId.has_value() ? projectModel_.findTrackById(trackId.value()) : nullptr;
}

void MainComponent::synchronizeMidiNoteSelectionWithEditableTrack()
{
    const auto trackId = editableMidiTrackIdForPianoRoll();
    if (!trackId.has_value()) {
        midiNoteSelectionState_.clear();
        return;
    }

    midiNoteSelectionState_.setActiveTrack(trackId.value());
    if (const auto* track = projectModel_.findTrackById(trackId.value()); track != nullptr) {
        midiNoteSelectionState_.removeDeletedNotes(track->midiNotes);
    }
}

void MainComponent::updateStatusForMidiNoteSelection()
{
    statusLabel_.setText(
        "Selected MIDI notes: " + juce::String(static_cast<int>(midiNoteSelectionState_.selectionCount())),
        juce::dontSendNotification);
}

bool MainComponent::canDeleteSelectedMidiNotes() const
{
    const auto* track = editableMidiTrackForPianoRoll();
    if (track == nullptr || !midiNoteSelectionState_.hasSelection()) {
        return false;
    }

    if (midiNoteSelectionState_.activeTrackId() != track->id) {
        return false;
    }

    return std::any_of(
        midiNoteSelectionState_.selectedNoteIds().begin(),
        midiNoteSelectionState_.selectedNoteIds().end(),
        [track](std::uint64_t noteId) { return trackContainsNoteId(*track, noteId); });
}

bool MainComponent::canEditSelectedMidiNotes() const
{
    return canDeleteSelectedMidiNotes();
}

bool MainComponent::shouldIgnoreKeyboardDeletion() const
{
    return shouldIgnoreKeyboardMidiEditing();
}

bool MainComponent::shouldIgnoreKeyboardMidiEditing() const
{
    if (juce::ModalComponentManager::getInstance()->getNumModalComponents() > 0) {
        return true;
    }

    auto* focused = juce::Component::getCurrentlyFocusedComponent();
    while (focused != nullptr) {
        if (dynamic_cast<juce::TextEditor*>(focused) != nullptr || dynamic_cast<juce::Slider*>(focused) != nullptr) {
            return true;
        }

        focused = focused->getParentComponent();
    }

    return false;
}

void MainComponent::selectMidiNote(std::uint64_t noteId, bool additive)
{
    const auto trackId = editableMidiTrackIdForPianoRoll();
    const auto* track = editableMidiTrackForPianoRoll();
    if (!trackId.has_value() || track == nullptr || noteId == 0 || !trackContainsNoteId(*track, noteId)) {
        return;
    }

    if (additive) {
        midiNoteSelectionState_.toggleNote(trackId.value(), noteId);
    } else {
        midiNoteSelectionState_.selectSingle(trackId.value(), noteId);
    }

    updateVisualWorkspace();
    menuBar_.repaint();
    updateStatusForMidiNoteSelection();
}

void MainComponent::clearMidiNoteSelectionFromEmptyClick(bool additive)
{
    if (additive) {
        return;
    }

    if (!midiNoteSelectionState_.hasSelection()) {
        return;
    }

    midiNoteSelectionState_.clearSelection();
    updateVisualWorkspace();
    menuBar_.repaint();
    updateStatusForSelection();
}

void MainComponent::applyMidiNoteMarqueeSelection(std::vector<std::uint64_t> noteIds, bool additive)
{
    const auto trackId = editableMidiTrackIdForPianoRoll();
    const auto* track = editableMidiTrackForPianoRoll();
    if (!trackId.has_value() || track == nullptr) {
        return;
    }

    std::vector<std::uint64_t> selectedIds;
    if (additive) {
        selectedIds = midiNoteSelectionState_.selectedNoteIds();
    }

    for (const auto noteId : noteIds) {
        if (noteId == 0 || !trackContainsNoteId(*track, noteId)) {
            continue;
        }

        if (std::find(selectedIds.begin(), selectedIds.end(), noteId) == selectedIds.end()) {
            selectedIds.push_back(noteId);
        }
    }

    midiNoteSelectionState_.setSelection(trackId.value(), std::move(selectedIds), std::nullopt);
    updateVisualWorkspace();
    menuBar_.repaint();
    updateStatusForMidiNoteSelection();
}

void MainComponent::createMidiNoteAt(double beat, int pitch)
{
    auto* track = editableMidiTrackForPianoRoll();
    if (track == nullptr) {
        statusLabel_.setText("Select a MIDI track before creating notes.", juce::dontSendNotification);
        return;
    }

    core::MidiNoteCreationRequest request;
    request.clickedBeat = beat;
    request.clickedPitch = pitch;
    request.defaultVelocity = 100;
    request.defaultChannel = core::mostCommonMidiChannel(track->midiNotes);
    request.snapEnabled = snapEnabled_;
    request.gridStepBeats = core::gridStepBeats(gridDenominator_);

    auto note = core::makeCreatedMidiNote(request);
    note.id = projectModel_.allocateMidiNoteId();

    auto command = std::make_unique<CreateMidiNoteCommand>(projectModel_, track->id, note);
    if (!command->redo()) {
        statusLabel_.setText("MIDI note could not be created.", juce::dontSendNotification);
        return;
    }

    redoResult_.reset();
    projectHistory_.pushExecuted(std::move(command));
    midiNoteSelectionState_.selectSingle(track->id, note.id);

    refreshAfterMidiNoteMutation();
    statusLabel_.setText(
        "Created MIDI note: " + noteNameForPitch(note.pitch)
            + " at beat " + juce::String(note.startBeat, 3),
        juce::dontSendNotification);
}

void MainComponent::moveSelectedMidiNotes(
    std::vector<std::uint64_t> selectedNoteIds,
    double deltaBeats,
    int deltaSemitones,
    juce::String statusPrefix,
    bool snapEnabled)
{
    auto* track = editableMidiTrackForPianoRoll();
    if (track == nullptr || selectedNoteIds.empty()) {
        return;
    }

    const auto beforeNotes = track->midiNotes;

    core::MoveSelectedNotesRequest request;
    request.selectedNoteIds = std::move(selectedNoteIds);
    request.requestedDeltaBeats = deltaBeats;
    request.requestedDeltaSemitones = deltaSemitones;
    request.snapEnabled = snapEnabled;
    request.gridStepBeats = core::gridStepBeats(gridDenominator_);

    const auto result = core::moveSelectedNotes(track->midiNotes, request);
    if (!result.changed || beforeNotes == track->midiNotes) {
        track->midiNotes = beforeNotes;
        return;
    }

    redoResult_.reset();
    projectHistory_.pushExecuted(std::make_unique<ReplaceMidiNotesCommand>(
        projectModel_,
        track->id,
        "Move MIDI Notes",
        beforeNotes,
        track->midiNotes));

    refreshAfterMidiNoteMutation();
    statusLabel_.setText(
        statusPrefix + ": " + juce::String(result.appliedDeltaBeats, 3)
            + " beats, " + juce::String(result.appliedDeltaSemitones) + " semitones",
        juce::dontSendNotification);
}

void MainComponent::resizeSelectedMidiNotes(
    std::uint64_t anchorNoteId,
    std::vector<std::uint64_t> selectedNoteIds,
    double requestedAnchorEndBeat)
{
    auto* track = editableMidiTrackForPianoRoll();
    if (track == nullptr || selectedNoteIds.empty() || anchorNoteId == 0) {
        return;
    }

    const auto beforeNotes = track->midiNotes;

    core::ResizeSelectedNotesRequest request;
    request.selectedNoteIds = std::move(selectedNoteIds);
    request.anchorNoteId = anchorNoteId;
    request.requestedAnchorEndBeat = requestedAnchorEndBeat;
    request.snapEnabled = snapEnabled_;
    request.gridStepBeats = core::gridStepBeats(gridDenominator_);

    const auto result = core::resizeSelectedNotes(track->midiNotes, request);
    if (!result.changed || beforeNotes == track->midiNotes) {
        track->midiNotes = beforeNotes;
        return;
    }

    redoResult_.reset();
    projectHistory_.pushExecuted(std::make_unique<ReplaceMidiNotesCommand>(
        projectModel_,
        track->id,
        "Resize MIDI Notes",
        beforeNotes,
        track->midiNotes));

    refreshAfterMidiNoteMutation();
    statusLabel_.setText(
        "Resized MIDI notes: " + juce::String(result.appliedDurationDeltaBeats, 3) + " beats",
        juce::dontSendNotification);
}

void MainComponent::nudgeSelectedMidiNotes(int deltaSemitones, double deltaBeats)
{
    if (shouldIgnoreKeyboardMidiEditing()) {
        return;
    }

    if (!midiNoteSelectionState_.hasSelection()) {
        return;
    }

    moveSelectedMidiNotes(
        midiNoteSelectionState_.selectedNoteIds(),
        deltaBeats,
        deltaSemitones,
        "Nudged MIDI notes",
        true);
}

std::optional<int> MainComponent::primarySelectedMidiNoteVelocity() const
{
    const auto* track = editableMidiTrackForPianoRoll();
    const auto noteId = midiNoteSelectionState_.primarySelectedNoteId();
    if (track == nullptr || !noteId.has_value()) {
        return std::nullopt;
    }

    const auto noteIndex = core::findNoteIndexById(track->midiNotes, noteId.value());
    if (!noteIndex.has_value()) {
        return std::nullopt;
    }

    return track->midiNotes.at(noteIndex.value()).velocity;
}

void MainComponent::updateVelocityControlState()
{
    const auto velocity = primarySelectedMidiNoteVelocity();

    suppressVelocityEditorCallbacks_ = true;
    if (velocity.has_value() && canEditSelectedMidiNotes()) {
        velocityEditor_.setEnabled(true);
        velocityEditor_.setText(juce::String(velocity.value()), false);
    } else {
        velocityEditor_.setText({}, false);
        velocityEditor_.setEnabled(false);
    }
    suppressVelocityEditorCallbacks_ = false;
}

void MainComponent::applyVelocityEditorValue()
{
    if (suppressVelocityEditorCallbacks_ || !velocityEditor_.isEnabled()) {
        return;
    }

    const auto text = velocityEditor_.getText().trim();
    if (text.isEmpty()) {
        updateVelocityControlState();
        return;
    }

    applyVelocityToSelectedMidiNotes(text.getIntValue());
}

void MainComponent::applyVelocityToSelectedMidiNotes(int velocity)
{
    auto* track = editableMidiTrackForPianoRoll();
    if (track == nullptr || !canEditSelectedMidiNotes()) {
        updateVelocityControlState();
        return;
    }

    const auto beforeNotes = track->midiNotes;
    const auto clampedVelocity = core::clampMidiVelocity(velocity);
    const auto changedCount = core::applyVelocityToSelectedNotes(
        track->midiNotes,
        midiNoteSelectionState_.selectedNoteIds(),
        clampedVelocity);

    if (changedCount == 0 || beforeNotes == track->midiNotes) {
        track->midiNotes = beforeNotes;
        updateVelocityControlState();
        return;
    }

    redoResult_.reset();
    projectHistory_.pushExecuted(std::make_unique<ReplaceMidiNotesCommand>(
        projectModel_,
        track->id,
        "Set MIDI Note Velocity",
        beforeNotes,
        track->midiNotes));

    refreshAfterMidiNoteMutation();
    statusLabel_.setText(
        "Set velocity " + juce::String(clampedVelocity) + " for "
            + juce::String(static_cast<int>(changedCount)) + " MIDI notes",
        juce::dontSendNotification);
}

void MainComponent::quantizeSelectedMidiNotesToGrid()
{
    auto* track = editableMidiTrackForPianoRoll();
    if (track == nullptr || !canEditSelectedMidiNotes()) {
        statusLabel_.setText("No selected MIDI notes to quantize.", juce::dontSendNotification);
        updateVelocityControlState();
        return;
    }

    const auto beforeNotes = track->midiNotes;
    const auto changedCount = core::quantizeSelectedNoteStarts(
        track->midiNotes,
        midiNoteSelectionState_.selectedNoteIds(),
        core::gridStepBeats(gridDenominator_));

    if (changedCount == 0 || beforeNotes == track->midiNotes) {
        track->midiNotes = beforeNotes;
        statusLabel_.setText("No MIDI notes changed by quantize.", juce::dontSendNotification);
        updateVelocityControlState();
        return;
    }

    redoResult_.reset();
    projectHistory_.pushExecuted(std::make_unique<ReplaceMidiNotesCommand>(
        projectModel_,
        track->id,
        "Quantize MIDI Notes",
        beforeNotes,
        track->midiNotes));

    refreshAfterMidiNoteMutation();
    statusLabel_.setText(
        "Quantized " + juce::String(static_cast<int>(changedCount))
            + " MIDI notes to " + gridLabelForDenominator(gridDenominator_) + " grid",
        juce::dontSendNotification);
}

void MainComponent::deleteSelectedMidiNotes(bool fromKeyboard)
{
    if (fromKeyboard && shouldIgnoreKeyboardDeletion()) {
        return;
    }

    auto* track = editableMidiTrackForPianoRoll();
    if (track == nullptr || !canDeleteSelectedMidiNotes()) {
        if (!fromKeyboard) {
            statusLabel_.setText("No selected MIDI notes to delete.", juce::dontSendNotification);
        }
        return;
    }

    std::vector<std::uint64_t> noteIds;
    std::vector<core::MidiNote> deletedNotes;
    for (const auto& note : track->midiNotes) {
        if (midiNoteSelectionState_.isSelected(note.id)) {
            noteIds.push_back(note.id);
            deletedNotes.push_back(note);
        }
    }

    if (deletedNotes.empty()) {
        midiNoteSelectionState_.clearSelection();
        updateVisualWorkspace();
        menuBar_.repaint();
        return;
    }

    auto command = std::make_unique<DeleteMidiNotesCommand>(projectModel_, track->id, noteIds, deletedNotes);
    if (!command->redo()) {
        statusLabel_.setText("Selected MIDI notes could not be deleted.", juce::dontSendNotification);
        return;
    }

    redoResult_.reset();
    projectHistory_.pushExecuted(std::move(command));
    midiNoteSelectionState_.clearSelection();

    const auto deletedCount = deletedNotes.size();
    refreshAfterMidiNoteMutation();
    statusLabel_.setText(
        "Deleted " + juce::String(static_cast<int>(deletedCount)) + " MIDI notes",
        juce::dontSendNotification);
}

void MainComponent::refreshAfterMidiNoteMutation()
{
    synchronizeMidiNoteSelectionWithEditableTrack();
    refreshTrackView();
    trackList_.repaint();
    menuBar_.repaint();
}

void MainComponent::startHermesJob(
    const hermes::HermesJobRequest& request,
    const juce::String& runningStatus,
    const juce::String& operationLabel)
{
    if (hermesJobRunner_ == nullptr) {
        showValidationError(this, "Hermes worker is unavailable.");
        return;
    }

    std::string submitError;
    activeOperation_ = request.kind;

    const auto safeThis = juce::Component::SafePointer<MainComponent>(this);
    const auto submitted = hermesJobRunner_->submit(
        request,
        [safeThis](hermes::HermesJobResult result) {
            juce::MessageManager::callAsync([safeThis, result = std::move(result)]() mutable {
                if (auto* owner = safeThis.getComponent()) {
                    owner->completeHermesJob(std::move(result));
                }
            });
        },
        submitError);

    if (!submitted) {
        activeOperation_.reset();
        showValidationError(this, submitError);
        return;
    }

    app::AppLogger::log(operationLabel + " queued on Hermes worker.");
    statusLabel_.setText(runningStatus, juce::dontSendNotification);
    menuBar_.repaint();
}

void MainComponent::completeHermesJob(hermes::HermesJobResult result)
{
    activeOperation_.reset();
    menuBar_.repaint();

    const auto title = operationTitle(result.kind);

    if (!result.operationResult.isSuccess()) {
        app::AppLogger::log(
            title + " failed after " + juce::String(result.durationMs, 2) + " ms: "
            + juce::String(result.operationResult.message));
        showHermesOperationMessage(
            this,
            title,
            juce::String(result.operationResult.message),
            juce::AlertWindow::WarningIcon);
        updateStatusForSelection();
        return;
    }

    auto sourceContext = hermes::HermesTrackContext {};
    sourceContext.trackType = core::TrackType::audio;
    sourceContext.trackName = "Hermes Source";

    const auto sourceTrackId = result.operationResult.sourceAudioTrackId;
    if (sourceTrackId != 0) {
        sourceContext.trackId = sourceTrackId;
        if (const auto* track = projectModel_.findTrackById(sourceTrackId); track != nullptr) {
            sourceContext.trackName = track->name;
            sourceContext.trackType = track->type;
            sourceContext.audioSourcePath = track->audioSourcePath;
        }
    }

    hermes::AppliedHermesResult appliedResult;
    const auto groupId = buildHermesGroupId().toStdString();
    const auto applyResult = hermes::applyHermesResultToProject(
        sourceContext,
        result.operationResult,
        groupId,
        projectController_,
        appliedResult);

    if (!applyResult.ok) {
        app::AppLogger::log(title + " apply failed: " + juce::String(applyResult.message));
        showHermesOperationMessage(
            this,
            title,
            juce::String(applyResult.message),
            juce::AlertWindow::WarningIcon);
        statusLabel_.setText(applyResult.message, juce::dontSendNotification);
        updateStatusForSelection();
        return;
    }

    undoResult_ = appliedResult;
    redoResult_.reset();

    if (!appliedResult.midiTrackIds.empty()) {
        projectController_.selectTrack(appliedResult.midiTrackIds.front());
    } else if (!appliedResult.trackIds.empty()) {
        projectController_.selectTrack(appliedResult.trackIds.front());
    }

    refreshTrackView();
    updateStatusForSelection();

    juce::String status = juce::String("Inserted ") + juce::String(static_cast<int>(applyResult.insertedNoteCount))
        + " MIDI note(s) into " + juce::String(static_cast<int>(applyResult.insertedTrackCount))
        + " track(s).";

    if (!result.operationResult.message.empty()) {
        status << " " << result.operationResult.message;
    }

    if (!result.operationResult.warnings.empty()) {
        status << " (warnings: " << static_cast<int>(result.operationResult.warnings.size()) << ")";
    }

    statusLabel_.setText(status, juce::dontSendNotification);

    app::AppLogger::log(
        title + " completed in " + juce::String(result.durationMs, 2) + " ms"
        + ", inserted_tracks=" + juce::String(static_cast<int>(applyResult.insertedTrackCount))
        + ", inserted_notes=" + juce::String(static_cast<int>(applyResult.insertedNoteCount)));

    if (!result.operationResult.warnings.empty()) {
        juce::String warningMessage;
        for (const auto& warning : result.operationResult.warnings) {
            warningMessage << "- " << warning << "\n";
            app::AppLogger::log(title + " warning: " + juce::String(warning));
        }

        showHermesOperationMessage(
            this,
            title + " Warnings",
            warningMessage.trimEnd(),
            juce::AlertWindow::InfoIcon);
    }
}

void MainComponent::startSelectedMidiPlayback()
{
    if (midiAuditionEngine_.isPaused()) {
        std::string error;
        if (!midiAuditionEngine_.resume(error)) {
            statusLabel_.setText(
                "Audio output unavailable: " + juce::String(error),
                juce::dontSendNotification);
            updateTransportControlState();
            return;
        }

        previousTransportMode_ = audio::TransportMode::playing;
        playheadFollowActive_ = false;
        updatePlaybackPlayhead(false);
        updateTransportDisplays();
        statusLabel_.setText("Playing: resumed", juce::dontSendNotification);
        updateTransportControlState();
        return;
    }

    refreshTransportSelectionState();
    audio::SelectionPlaybackOptions options;
    if (selectedWavBpmResult_.has_value()
        && selectedWavBpmResult_->estimate.isConfident()
        && transportSelectionSummary_.firstReadableAudioPath.has_value()
        && requestedWavFingerprint_.has_value()
        && requestedWavFingerprint_->sourcePath
            == selectedWavBpmResult_->fingerprint.sourcePath) {
        options.detectedWavBpm = selectedWavBpmResult_->estimate.bpm;
    }

    auto snapshotResult = audio::createSelectionPlaybackSnapshot(
        projectModel_,
        selectionState_,
        options);
    if (!snapshotResult.ok) {
        statusLabel_.setText(
            juce::String(snapshotResult.message),
            juce::dontSendNotification);
        updateTransportControlState();
        return;
    }

    const auto usesFallbackTempo =
        snapshotResult.snapshot.tempoSource == audio::PlaybackTempoSource::fallback;
    auto startSeconds = midiAuditionEngine_.playheadSeconds();
    if (startSeconds >= snapshotResult.snapshot.durationSeconds - 1.0e-9) {
        startSeconds = 0.0;
    }

    std::string error;
    if (!midiAuditionEngine_.startPlayback(
            std::move(snapshotResult.snapshot),
            startSeconds,
            error)) {
        statusLabel_.setText(
            "Audio output unavailable: " + juce::String(error),
            juce::dontSendNotification);
        updateTransportControlState();
        return;
    }

    previousTransportMode_ = audio::TransportMode::playing;
    playheadFollowActive_ = false;
    updatePlaybackPlayhead(true);
    updateTransportDisplays();
    juce::String status(snapshotResult.message);
    if (!snapshotResult.skippedAudioTracks.empty()) {
        status << " | "
               << juce::String(audio::describeSkippedAudioTrack(
                      snapshotResult.skippedAudioTracks.front()));
        if (snapshotResult.skippedAudioTracks.size() > 1) {
            status << " (+" << static_cast<int>(snapshotResult.skippedAudioTracks.size() - 1)
                   << " more)";
        }
    }
    if (usesFallbackTempo) {
        status << " | Using 120.0 BPM fallback for this playback.";
    }
    statusLabel_.setText(status, juce::dontSendNotification);
    updateTransportControlState();
}

void MainComponent::pausePlayback()
{
    if (!midiAuditionEngine_.isPlaying()) {
        return;
    }

    midiAuditionEngine_.pause();
    previousTransportMode_ = audio::TransportMode::paused;
    playheadFollowActive_ = false;
    updatePlaybackPlayhead(false);
    updateTransportDisplays();
    statusLabel_.setText("Paused playback", juce::dontSendNotification);
    updateTransportControlState();
}

void MainComponent::stopMidiPlayback()
{
    midiAuditionEngine_.stop();
    previousTransportMode_ = audio::TransportMode::stopped;
    playheadFollowActive_ = false;
    updatePlaybackPlayhead(false);
    updateTransportDisplays();
    statusLabel_.setText("Stopped playback", juce::dontSendNotification);
    updateTransportControlState();
}

void MainComponent::panicMidiPlayback()
{
    midiAuditionEngine_.panic();
    midiAuditionEngine_.setPreviewDuration(
        transportSelectionSummary_.durationSeconds);
    previousTransportMode_ = audio::TransportMode::stopped;
    playheadFollowActive_ = false;
    updatePlaybackPlayhead(false);
    updateTransportDisplays();
    statusLabel_.setText("Panic: all playback silenced", juce::dontSendNotification);
    updateTransportControlState();
}

void MainComponent::seekPlayback(double deltaSeconds)
{
    const auto total = midiAuditionEngine_.totalDurationSeconds();
    if (total <= 0.0) {
        return;
    }

    const auto target = audio::seekTransportSeconds(
        midiAuditionEngine_.playheadSeconds(),
        deltaSeconds,
        total);
    midiAuditionEngine_.seekTo(target);
    playheadFollowActive_ = false;
    updatePlaybackPlayhead(true);
    updateTransportDisplays();
    statusLabel_.setText(
        deltaSeconds < 0.0 ? "Seek: -5 seconds" : "Seek: +5 seconds",
        juce::dontSendNotification);
    updateTransportControlState();
}

void MainComponent::refreshTransportSelectionState()
{
    transportSelectionSummary_ = audio::createSelectionPlaybackSummary(
        projectModel_,
        selectionState_);
    requestSelectedWavBpmIfNeeded();

    audio::SelectionPlaybackOptions options;
    if (selectedWavBpmResult_.has_value()
        && selectedWavBpmResult_->estimate.isConfident()
        && requestedWavFingerprint_.has_value()
        && requestedWavFingerprint_->sourcePath
            == selectedWavBpmResult_->fingerprint.sourcePath) {
        options.detectedWavBpm = selectedWavBpmResult_->estimate.bpm;
    }

    const auto summary = audio::createSelectionPlaybackSummary(
        projectModel_,
        selectionState_,
        options);
    transportSelectionSummary_ = summary;
    if (const auto snapshot = midiAuditionEngine_.playbackSnapshot();
        snapshot != nullptr
        && midiAuditionEngine_.transportMode() != audio::TransportMode::stopped) {
        resolvedTimelineInfo_.tempoMap = snapshot->playheadTempoMap;
    } else if (!summary.tempoMap.empty()) {
        resolvedTimelineInfo_.tempoMap = summary.tempoMap;
    }
    timelineView_.setTempoMap(resolvedTimelineInfo_.tempoMap);
    if (midiAuditionEngine_.transportMode() == audio::TransportMode::stopped) {
        midiAuditionEngine_.setPreviewDuration(summary.durationSeconds);
        updatePlaybackPlayhead(false);
    }
    updateTransportDisplays();
}

void MainComponent::requestSelectedWavBpmIfNeeded()
{
    if (!transportSelectionSummary_.firstReadableAudioPath.has_value()) {
        requestedWavFingerprint_.reset();
        selectedWavBpmResult_.reset();
        wavBpmRequestGeneration_ = 0;
        return;
    }

    const auto fingerprint = audio::fingerprintWavFile(
        transportSelectionSummary_.firstReadableAudioPath.value());
    if (!fingerprint.has_value()) {
        requestedWavFingerprint_.reset();
        selectedWavBpmResult_.reset();
        wavBpmRequestGeneration_ = 0;
        return;
    }

    if (requestedWavFingerprint_.has_value()
        && requestedWavFingerprint_.value() == fingerprint.value()) {
        return;
    }

    requestedWavFingerprint_ = fingerprint;
    selectedWavBpmResult_.reset();
    wavBpmRequestGeneration_ = wavBpmAnalysisService_.request(
        fingerprint->sourcePath);
    if (const auto cached = wavBpmAnalysisService_.pollCompleted();
        cached.has_value()
        && cached->requestGeneration == wavBpmRequestGeneration_
        && cached->fingerprint == fingerprint.value()) {
        selectedWavBpmResult_ = cached;
    }
}

void MainComponent::processCompletedWavBpmAnalysis()
{
    const auto completed = wavBpmAnalysisService_.pollCompleted();
    if (!completed.has_value()
        || completed->requestGeneration != wavBpmRequestGeneration_
        || !requestedWavFingerprint_.has_value()
        || !(completed->fingerprint == requestedWavFingerprint_.value())) {
        return;
    }

    selectedWavBpmResult_ = completed;
    if (midiAuditionEngine_.transportMode() == audio::TransportMode::stopped) {
        refreshTransportSelectionState();
    }

    if (!completed->estimate.isConfident()
        && transportSelectionSummary_.playable
        && transportSelectionSummary_.tempoSource
            != audio::PlaybackTempoSource::explicitMidi) {
        statusLabel_.setText(
            "WAV BPM uncertain; using 120.0 BPM fallback.",
            juce::dontSendNotification);
    }
    updateTransportDisplays();
}

void MainComponent::updateTransportDisplays()
{
    timeCounterLabel_.setText(
        juce::String(audio::formatTransportCounter(
            midiAuditionEngine_.playheadSeconds(),
            midiAuditionEngine_.totalDurationSeconds())),
        juce::dontSendNotification);

    const auto snapshot = midiAuditionEngine_.playbackSnapshot();
    const auto hasPlayableSelection = midiAuditionEngine_.totalDurationSeconds() > 0.0
        || transportSelectionSummary_.playable;
    if (!hasPlayableSelection) {
        bpmLabel_.setText("BPM --", juce::dontSendNotification);
        bpmLabel_.setTooltip("No playable selection.");
        return;
    }

    const auto activeSnapshot = snapshot != nullptr
        && midiAuditionEngine_.transportMode() != audio::TransportMode::stopped;
    if (activeSnapshot) {
        bpmLabel_.setText(
            "BPM " + juce::String(
                audio::selectionPlaybackBpm(
                    midiAuditionEngine_.playheadSeconds(),
                    *snapshot),
                1),
            juce::dontSendNotification);
        if (snapshot->tempoSource == audio::PlaybackTempoSource::explicitMidi) {
            bpmLabel_.setTooltip("Explicit MIDI tempo map.");
        } else if (snapshot->tempoSource == audio::PlaybackTempoSource::detectedWav) {
            bpmLabel_.setTooltip(
                "Detected WAV tempo estimate; audio remains at original speed.");
        } else {
            bpmLabel_.setTooltip(
                "120.0 BPM fallback for this immutable playback snapshot.");
        }
        return;
    }

    if (snapshot == nullptr
        && transportSelectionSummary_.tempoSource
            == audio::PlaybackTempoSource::explicitMidi) {
        const auto beat = audio::midiSecondsToBeat(
            midiAuditionEngine_.playheadSeconds(),
            transportSelectionSummary_.tempoMap);
        bpmLabel_.setText(
            "BPM " + juce::String(
                audio::playbackBpmAtBeat(
                    beat,
                    transportSelectionSummary_.tempoMap),
                1),
            juce::dontSendNotification);
        bpmLabel_.setTooltip("Explicit MIDI tempo map.");
        return;
    }

    const auto resultMatchesSelection = selectedWavBpmResult_.has_value()
        && selectedWavBpmResult_->estimate.isConfident()
        && requestedWavFingerprint_.has_value()
        && selectedWavBpmResult_->fingerprint
            == requestedWavFingerprint_.value();
    const auto resultMatchesPlayback = snapshot == nullptr
        || (requestedWavFingerprint_.has_value()
            && !snapshot->audioStems.empty()
            && std::filesystem::path(snapshot->audioStems.front().sourcePath)
                    .lexically_normal()
                == std::filesystem::path(
                       requestedWavFingerprint_->sourcePath)
                       .lexically_normal());
    if (resultMatchesSelection && resultMatchesPlayback) {
        bpmLabel_.setText(
            "BPM " + juce::String(
                selectedWavBpmResult_->estimate.bpm.value(),
                1),
            juce::dontSendNotification);
        bpmLabel_.setTooltip(
            "Detected WAV tempo estimate; audio remains at original speed.");
        return;
    }

    if (wavBpmAnalysisService_.isAnalyzing()
        && requestedWavFingerprint_.has_value()) {
        bpmLabel_.setText("BPM analyzing...", juce::dontSendNotification);
        bpmLabel_.setTooltip("Analyzing a bounded WAV segment outside the audio callback.");
        return;
    }

    bpmLabel_.setText("BPM 120.0", juce::dontSendNotification);
    bpmLabel_.setTooltip("Fallback transport tempo; WAV detection was unavailable or uncertain.");
}

void MainComponent::updatePlaybackPlayhead(bool ensureVisible)
{
    const auto total = midiAuditionEngine_.totalDurationSeconds();
    if (total <= 0.0) {
        playbackPlayheadBeat_.reset();
    } else if (const auto snapshot = midiAuditionEngine_.playbackSnapshot();
               snapshot != nullptr) {
        playbackPlayheadBeat_ = audio::selectionPlayheadBeat(
            midiAuditionEngine_.playheadSeconds(),
            *snapshot);
    } else {
        playbackPlayheadBeat_ = audio::midiSecondsToBeat(
            midiAuditionEngine_.playheadSeconds(),
            transportSelectionSummary_.tempoMap);
    }

    timelineView_.setPlayheadBeat(playbackPlayheadBeat_);
    pianoRollView_.setPlayheadBeat(playbackPlayheadBeat_);

    if (ensureVisible && playbackPlayheadBeat_.has_value()) {
        timelineViewportState_ = core::ensureTimelineBeatVisible(
            timelineViewportState_,
            playbackPlayheadBeat_.value(),
            estimateHorizontalContentEndBeat());
        updateHorizontalViewportViews();
    }
}

void MainComponent::updateHorizontalViewportViews()
{
    updateHorizontalScrollRange();
    timelineView_.setViewportState(timelineViewportState_);
    timeRulerView_.setViewportState(timelineViewportState_);
    pianoRollView_.setViewportState(timelineViewportState_);
}

void MainComponent::updateTransportControlState()
{
    const auto state = audio::selectionTransportCommandState(
        projectModel_,
        selectionState_,
        midiAuditionEngine_.transportMode(),
        midiAuditionEngine_.totalDurationSeconds());
    rewindButton_.setEnabled(state.rewindEnabled);
    playButton_.setEnabled(state.playEnabled);
    pauseButton_.setEnabled(state.pauseEnabled);
    stopButton_.setEnabled(state.stopEnabled);
    fastForwardButton_.setEnabled(state.fastForwardEnabled);
    panicButton_.setEnabled(state.panicEnabled);
}

void MainComponent::timerCallback()
{
    midiAuditionEngine_.collectRetiredSnapshots();
    processCompletedWavBpmAnalysis();

    const auto mode = midiAuditionEngine_.transportMode();
    updatePlaybackPlayhead(false);
    if (audio::shouldAutomaticallyFollowPlayhead(mode)
        && playbackPlayheadBeat_.has_value()) {
        const auto visible = core::timelineVisibleRange(timelineViewportState_);
        const auto thresholdBeat = visible.startBeat
            + (timelineViewportState_.visibleBeats * 0.80);
        if (playbackPlayheadBeat_.value() >= thresholdBeat) {
            playheadFollowActive_ = true;
        }

        if (playheadFollowActive_) {
            const auto followed = core::followTimelinePlayhead(
                timelineViewportState_,
                playbackPlayheadBeat_.value(),
                estimateHorizontalContentEndBeat(),
                0.0,
                0.72);
            if (std::abs(followed.startBeat - timelineViewportState_.startBeat) > 1.0e-9) {
                timelineViewportState_ = followed;
                updateHorizontalViewportViews();
            }
        }
    } else if (previousTransportMode_ == audio::TransportMode::playing
               && mode == audio::TransportMode::stopped
               && midiAuditionEngine_.totalDurationSeconds() > 0.0
               && midiAuditionEngine_.playheadSeconds()
                   >= midiAuditionEngine_.totalDurationSeconds() - 1.0e-6) {
        statusLabel_.setText("Selection playback finished.", juce::dontSendNotification);
    }

    if (!audio::shouldAutomaticallyFollowPlayhead(mode)) {
        playheadFollowActive_ = false;
    }

    previousTransportMode_ = mode;
    updateTransportDisplays();
    updateTransportControlState();
}

void MainComponent::runHermesDrumsMakeMidi()
{
    app::AppLogger::log("Hermes command selected: Drums -> Make MIDI from WAV");

    if (isHermesJobRunning()) {
        showValidationError(this, "Hermes processing is already running.");
        return;
    }

    const auto availability = hermes::getHermesCommandAvailability(
        hermes::HermesCommand::drumsMakeMidiFromWav,
        projectModel_,
        selectionState_);

    if (availability != hermes::HermesCommandAvailability::enabled) {
        const auto reason = hermes::describeAvailability(
            hermes::HermesCommand::drumsMakeMidiFromWav,
            availability);
        app::AppLogger::log("Validation failure: " + juce::String(reason));
        showValidationError(this, reason);
        updateStatusForSelection();
        return;
    }

    const auto context = selectedTrackContext();
    if (!context.has_value()) {
        showValidationError(this, "No selected track context available.");
        return;
    }

    const auto sourceFile = juce::File(context->audioSourcePath);
    if (!sourceFile.existsAsFile()) {
        const auto message = "Selected WAV source file does not exist: " + context->audioSourcePath;
        app::AppLogger::log("Validation failure: " + juce::String(message));
        showValidationError(this, message);
        return;
    }

    const auto contextValidation = hermes::validateTrackContextForDrums(context.value());
    if (!contextValidation.ok) {
        showValidationError(this, contextValidation.message);
        updateStatusForSelection();
        return;
    }

    const auto options = showDrumsMakeMidiDialog(this);
    if (!options.has_value()) {
        return;
    }

    const auto validation = hermes::validateDrumsOptions(options.value());
    if (!validation.ok) {
        app::AppLogger::log("Validation failure: " + juce::String(validation.message));
        showValidationError(this, validation.message);
        return;
    }

    hermes::HermesJobRequest request;
    request.kind = hermes::HermesOperationKind::drumsExtraction;
    request.trackContext = context.value();
    request.drumsOptions = options.value();

    startHermesJob(
        request,
        "Hermes drums processing is running in background...",
        "Hermes Drums");
}

void MainComponent::runHermesDrumMapping()
{
    app::AppLogger::log("Hermes command selected: Drums -> Drum Mapping");

    if (showDrumMappingDialog(this)) {
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::InfoIcon,
            "DAWHermes",
            "Drum mapping changes were applied to the UI mapping shell. Full custom-map bridge is scheduled for a later milestone.");
    }
}

void MainComponent::runHermesBassRepair()
{
    app::AppLogger::log("Hermes command selected: Bass -> Repair MIDI against WAV");

    if (isHermesJobRunning()) {
        showValidationError(this, "Hermes processing is already running.");
        return;
    }

    const auto availability = hermes::getHermesCommandAvailability(
        hermes::HermesCommand::bassMakeRepairMidiFromWav,
        projectModel_,
        selectionState_);

    if (availability != hermes::HermesCommandAvailability::enabled) {
        const auto reason = hermes::describeAvailability(
            hermes::HermesCommand::bassMakeRepairMidiFromWav,
            availability);
        app::AppLogger::log("Validation failure: " + juce::String(reason));
        showValidationError(this, reason);
        updateStatusForSelection();
        return;
    }

    const auto pairContext = selectedAudioMidiPairContext();
    if (!pairContext.has_value()) {
        showValidationError(this, "Select exactly one audio track and one non-empty MIDI track.");
        return;
    }

    const auto contextValidation = hermes::validateTrackContextForAudioMidiPair(pairContext.value());
    if (!contextValidation.ok) {
        showValidationError(this, contextValidation.message);
        return;
    }

    const auto options = showBassRepairDialog(this, pairContext.value());
    if (!options.has_value()) {
        return;
    }

    const auto optionsValidation = hermes::validateBassOptions(options.value());
    if (!optionsValidation.ok) {
        showValidationError(this, optionsValidation.message);
        return;
    }

    hermes::HermesJobRequest request;
    request.kind = hermes::HermesOperationKind::bassRepair;
    request.pairContext = pairContext.value();
    request.bassOptions = options.value();

    startHermesJob(
        request,
        "Hermes bass repair is running in background...",
        "Hermes Bass");
}

void MainComponent::runHermesSynchronize()
{
    app::AppLogger::log("Hermes command selected: Synchronize MIDI with WAV");

    if (isHermesJobRunning()) {
        showValidationError(this, "Hermes processing is already running.");
        return;
    }

    const auto availability = hermes::getHermesCommandAvailability(
        hermes::HermesCommand::synchronizeMidiWithWav,
        projectModel_,
        selectionState_);

    if (availability != hermes::HermesCommandAvailability::enabled) {
        const auto reason = hermes::describeAvailability(
            hermes::HermesCommand::synchronizeMidiWithWav,
            availability);
        app::AppLogger::log("Validation failure: " + juce::String(reason));
        showValidationError(this, reason);
        updateStatusForSelection();
        return;
    }

    const auto pairContext = selectedAudioMidiPairContext();
    if (!pairContext.has_value()) {
        showValidationError(this, "Select exactly one audio track and one non-empty MIDI track.");
        return;
    }

    const auto contextValidation = hermes::validateTrackContextForAudioMidiPair(pairContext.value());
    if (!contextValidation.ok) {
        showValidationError(this, contextValidation.message);
        return;
    }

    const auto options = showSynchronizeDialog(this, pairContext.value());
    if (!options.has_value()) {
        return;
    }

    const auto optionsValidation = hermes::validateSyncOptions(options.value());
    if (!optionsValidation.ok) {
        showValidationError(this, optionsValidation.message);
        return;
    }

    hermes::HermesJobRequest request;
    request.kind = hermes::HermesOperationKind::midiWavSynchronization;
    request.pairContext = pairContext.value();
    request.syncOptions = options.value();

    startHermesJob(
        request,
        "Hermes MIDI/WAV synchronization is running in background...",
        "Hermes Synchronize");
}

void MainComponent::runHermesSetFixBpm()
{
    app::AppLogger::log("Hermes command selected: Set / Fix BPM");

    const auto availability = hermes::getHermesCommandAvailability(
        hermes::HermesCommand::setFixBpm,
        projectModel_,
        selectionState_);

    if (availability != hermes::HermesCommandAvailability::enabled) {
        const auto reason = hermes::describeAvailability(
            hermes::HermesCommand::setFixBpm,
            availability);
        app::AppLogger::log("Validation failure: " + juce::String(reason));
        showValidationError(this, reason);
        return;
    }

    const auto context = selectedTrackContext();
    if (!context.has_value()) {
        showValidationError(this, "No selected track context available.");
        return;
    }

    const auto bpmOptions = showSetFixBpmDialog(this, context.value());
    if (!bpmOptions.has_value()) {
        return;
    }

    const auto validation = hermes::validateBpmOptions(bpmOptions.value());
    if (!validation.ok) {
        app::AppLogger::log("Validation failure: " + juce::String(validation.message));
        showValidationError(this, validation.message);
        return;
    }

    showMilestoneNotIntegratedMessage(this, "Set / Fix BPM is not integrated yet.");
    statusLabel_.setText("Set / Fix BPM is not yet integrated.", juce::dontSendNotification);
}

void MainComponent::runUndo()
{
    if (isHermesJobRunning()) {
        statusLabel_.setText("Wait for Hermes processing to finish before undo.", juce::dontSendNotification);
        return;
    }

    if (!canUndo()) {
        statusLabel_.setText("Nothing to undo.", juce::dontSendNotification);
        return;
    }

    if (projectHistory_.canUndo()) {
        const auto label = projectHistory_.undoLabel();
        if (!projectHistory_.undo()) {
            statusLabel_.setText("Undo failed: MIDI edit could not be restored.", juce::dontSendNotification);
            return;
        }

        synchronizeMidiNoteSelectionWithEditableTrack();
        refreshTrackView();
        updateStatusForSelection();
        statusLabel_.setText("Undo " + juce::String(label), juce::dontSendNotification);
        app::AppLogger::log("Undo " + juce::String(label));
        return;
    }

    auto resultToUndo = undoResult_.value();
    if (!hermes::undoAppliedHermesResult(projectController_, resultToUndo)) {
        app::AppLogger::log("Undo failed for last Hermes result.");
        statusLabel_.setText("Undo failed: generated Hermes tracks were already modified.", juce::dontSendNotification);
        return;
    }

    const auto removedTrackCount = resultToUndo.tracks.size();
    const auto removedNoteCount = hermes::countAppliedHermesNotes(resultToUndo);

    redoResult_ = std::move(resultToUndo);
    undoResult_.reset();

    refreshTrackView();
    updateStatusForSelection();

    const auto status = juce::String("Undo removed ") + juce::String(static_cast<int>(removedTrackCount))
        + " Hermes track(s) and " + juce::String(static_cast<int>(removedNoteCount)) + " note(s).";
    statusLabel_.setText(status, juce::dontSendNotification);
    app::AppLogger::log(status);
}

void MainComponent::runRedo()
{
    if (isHermesJobRunning()) {
        statusLabel_.setText("Wait for Hermes processing to finish before redo.", juce::dontSendNotification);
        return;
    }

    if (!canRedo()) {
        statusLabel_.setText("Nothing to redo.", juce::dontSendNotification);
        return;
    }

    if (projectHistory_.canRedo()) {
        const auto label = projectHistory_.redoLabel();
        if (!projectHistory_.redo()) {
            statusLabel_.setText("Redo failed: MIDI edit could not be restored.", juce::dontSendNotification);
            return;
        }

        synchronizeMidiNoteSelectionWithEditableTrack();
        refreshTrackView();
        updateStatusForSelection();
        statusLabel_.setText("Redo " + juce::String(label), juce::dontSendNotification);
        app::AppLogger::log("Redo " + juce::String(label));
        return;
    }

    auto redoResult = redoResult_.value();
    if (!hermes::redoAppliedHermesResult(projectController_, redoResult)) {
        app::AppLogger::log("Redo failed for last Hermes result.");
        statusLabel_.setText("Redo failed: unable to restore Hermes result.", juce::dontSendNotification);
        return;
    }

    if (!redoResult.midiTrackIds.empty()) {
        projectController_.selectTrack(redoResult.midiTrackIds.front());
    } else if (!redoResult.trackIds.empty()) {
        projectController_.selectTrack(redoResult.trackIds.front());
    }

    const auto restoredTrackCount = redoResult.tracks.size();
    const auto restoredNoteCount = hermes::countAppliedHermesNotes(redoResult);

    undoResult_ = std::move(redoResult);
    redoResult_.reset();

    refreshTrackView();
    updateStatusForSelection();

    const auto status = juce::String("Redo restored ") + juce::String(static_cast<int>(restoredTrackCount))
        + " Hermes track(s) and " + juce::String(static_cast<int>(restoredNoteCount))
        + " note(s) without re-running Hermes analysis.";
    statusLabel_.setText(status, juce::dontSendNotification);
    app::AppLogger::log(status);
}

void MainComponent::runResetPanelLayout()
{
    panelLayoutState_ = core::defaultMainPanelLayoutState();
    panelLayoutState_ = core::sanitizeMainPanelLayoutState(panelLayoutState_);
    savePanelLayoutState();
    resized();
    repaint();
    statusLabel_.setText("Panel layout reset to defaults.", juce::dontSendNotification);
}

void MainComponent::runToggleMidiComparison()
{
    if (midiComparisonEnabled_) {
        midiComparisonEnabled_ = false;
        updateVisualWorkspace();
        updateStatusForSelection();
        return;
    }

    const auto pair = selectedMidiComparisonPair();
    if (!pair.has_value()) {
        showValidationError(this, "Select exactly two MIDI tracks to compare.");
        return;
    }

    midiComparisonEnabled_ = true;

    double minBeat = 0.0;
    double maxBeat = 0.0;
    bool hasNotes = false;

    const auto includeTrackBounds = [&minBeat, &maxBeat, &hasNotes](const core::Track& track) {
        for (const auto& note : track.midiNotes) {
            if (!hasNotes) {
                minBeat = note.startBeat;
                maxBeat = note.startBeat + note.durationBeats;
                hasNotes = true;
            } else {
                minBeat = std::min(minBeat, note.startBeat);
                maxBeat = std::max(maxBeat, note.startBeat + note.durationBeats);
            }
        }
    };

    includeTrackBounds(pair->first);
    includeTrackBounds(pair->second);

    if (hasNotes) {
        timelineViewportState_ = core::fitTimelineViewport(minBeat, maxBeat, 0.5, 1.0, 512.0);
        std::vector<core::MidiNote> notes = pair->first.midiNotes;
        notes.insert(notes.end(), pair->second.midiNotes.begin(), pair->second.midiNotes.end());
        pitchViewportState_ = core::fitPitchViewportToNotes(notes, 2.0);
    }

    updateVisualWorkspace();
    updateStatusForSelection();
}

void MainComponent::runClearHermesCache()
{
    if (isHermesJobRunning()) {
        showValidationError(this, "Wait for Hermes processing to finish before clearing cache.");
        return;
    }

    std::string error;
    const auto removedCount = hermes::clearHermesCache(error);
    if (!error.empty()) {
        showHermesOperationMessage(
            this,
            "Hermes Cache",
            juce::String("Cache clear warning: ") + juce::String(error),
            juce::AlertWindow::WarningIcon);
        statusLabel_.setText("Hermes cache clear encountered warnings.", juce::dontSendNotification);
        return;
    }

    statusLabel_.setText(
        "Cleared " + juce::String(static_cast<int>(removedCount)) + " Hermes cache folder(s).",
        juce::dontSendNotification);
}

bool MainComponent::canUndo() const
{
    return projectHistory_.canUndo() || (undoResult_.has_value() && !undoResult_->trackIds.empty());
}

bool MainComponent::canRedo() const
{
    return projectHistory_.canRedo() || (redoResult_.has_value() && !redoResult_->tracks.empty());
}

void MainComponent::runComposerAssistantSettings()
{
    const auto updatedSettings = showComposerAssistantSettingsDialog(this, composerSettings_);
    if (!updatedSettings.has_value()) {
        return;
    }

    const auto validation = composerConnector_.validateSettings(updatedSettings.value());
    if (!validation.ok) {
        showValidationError(this, validation.message);
        return;
    }

    composerSettings_ = updatedSettings.value();
    saveComposerSettings();
    statusLabel_.setText("Composer Assistant connector settings saved.", juce::dontSendNotification);
}

void MainComponent::runComposerAssistantProbe()
{
    const auto result = composerConnector_.probe(composerSettings_);
    showHermesOperationMessage(
        this,
        "Composer Assistant Probe",
        juce::String(result.message),
        result.ok ? juce::AlertWindow::InfoIcon : juce::AlertWindow::WarningIcon);
    statusLabel_.setText(result.message, juce::dontSendNotification);
}

void MainComponent::loadComposerSettings()
{
    composerSettings_ = composerConnector_.defaultSettings();

    if (auto* settings = applicationProperties_.getUserSettings()) {
        composerSettings_.enabled = settings->getBoolValue("composer.enabled", composerSettings_.enabled);
        composerSettings_.host = settings->getValue("composer.host", composerSettings_.host).toStdString();
        composerSettings_.port = settings->getIntValue("composer.port", composerSettings_.port);
        composerSettings_.timeoutMs = settings->getIntValue("composer.timeoutMs", composerSettings_.timeoutMs);
        composerSettings_.requireLoopbackHost = settings->getBoolValue(
            "composer.requireLoopbackHost",
            composerSettings_.requireLoopbackHost);
    }
}

void MainComponent::saveComposerSettings() const
{
    if (auto* settings = applicationProperties_.getUserSettings()) {
        settings->setValue("composer.enabled", composerSettings_.enabled);
        settings->setValue("composer.host", juce::String(composerSettings_.host));
        settings->setValue("composer.port", composerSettings_.port);
        settings->setValue("composer.timeoutMs", composerSettings_.timeoutMs);
        settings->setValue("composer.requireLoopbackHost", composerSettings_.requireLoopbackHost);
        settings->saveIfNeeded();
    }
}

void MainComponent::resetProject()
{
    if (isHermesJobRunning()) {
        statusLabel_.setText("Wait for Hermes processing to finish before creating a new project.", juce::dontSendNotification);
        return;
    }

    stopMidiPlayback();
    projectModel_.clear();
    projectController_.clearSelection();
    midiNoteSelectionState_.clear();
    projectHistory_.clear();
    undoResult_.reset();
    redoResult_.reset();
    refreshTrackView();
    updateStatusForSelection();
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showMessageBox(
        juce::AlertWindow::InfoIcon,
        "About DAWHermes",
        "DAWHermes Milestone 3.4\nNative Windows MIDI editing and synchronized audio-stem audition with embedded Hermes tools.");
}

}  // namespace dawhermes::ui
