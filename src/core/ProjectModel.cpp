#include "core/ProjectModel.h"

#include <algorithm>
#include <cstddef>
#include <utility>

namespace dawhermes::core {

Track& ProjectModel::addTrack(TrackType type, std::string name, std::uint64_t parentTrackId)
{
    if (name.empty()) {
        if (type == TrackType::audio) {
            name = "Audio Track " + std::to_string(nextAudioTrackNumber_++);
        } else if (type == TrackType::midi) {
            name = "MIDI Track " + std::to_string(nextMidiTrackNumber_++);
        } else {
            name = "Group " + std::to_string(nextGroupTrackNumber_++);
        }
    }

    if (parentTrackId != 0) {
        const auto* parent = findTrackById(parentTrackId);
        if (parent == nullptr || parent->type != TrackType::group) {
            parentTrackId = 0;
        }
    }

    Track newTrack { nextTrackId_++, std::move(name), type, parentTrackId };

    if (parentTrackId == 0) {
        tracks_.push_back(std::move(newTrack));
        return tracks_.back();
    }

    const auto insertionIndex = insertionIndexForParent(parentTrackId);
    const auto insertIt = tracks_.begin() + static_cast<std::ptrdiff_t>(insertionIndex);
    auto inserted = tracks_.insert(insertIt, std::move(newTrack));
    return *inserted;
}

bool ProjectModel::removeTrackById(std::uint64_t id)
{
    const auto* existing = findTrackById(id);
    if (existing == nullptr) {
        return false;
    }

    const auto previousSize = tracks_.size();
    tracks_.erase(std::remove_if(
                     tracks_.begin(),
                     tracks_.end(),
                     [this, id](const Track& track) {
                         return track.id == id || isDescendantOf(track, id);
                     }),
                 tracks_.end());

    return tracks_.size() != previousSize;
}

bool ProjectModel::setAudioSourcePath(std::uint64_t id, std::string audioSourcePath)
{
    auto* track = findTrackById(id);
    if (track == nullptr || track->type != TrackType::audio) {
        return false;
    }

    track->audioSourcePath = std::move(audioSourcePath);
    return true;
}

bool ProjectModel::replaceMidiNotes(std::uint64_t id, std::vector<MidiNote> midiNotes)
{
    auto* track = findTrackById(id);
    if (track == nullptr || track->type != TrackType::midi) {
        return false;
    }

    track->midiNotes = std::move(midiNotes);
    return true;
}

bool ProjectModel::setGeneratedGroupId(std::uint64_t id, std::string groupId)
{
    auto* track = findTrackById(id);
    if (track == nullptr) {
        return false;
    }

    track->generatedGroupId = std::move(groupId);
    return true;
}

Track* ProjectModel::findTrackById(std::uint64_t id)
{
    const auto it = std::find_if(
        tracks_.begin(), tracks_.end(), [id](const Track& track) { return track.id == id; });

    if (it == tracks_.end()) {
        return nullptr;
    }

    return &(*it);
}

const Track* ProjectModel::findTrackById(std::uint64_t id) const
{
    const auto it = std::find_if(
        tracks_.begin(), tracks_.end(), [id](const Track& track) { return track.id == id; });

    if (it == tracks_.end()) {
        return nullptr;
    }

    return &(*it);
}

const std::vector<Track>& ProjectModel::tracks() const noexcept
{
    return tracks_;
}

bool ProjectModel::empty() const noexcept
{
    return tracks_.empty();
}

void ProjectModel::clear()
{
    tracks_.clear();
}

bool ProjectModel::isDescendantOf(const Track& track, std::uint64_t ancestorTrackId) const
{
    if (ancestorTrackId == 0 || track.parentTrackId == 0) {
        return false;
    }

    auto parentId = track.parentTrackId;
    std::size_t loopGuard = 0;
    while (parentId != 0 && loopGuard < tracks_.size()) {
        if (parentId == ancestorTrackId) {
            return true;
        }

        const auto* parentTrack = findTrackById(parentId);
        if (parentTrack == nullptr || parentTrack->parentTrackId == parentId) {
            return false;
        }

        parentId = parentTrack->parentTrackId;
        ++loopGuard;
    }

    return false;
}

std::size_t ProjectModel::insertionIndexForParent(std::uint64_t parentTrackId) const
{
    const auto it = std::find_if(
        tracks_.begin(), tracks_.end(), [parentTrackId](const Track& track) { return track.id == parentTrackId; });

    if (it == tracks_.end()) {
        return tracks_.size();
    }

    std::size_t index = static_cast<std::size_t>(std::distance(tracks_.begin(), it)) + 1;
    while (index < tracks_.size()) {
        if (!isDescendantOf(tracks_.at(index), parentTrackId)) {
            break;
        }

        ++index;
    }

    return index;
}

}  // namespace dawhermes::core
