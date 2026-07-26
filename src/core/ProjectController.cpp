#include "core/ProjectController.h"

#include <utility>

namespace dawhermes::core {

ProjectController::ProjectController(ProjectModel& project, SelectionState& selection)
    : project_(project), selection_(selection)
{
}

Track& ProjectController::addTrack(TrackType type, std::string name, std::uint64_t parentTrackId)
{
    return project_.addTrack(type, std::move(name), parentTrackId);
}

void ProjectController::selectTrack(std::uint64_t trackId)
{
    if (project_.findTrackById(trackId) != nullptr) {
        selection_.selectTrack(trackId);
    }
}

void ProjectController::clearSelection()
{
    selection_.clear();
}

bool ProjectController::deleteSelectedTrack()
{
    if (!selection_.hasSelection()) {
        return false;
    }

    const auto trackId = selection_.selectedTrackId().value();
    const bool removed = project_.removeTrackById(trackId);
    if (removed) {
        selection_.clear();
    }

    return removed;
}

bool ProjectController::deleteTrackById(std::uint64_t trackId)
{
    const bool removed = project_.removeTrackById(trackId);
    if (removed && selection_.isSelected(trackId)) {
        selection_.clear();
    }

    return removed;
}

bool ProjectController::assignAudioSourceToTrack(std::uint64_t trackId, std::string audioSourcePath)
{
    return project_.setAudioSourcePath(trackId, std::move(audioSourcePath));
}

bool ProjectController::assignAudioSourceToSelectedTrack(std::string audioSourcePath)
{
    if (!selection_.hasSelection()) {
        return false;
    }

    return assignAudioSourceToTrack(selection_.selectedTrackId().value(), std::move(audioSourcePath));
}

bool ProjectController::replaceMidiNotesOnTrack(std::uint64_t trackId, std::vector<MidiNote> midiNotes)
{
    return project_.replaceMidiNotes(trackId, std::move(midiNotes));
}

bool ProjectController::setGeneratedGroupId(std::uint64_t trackId, std::string groupId)
{
    return project_.setGeneratedGroupId(trackId, std::move(groupId));
}

bool ProjectController::canDeleteSelectedTrack() const
{
    if (!selection_.hasSelection()) {
        return false;
    }

    return project_.findTrackById(selection_.selectedTrackId().value()) != nullptr;
}

std::optional<std::uint64_t> ProjectController::selectedTrackId() const
{
    return selection_.selectedTrackId();
}

}  // namespace dawhermes::core
