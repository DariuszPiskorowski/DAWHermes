#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/ProjectModel.h"
#include "core/SelectionState.h"

namespace dawhermes::core {

class ProjectController {
public:
    ProjectController(ProjectModel& project, SelectionState& selection);

    Track& addTrack(TrackType type, std::string name = {}, std::uint64_t parentTrackId = 0);

    void selectTrack(std::uint64_t trackId);
    void toggleTrackSelection(std::uint64_t trackId);
    void clearSelection();

    bool deleteSelectedTrack();
    bool deleteTrackById(std::uint64_t trackId);

    bool replaceMidiNotesOnTrack(std::uint64_t trackId, std::vector<MidiNote> midiNotes);
    bool setMidiSourceMetadata(std::uint64_t trackId, MidiSourceMetadata metadata);
    bool clearMidiSourceMetadata(std::uint64_t trackId);
    bool setGeneratedGroupId(std::uint64_t trackId, std::string groupId);

    bool canDeleteSelectedTrack() const;
    std::optional<std::uint64_t> selectedTrackId() const;
    const std::vector<std::uint64_t>& selectedTrackIds() const;

private:
    ProjectModel& project_;
    SelectionState& selection_;
};

}  // namespace dawhermes::core
