#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "core/ProjectHistory.h"
#include "core/ProjectModel.h"
#include "core/SelectionState.h"

namespace dawhermes::core {

struct PreparedAudioTrackImport {
    std::string trackName;
    std::string sourcePath;
    AudioSourceMetadata metadata;
};

class ImportAudioTracksCommand final : public ProjectEditCommand {
public:
    ImportAudioTracksCommand(
        ProjectModel& project,
        SelectionState& selection,
        std::vector<PreparedAudioTrackImport> imports);

    std::string label() const override;
    bool undo() override;
    bool redo() override;

    const std::vector<std::uint64_t>& createdTrackIds() const noexcept;

private:
    void restoreSelection(const std::vector<std::uint64_t>& trackIds);

    ProjectModel& project_;
    SelectionState& selection_;
    std::vector<PreparedAudioTrackImport> imports_;
    std::vector<std::uint64_t> previousSelection_;
    std::vector<std::uint64_t> createdTrackIds_;
    bool capturedPreviousSelection_ { false };
};

}  // namespace dawhermes::core
