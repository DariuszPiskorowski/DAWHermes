#include "core/ProjectController.h"

namespace dawhermes::core {

ProjectController::ProjectController(ProjectModel& project, SelectionState& selection)
    : project_(project), selection_(selection)
{
}

Track& ProjectController::addTrack(TrackType type, std::string name)
{
    return project_.addTrack(type, std::move(name));
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
