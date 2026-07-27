#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Track.h"

namespace dawhermes::core {

class ProjectModel {
public:
    Track& addTrack(TrackType type, std::string name = {}, std::uint64_t parentTrackId = 0);
    bool removeTrackById(std::uint64_t id);
    bool setAudioSourcePath(std::uint64_t id, std::string audioSourcePath);
    bool replaceMidiNotes(std::uint64_t id, std::vector<MidiNote> midiNotes);
    bool appendMidiNote(std::uint64_t id, MidiNote midiNote);
    bool setMidiSourceMetadata(std::uint64_t id, MidiSourceMetadata metadata);
    bool clearMidiSourceMetadata(std::uint64_t id);
    bool setGeneratedGroupId(std::uint64_t id, std::string groupId);

    std::uint64_t allocateMidiNoteId();
    bool repairMidiNoteIds(std::uint64_t trackId);
    bool repairAllMidiNoteIds();

    Track* findTrackById(std::uint64_t id);
    const Track* findTrackById(std::uint64_t id) const;

    const std::vector<Track>& tracks() const noexcept;
    bool empty() const noexcept;
    void clear();

private:
    void assignStableMidiNoteIds(std::vector<MidiNote>& notes, std::optional<std::uint64_t> exemptTrackId = std::nullopt);
    bool isMidiNoteIdInUse(std::uint64_t noteId, std::optional<std::uint64_t> exemptTrackId = std::nullopt) const;
    bool isDescendantOf(const Track& track, std::uint64_t ancestorTrackId) const;
    std::size_t insertionIndexForParent(std::uint64_t parentTrackId) const;

    std::vector<Track> tracks_;
    std::uint64_t nextTrackId_ { 1 };
    std::uint64_t nextMidiNoteId_ { 1 };
    std::uint64_t nextAudioTrackNumber_ { 1 };
    std::uint64_t nextMidiTrackNumber_ { 1 };
    std::uint64_t nextGroupTrackNumber_ { 1 };
};

}  // namespace dawhermes::core
