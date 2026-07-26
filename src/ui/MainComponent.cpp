#include "ui/MainComponent.h"

#include <exception>

#include "app/AppLogger.h"
#include "hermes/HermesCommandAvailability.h"
#include "hermes/HermesValidation.h"
#include "ui/HermesDialogs.h"

namespace dawhermes::ui {

namespace {

juce::String trackTypeLabel(core::TrackType type)
{
    return type == core::TrackType::audio ? "Audio" : "MIDI";
}

void addHermesMenuItem(
    juce::PopupMenu& menu,
    int itemId,
    juce::String title,
    hermes::HermesCommand command,
    const core::ProjectModel& project,
    const core::SelectionState& selection)
{
    const auto availability = hermes::getHermesCommandAvailability(command, project, selection);
    const auto reason = hermes::describeAvailability(command, availability);
    const auto enabled = availability == hermes::HermesCommandAvailability::enabled;

    if (!enabled && !reason.empty()) {
        title << " (" << reason << ")";
    }

    menu.addItem(itemId, title, enabled);
}

}  // namespace

MainComponent::MainComponent(hermes::IHermesEngine& hermesEngine)
    : hermesEngine_(hermesEngine),
      projectController_(projectModel_, selectionState_),
      menuBar_(this)
{
    setOpaque(true);

    addAndMakeVisible(menuBar_);

    transportLabel_.setText(
        "Transport (Milestone 0 placeholder)  |  Play / Stop / Record unavailable",
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

    updateStatusForSelection();
}

MainComponent::~MainComponent()
{
    trackList_.setModel(nullptr);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(juce::Colour(0xff15181d));
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(6);

    menuBar_.setBounds(area.removeFromTop(28));

    auto transportArea = area.removeFromTop(40);
    transportLabel_.setBounds(transportArea.reduced(0, 4));

    statusLabel_.setBounds(area.removeFromBottom(24));

    auto rightArea = area.removeFromRight(280).reduced(4);
    aiAssistantLabel_.setBounds(rightArea);

    auto leftArea = area.removeFromLeft(260).reduced(4);
    tracksHeaderLabel_.setBounds(leftArea.removeFromTop(28));
    trackList_.setBounds(leftArea);

    auto bottomCenter = area.removeFromBottom(180).reduced(4);
    midiEditorLabel_.setBounds(bottomCenter);

    timelineLabel_.setBounds(area.reduced(4));
}

juce::StringArray MainComponent::getMenuBarNames()
{
    return { "File", "Edit", "Track", "Tools", "Help" };
}

juce::PopupMenu MainComponent::getMenuForIndex(int topLevelMenuIndex, const juce::String&)
{
    juce::PopupMenu menu;

    switch (topLevelMenuIndex) {
    case 0:
        menu.addItem(commandNewProject, "New Project");
        menu.addSeparator();
        menu.addItem(commandExit, "Exit");
        break;
    case 1:
        menu.addItem(commandUndo, "Undo", false);
        menu.addItem(commandRedo, "Redo", false);
        break;
    case 2:
        menu.addItem(commandAddAudioTrack, "Add Audio Track");
        menu.addItem(commandAddMidiTrack, "Add MIDI Track");
        menu.addSeparator();
        menu.addItem(commandDeleteSelectedTrack, "Delete Selected Track", projectController_.canDeleteSelectedTrack());
        break;
    case 3:
        menu.addSubMenu("Hermes", buildHermesMenu());
        break;
    case 4:
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

    g.fillAll(selected ? juce::Colour(0xff2f5f9a)
                       : ((rowNumber % 2 == 0) ? juce::Colour(0xff252a31) : juce::Colour(0xff21262d)));

    g.setColour(juce::Colours::white);
    g.drawText(
        juce::String(track.name) + "  [" + trackTypeLabel(track.type) + "]",
        8,
        0,
        width - 16,
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

    projectController_.selectTrack(track.id);
    refreshTrackView();
    updateStatusForSelection();

    if (event.mods.isPopupMenu()) {
        showTrackContextMenu();
    }
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
        case commandDeleteSelectedTrack:
            deleteSelectedTrack();
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

juce::PopupMenu MainComponent::buildHermesMenu() const
{
    juce::PopupMenu hermesMenu;

    juce::PopupMenu drumsSubMenu;
    addHermesMenuItem(
        drumsSubMenu,
        commandHermesDrumsMakeMidi,
        "Make MIDI from WAV...",
        hermes::HermesCommand::drumsMakeMidiFromWav,
        projectModel_,
        selectionState_);
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
        "Make / Repair MIDI from WAV...",
        hermes::HermesCommand::bassMakeRepairMidiFromWav,
        projectModel_,
        selectionState_);

    hermesMenu.addSubMenu("Drums", drumsSubMenu);
    hermesMenu.addSubMenu("Bass", bassSubMenu);

    addHermesMenuItem(
        hermesMenu,
        commandHermesSynchronize,
        "Synchronize MIDI with WAV...",
        hermes::HermesCommand::synchronizeMidiWithWav,
        projectModel_,
        selectionState_);

    addHermesMenuItem(
        hermesMenu,
        commandHermesSetFixBpm,
        "Set / Fix BPM...",
        hermes::HermesCommand::setFixBpm,
        projectModel_,
        selectionState_);

    return hermesMenu;
}

juce::PopupMenu MainComponent::buildTrackContextMenu() const
{
    juce::PopupMenu menu;
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

std::optional<hermes::HermesTrackContext> MainComponent::selectedTrackContext() const
{
    const auto track = selectedTrack();
    if (!track.has_value()) {
        return std::nullopt;
    }

    return hermes::HermesTrackContext { track->id, track->name, track->type };
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
    auto status = juce::String("No track selected.");
    if (const auto track = selectedTrack(); track.has_value()) {
        status = "Selected: " + juce::String(track->name) + " (" + trackTypeLabel(track->type) + ")";
    }

    const auto syncAvailability = hermes::getHermesCommandAvailability(
        hermes::HermesCommand::synchronizeMidiWithWav,
        projectModel_,
        selectionState_);
    const auto syncReason = hermes::describeAvailability(
        hermes::HermesCommand::synchronizeMidiWithWav,
        syncAvailability);
    if (!syncReason.empty()) {
        status << "  |  " << syncReason;
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

void MainComponent::deleteSelectedTrack()
{
    if (!projectController_.deleteSelectedTrack()) {
        statusLabel_.setText("No selected track to delete.", juce::dontSendNotification);
        return;
    }

    refreshTrackView();
    updateStatusForSelection();
}

void MainComponent::runHermesDrumsMakeMidi()
{
    app::AppLogger::log("Hermes command selected: Drums -> Make MIDI from WAV");

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

    const auto result = hermesEngine_.drumsMakeMidiFromWav(context.value(), options.value());
    showMilestoneNotIntegratedMessage(this, result.message);
    statusLabel_.setText(result.message, juce::dontSendNotification);
}

void MainComponent::runHermesDrumMapping()
{
    app::AppLogger::log("Hermes command selected: Drums -> Drum Mapping");

    if (showDrumMappingDialog(this)) {
        juce::AlertWindow::showMessageBox(
            juce::AlertWindow::InfoIcon,
            "DAWHermes",
            "Drum mapping was applied to the Milestone 0 UI shell only. Processing integration is pending.");
    }
}

void MainComponent::runHermesBassRepair()
{
    app::AppLogger::log("Hermes command selected: Bass -> Make / Repair MIDI from WAV");

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
        return;
    }

    const auto context = selectedTrackContext();
    if (!context.has_value()) {
        showValidationError(this, "No selected track context available.");
        return;
    }

    if (!showBassRepairDialog(this, context.value())) {
        return;
    }

    const auto result = hermesEngine_.bassMakeOrRepairMidiFromWav(context.value(), hermes::HermesBassOptions {});
    showMilestoneNotIntegratedMessage(this, result.message);
    statusLabel_.setText(result.message, juce::dontSendNotification);
}

void MainComponent::runHermesSynchronize()
{
    app::AppLogger::log("Hermes command selected: Synchronize MIDI with WAV");

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
        return;
    }

    const auto context = selectedTrackContext();
    if (!context.has_value()) {
        showValidationError(this, "No selected track context available.");
        return;
    }

    if (!showSynchronizeDialog(this, context.value())) {
        return;
    }

    const auto result = hermesEngine_.synchronizeMidiWithWav(context.value(), hermes::HermesSyncOptions {});
    showMilestoneNotIntegratedMessage(this, result.message);
    statusLabel_.setText(result.message, juce::dontSendNotification);
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

    const auto result = hermesEngine_.setOrFixBpm(context.value(), bpmOptions.value());
    showMilestoneNotIntegratedMessage(this, result.message);
    statusLabel_.setText(result.message, juce::dontSendNotification);
}

void MainComponent::resetProject()
{
    projectModel_.clear();
    projectController_.clearSelection();
    refreshTrackView();
    updateStatusForSelection();
}

void MainComponent::showAbout()
{
    juce::AlertWindow::showMessageBox(
        juce::AlertWindow::InfoIcon,
        "About DAWHermes",
        "DAWHermes Milestone 0\nNative Windows DAW foundation with Hermes UI shell.");
}

}  // namespace dawhermes::ui
