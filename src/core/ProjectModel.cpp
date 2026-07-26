#include "core/ProjectModel.h"

#include <algorithm>
#include <utility>

namespace dawhermes::core {

Track& ProjectModel::addTrack(TrackType type, std::string name)
{
    if (name.empty()) {
        if (type == TrackType::audio) {
            name = "Audio Track " + std::to_string(nextAudioTrackNumber_++);
        } else {
            name = "MIDI Track " + std::to_string(nextMidiTrackNumber_++);
        }
    }

    tracks_.push_back(Track { nextTrackId_++, std::move(name), type });
    return tracks_.back();
}

bool ProjectModel::removeTrackById(std::uint64_t id)
{
    const auto previousSize = tracks_.size();
    tracks_.erase(
        std::remove_if(tracks_.begin(), tracks_.end(), [id](const Track& track) { return track.id == id; }),
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

}  // namespace dawhermes::core
