#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "core/ProjectModel.h"
#include "core/SelectionState.h"

namespace dawhermes::core {

class ProjectController {
public:
    ProjectController(ProjectModel& project, SelectionState& selection);

    Track& addTrack(TrackType type, std::string name = {});

    void selectTrack(std::uint64_t trackId);
    void clearSelection();

    bool deleteSelectedTrack();
    bool deleteTrackById(std::uint64_t trackId);

    bool canDeleteSelectedTrack() const;
    std::optional<std::uint64_t> selectedTrackId() const;

private:
    ProjectModel& project_;
    SelectionState& selection_;
};

}  // namespace dawhermes::core
