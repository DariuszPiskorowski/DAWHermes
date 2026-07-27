#include "ui/MainComponent.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <utility>

#include "app/AppLogger.h"
#include "hermes/HermesCache.h"
#include "hermes/HermesCommandAvailability.h"
#include "hermes/HermesValidation.h"
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

    addAndMakeVisible(menuBar_);

    transportLabel_.setText(
        "Transport (Milestone 1 placeholder)  |  Play / Stop / Record unavailable",
        juce::dontSendNotification);
    transportLabel_.setJustificationType(juce::Justification::centredLeft);
    transportLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff2b323b));
    transportLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(transportLabel_);

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

    timelineLabel_.setText("Timeline / Work Area", juce::dontSendNotification);
    timelineLabel_.setJustificationType(juce::Justification::centred);
    timelineLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff20252b));
    timelineLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(timelineLabel_);

    midiEditorLabel_.setText("MIDI Editor", juce::dontSendNotification);
    midiEditorLabel_.setJustificationType(juce::Justification::centred);
    midiEditorLabel_.setColour(juce::Label::backgroundColourId, juce::Colour(0xff1d2127));
    midiEditorLabel_.setColour(juce::Label::textColourId, juce::Colours::white);
    addAndMakeVisible(midiEditorLabel_);

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

    loadPanelLayoutState();
    loadComposerSettings();
    cleanupStaleHermesCacheOnStartup();
    updateStatusForSelection();
}

MainComponent::~MainComponent()
{
    if (hermesJobRunner_ != nullptr) {
        hermesJobRunner_->stop();
    }

    savePanelLayoutState();
    trackList_.setModel(nullptr);
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
    tracksHeaderLabel_.setBounds(
        layout.tracksHeader.x,
        layout.tracksHeader.y,
        layout.tracksHeader.width,
        layout.tracksHeader.height);
    trackList_.setBounds(layout.trackList.x, layout.trackList.y, layout.trackList.width, layout.trackList.height);
    timelineLabel_.setBounds(layout.timeline.x, layout.timeline.y, layout.timeline.width, layout.timeline.height);
    aiAssistantLabel_.setBounds(
        layout.aiAssistant.x,
        layout.aiAssistant.y,
        layout.aiAssistant.width,
        layout.aiAssistant.height);
    midiEditorLabel_.setBounds(
        layout.midiEditor.x,
        layout.midiEditor.y,
        layout.midiEditor.width,
        layout.midiEditor.height);
    statusLabel_.setBounds(layout.statusBar.x, layout.statusBar.y, layout.statusBar.width, layout.statusBar.height);
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
        menu.addSeparator();
        menu.addItem(commandExit, "Exit");
        break;
    case 1:
        menu.addItem(commandUndo, "Undo", canUndo());
        menu.addItem(commandRedo, "Redo", canRedo());
        break;
    case 2:
        menu.addItem(commandResetPanelLayout, "Reset Panel Layout");
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
        case commandAssignAudioFile:
            assignAudioFileToSelectedTrack();
            break;
        case commandDeleteSelectedTrack:
            deleteSelectedTrack();
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
    menuBar_.repaint();
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
    return undoResult_.has_value() && !undoResult_->trackIds.empty();
}

bool MainComponent::canRedo() const
{
    return redoResult_.has_value() && !redoResult_->tracks.empty();
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

    projectModel_.clear();
    projectController_.clearSelection();
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
        "DAWHermes Milestone 2\nNative Windows DAW shell with embedded Hermes tools.");
}

}  // namespace dawhermes::ui
