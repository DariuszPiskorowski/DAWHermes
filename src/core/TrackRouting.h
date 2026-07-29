#pragma once

#include <cstdint>
#include <vector>

#include "core/ProjectModel.h"

namespace dawhermes::core {

struct ProjectRoutingState {
    std::vector<std::uint64_t> audibleTrackIds;
    bool anySolo { false };

    bool isAudible(std::uint64_t trackId) const noexcept;
};

bool hasMutedAncestor(
    const ProjectModel& project,
    const Track& track) noexcept;
bool hasSoloedAncestor(
    const ProjectModel& project,
    const Track& track) noexcept;
bool projectHasSolo(const ProjectModel& project) noexcept;
bool isTrackEffectivelyAudible(
    const ProjectModel& project,
    const Track& track) noexcept;
ProjectRoutingState createProjectRoutingState(
    const ProjectModel& project);

}  // namespace dawhermes::core
