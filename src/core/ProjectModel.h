#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/Track.h"

namespace dawhermes::core {

class ProjectModel {
public:
    Track& addTrack(TrackType type, std::string name = {});
    bool removeTrackById(std::uint64_t id);
    bool setAudioSourcePath(std::uint64_t id, std::string audioSourcePath);
    bool replaceMidiNotes(std::uint64_t id, std::vector<MidiNote> midiNotes);
    bool setGeneratedGroupId(std::uint64_t id, std::string groupId);

    Track* findTrackById(std::uint64_t id);
    const Track* findTrackById(std::uint64_t id) const;

    const std::vector<Track>& tracks() const noexcept;
    bool empty() const noexcept;
    void clear();

private:
    std::vector<Track> tracks_;
    std::uint64_t nextTrackId_ { 1 };
    std::uint64_t nextAudioTrackNumber_ { 1 };
    std::uint64_t nextMidiTrackNumber_ { 1 };
};

}  // namespace dawhermes::core
