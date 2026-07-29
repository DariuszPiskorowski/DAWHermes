#include "core/TrackRouting.h"

#include <algorithm>

namespace dawhermes::core {

namespace {

template <typename Predicate>
bool anyAncestorMatches(
    const ProjectModel& project,
    const Track& track,
    Predicate predicate) noexcept
{
    auto parentId = track.parentTrackId;
    std::size_t guard = 0;
    while (parentId != 0 && guard < project.tracks().size()) {
        const auto* parent = project.findTrackById(parentId);
        if (parent == nullptr || parent->id == parent->parentTrackId) {
            return false;
        }
        if (predicate(*parent)) {
            return true;
        }
        parentId = parent->parentTrackId;
        ++guard;
    }
    return false;
}

}  // namespace

bool ProjectRoutingState::isAudible(std::uint64_t trackId) const noexcept
{
    return std::binary_search(
        audibleTrackIds.begin(),
        audibleTrackIds.end(),
        trackId);
}

bool hasMutedAncestor(
    const ProjectModel& project,
    const Track& track) noexcept
{
    return anyAncestorMatches(
        project,
        track,
        [](const Track& ancestor) { return ancestor.muted; });
}

bool hasSoloedAncestor(
    const ProjectModel& project,
    const Track& track) noexcept
{
    return anyAncestorMatches(
        project,
        track,
        [](const Track& ancestor) { return ancestor.soloed; });
}

bool projectHasSolo(const ProjectModel& project) noexcept
{
    return std::any_of(
        project.tracks().begin(),
        project.tracks().end(),
        [](const Track& track) { return track.soloed; });
}

bool isTrackEffectivelyAudible(
    const ProjectModel& project,
    const Track& track) noexcept
{
    if (track.type == TrackType::group
        || track.muted
        || hasMutedAncestor(project, track)) {
        return false;
    }

    if (!projectHasSolo(project)) {
        return true;
    }

    return track.soloed || hasSoloedAncestor(project, track);
}

ProjectRoutingState createProjectRoutingState(
    const ProjectModel& project)
{
    ProjectRoutingState state;
    state.anySolo = projectHasSolo(project);
    for (const auto& track : project.tracks()) {
        if (isTrackEffectivelyAudible(project, track)) {
            state.audibleTrackIds.push_back(track.id);
        }
    }
    std::sort(
        state.audibleTrackIds.begin(),
        state.audibleTrackIds.end());
    return state;
}

}  // namespace dawhermes::core
