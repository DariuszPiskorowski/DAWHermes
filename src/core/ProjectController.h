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

    Track& addTrack(TrackType type, std::string name = {}, std::uint64_t parentTrackId = 0);

    void selectTrack(std::uint64_t trackId);
    void clearSelection();

    bool deleteSelectedTrack();
    bool deleteTrackById(std::uint64_t trackId);

    bool assignAudioSourceToTrack(std::uint64_t trackId, std::string audioSourcePath);
    bool assignAudioSourceToSelectedTrack(std::string audioSourcePath);
    bool replaceMidiNotesOnTrack(std::uint64_t trackId, std::vector<MidiNote> midiNotes);
    bool setGeneratedGroupId(std::uint64_t trackId, std::string groupId);

    bool canDeleteSelectedTrack() const;
    std::optional<std::uint64_t> selectedTrackId() const;

private:
    ProjectModel& project_;
    SelectionState& selection_;
};

}  // namespace dawhermes::core
