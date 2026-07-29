#include "core/AudioTrackImport.h"

#include <algorithm>
#include <utility>

namespace dawhermes::core {

namespace {

bool isValidImport(const PreparedAudioTrackImport& import)
{
    return !import.trackName.empty()
        && !import.sourcePath.empty()
        && import.metadata.sampleRate > 0.0
        && import.metadata.channelCount >= 1
        && import.metadata.channelCount <= 2
        && import.metadata.durationSeconds > 0.0
        && import.metadata.frameCount > 0;
}

}  // namespace

ImportAudioTracksCommand::ImportAudioTracksCommand(
    ProjectModel& project,
    SelectionState& selection,
    std::vector<PreparedAudioTrackImport> imports)
    : project_(project),
      selection_(selection),
      imports_(std::move(imports))
{
}

std::string ImportAudioTracksCommand::label() const
{
    return imports_.size() == 1
        ? "Import Audio Track"
        : "Import Audio Tracks";
}

bool ImportAudioTracksCommand::undo()
{
    if (createdTrackIds_.empty()
        || std::any_of(
            createdTrackIds_.begin(),
            createdTrackIds_.end(),
            [this](std::uint64_t trackId) {
                const auto* track = project_.findTrackById(trackId);
                return track == nullptr || track->type != TrackType::audio;
            })) {
        return false;
    }

    for (auto it = createdTrackIds_.rbegin(); it != createdTrackIds_.rend(); ++it) {
        if (!project_.removeTrackById(*it)) {
            return false;
        }
    }

    createdTrackIds_.clear();
    restoreSelection(previousSelection_);
    return true;
}

bool ImportAudioTracksCommand::redo()
{
    if (imports_.empty()
        || !createdTrackIds_.empty()
        || !std::all_of(imports_.begin(), imports_.end(), isValidImport)) {
        return false;
    }

    if (!capturedPreviousSelection_) {
        previousSelection_ = selection_.selectedTrackIds();
        capturedPreviousSelection_ = true;
    }

    for (const auto& import : imports_) {
        const auto trackId = project_.addTrack(TrackType::audio, import.trackName).id;
        if (!project_.setAudioSource(trackId, import.sourcePath, import.metadata)) {
            project_.removeTrackById(trackId);
            for (auto it = createdTrackIds_.rbegin(); it != createdTrackIds_.rend(); ++it) {
                project_.removeTrackById(*it);
            }
            createdTrackIds_.clear();
            restoreSelection(previousSelection_);
            return false;
        }
        createdTrackIds_.push_back(trackId);
    }

    restoreSelection(createdTrackIds_);
    return true;
}

const std::vector<std::uint64_t>& ImportAudioTracksCommand::createdTrackIds() const noexcept
{
    return createdTrackIds_;
}

void ImportAudioTracksCommand::restoreSelection(
    const std::vector<std::uint64_t>& trackIds)
{
    selection_.clear();
    for (const auto trackId : trackIds) {
        if (project_.findTrackById(trackId) != nullptr) {
            selection_.toggleTrack(trackId);
        }
    }
}

}  // namespace dawhermes::core
