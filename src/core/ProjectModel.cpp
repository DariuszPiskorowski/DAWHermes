#include "core/ProjectModel.h"

#include <algorithm>
#include <cstddef>
#include <unordered_set>
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
    track->audioSourceMetadata.reset();
    return true;
}

bool ProjectModel::setAudioSource(
    std::uint64_t id,
    std::string audioSourcePath,
    AudioSourceMetadata metadata)
{
    auto* track = findTrackById(id);
    if (track == nullptr
        || track->type != TrackType::audio
        || audioSourcePath.empty()
        || metadata.sampleRate <= 0.0
        || metadata.channelCount < 1
        || metadata.durationSeconds <= 0.0
        || metadata.frameCount == 0) {
        return false;
    }

    track->audioSourcePath = std::move(audioSourcePath);
    track->audioSourceMetadata = std::move(metadata);
    return true;
}

bool ProjectModel::replaceMidiNotes(std::uint64_t id, std::vector<MidiNote> midiNotes)
{
    auto* track = findTrackById(id);
    if (track == nullptr || track->type != TrackType::midi) {
        return false;
    }

    assignStableMidiNoteIds(midiNotes, id);
    track->midiNotes = std::move(midiNotes);
    return true;
}

bool ProjectModel::appendMidiNote(std::uint64_t id, MidiNote midiNote)
{
    auto* track = findTrackById(id);
    if (track == nullptr || track->type != TrackType::midi) {
        return false;
    }

    std::vector<MidiNote> scratch { midiNote };
    assignStableMidiNoteIds(scratch, id);
    track->midiNotes.push_back(scratch.front());
    return true;
}

bool ProjectModel::setMidiSourceMetadata(std::uint64_t id, MidiSourceMetadata metadata)
{
    auto* track = findTrackById(id);
    if (track == nullptr || track->type != TrackType::midi) {
        return false;
    }

    track->midiSourceMetadata = std::move(metadata);
    return true;
}

bool ProjectModel::clearMidiSourceMetadata(std::uint64_t id)
{
    auto* track = findTrackById(id);
    if (track == nullptr || track->type != TrackType::midi) {
        return false;
    }

    track->midiSourceMetadata.reset();
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

bool ProjectModel::setTrackMuted(std::uint64_t id, bool muted)
{
    auto* track = findTrackById(id);
    if (track == nullptr) {
        return false;
    }

    track->muted = muted;
    return true;
}

bool ProjectModel::setTrackSoloed(std::uint64_t id, bool soloed)
{
    auto* track = findTrackById(id);
    if (track == nullptr) {
        return false;
    }

    track->soloed = soloed;
    return true;
}

bool ProjectModel::setInstrumentAssignment(
    std::uint64_t id,
    InstrumentAssignment assignment)
{
    auto* track = findTrackById(id);
    if (track == nullptr || track->type != TrackType::midi) {
        return false;
    }
    if (assignment.kind == InstrumentKind::vst3
        && (assignment.pluginIdentifier.empty()
            || assignment.pluginName.empty())) {
        return false;
    }
    if (assignment.kind == InstrumentKind::internalSynth) {
        assignment = {};
    }
    track->instrument = std::move(assignment);
    return true;
}

std::uint64_t ProjectModel::allocateMidiNoteId()
{
    while (nextMidiNoteId_ == 0 || isMidiNoteIdInUse(nextMidiNoteId_)) {
        ++nextMidiNoteId_;
    }

    const auto allocated = nextMidiNoteId_;
    ++nextMidiNoteId_;
    return allocated;
}

bool ProjectModel::repairMidiNoteIds(std::uint64_t trackId)
{
    auto* track = findTrackById(trackId);
    if (track == nullptr || track->type != TrackType::midi) {
        return false;
    }

    const auto before = track->midiNotes;
    assignStableMidiNoteIds(track->midiNotes, trackId);
    return before != track->midiNotes;
}

bool ProjectModel::repairAllMidiNoteIds()
{
    bool changed = false;
    for (auto& track : tracks_) {
        if (track.type != TrackType::midi) {
            continue;
        }

        const auto before = track.midiNotes;
        assignStableMidiNoteIds(track.midiNotes, track.id);
        if (before != track.midiNotes) {
            changed = true;
        }
    }

    return changed;
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
    nextTrackId_ = 1;
    nextMidiNoteId_ = 1;
    nextAudioTrackNumber_ = 1;
    nextMidiTrackNumber_ = 1;
    nextGroupTrackNumber_ = 1;
}

void ProjectModel::assignStableMidiNoteIds(std::vector<MidiNote>& notes, std::optional<std::uint64_t> exemptTrackId)
{
    std::unordered_set<std::uint64_t> localIds;
    localIds.reserve(notes.size());

    for (auto& note : notes) {
        const auto invalidOrDuplicate = note.id == 0
            || localIds.find(note.id) != localIds.end()
            || isMidiNoteIdInUse(note.id, exemptTrackId);

        if (invalidOrDuplicate) {
            note.id = allocateMidiNoteId();
        } else {
            nextMidiNoteId_ = std::max(nextMidiNoteId_, note.id + 1);
        }

        localIds.insert(note.id);
    }
}

bool ProjectModel::isMidiNoteIdInUse(std::uint64_t noteId, std::optional<std::uint64_t> exemptTrackId) const
{
    if (noteId == 0) {
        return false;
    }

    for (const auto& track : tracks_) {
        if (track.type != TrackType::midi) {
            continue;
        }

        if (exemptTrackId.has_value() && exemptTrackId.value() == track.id) {
            continue;
        }

        const auto existing = std::find_if(track.midiNotes.begin(), track.midiNotes.end(), [noteId](const MidiNote& note) {
            return note.id == noteId;
        });

        if (existing != track.midiNotes.end()) {
            return true;
        }
    }

    return false;
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
