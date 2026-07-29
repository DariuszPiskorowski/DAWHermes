#include "hermes/HermesCommandAvailability.h"

#include <filesystem>

#include "core/Utf8Path.h"

namespace dawhermes::hermes {

HermesCommandAvailability getHermesCommandAvailability(
    HermesCommand command,
    const core::ProjectModel& project,
    const core::SelectionState& selection)
{
    if (command == HermesCommand::drumsMapping) {
        return HermesCommandAvailability::enabled;
    }

    const auto& selectedIds = selection.selectedTrackIds();
    if (selectedIds.empty()) {
        switch (command) {
        case HermesCommand::drumsMakeMidiFromWav:
        case HermesCommand::bassMakeRepairMidiFromWav:
            return HermesCommandAvailability::requiresAudioTrack;
        case HermesCommand::setFixBpm:
            return HermesCommandAvailability::requiresMidiTrack;
        case HermesCommand::synchronizeMidiWithWav:
            return HermesCommandAvailability::requiresAudioAndMidi;
        default:
            return HermesCommandAvailability::enabled;
        }
    }

    std::size_t audioCount = 0;
    std::size_t midiCount = 0;
    std::size_t validAudioCount = 0;
    std::size_t validMidiCount = 0;
    const core::Track* primaryTrack = nullptr;

    for (const auto selectedId : selectedIds) {
        const auto* track = project.findTrackById(selectedId);
        if (track == nullptr) {
            continue;
        }

        if (primaryTrack == nullptr || selection.selectedTrackId().value_or(0) == selectedId) {
            primaryTrack = track;
        }

        if (track->type == core::TrackType::audio) {
            ++audioCount;
            std::error_code ec;
            const auto sourcePath = core::pathFromUtf8(track->audioSourcePath);
            if (!track->audioSourcePath.empty()
                && std::filesystem::exists(sourcePath, ec)
                && std::filesystem::is_regular_file(sourcePath, ec)) {
                ++validAudioCount;
            }
        } else if (track->type == core::TrackType::midi) {
            ++midiCount;
            if (!track->midiNotes.empty()) {
                ++validMidiCount;
            }
        }
    }

    if (primaryTrack == nullptr) {
        return HermesCommandAvailability::requiresAudioAndMidi;
    }

    const auto hasValidPair =
        selectedIds.size() == 2 && validAudioCount == 1 && validMidiCount == 1 && audioCount == 1 && midiCount == 1;

    switch (command) {
    case HermesCommand::drumsMakeMidiFromWav:
        if (selectedIds.size() != 1) {
            return HermesCommandAvailability::requiresAudioTrack;
        }

        if (primaryTrack->type != core::TrackType::audio) {
            return HermesCommandAvailability::requiresAudioTrack;
        }

        return primaryTrack->audioSourcePath.empty() ? HermesCommandAvailability::requiresAudioFile
                                              : HermesCommandAvailability::enabled;
    case HermesCommand::bassMakeRepairMidiFromWav:
        return hasValidPair ? HermesCommandAvailability::enabled : HermesCommandAvailability::requiresAudioAndMidi;
    case HermesCommand::setFixBpm:
        if (selectedIds.size() != 1) {
            return HermesCommandAvailability::requiresMidiTrack;
        }

        return primaryTrack->type == core::TrackType::midi ? HermesCommandAvailability::enabled
                                                     : HermesCommandAvailability::requiresMidiTrack;
    case HermesCommand::synchronizeMidiWithWav:
        return hasValidPair ? HermesCommandAvailability::enabled : HermesCommandAvailability::requiresAudioAndMidi;
    case HermesCommand::drumsMapping:
        return HermesCommandAvailability::enabled;
    default:
        return HermesCommandAvailability::requiresAudioAndMidi;
    }
}

std::string describeAvailability(HermesCommand command, HermesCommandAvailability availability)
{
    if (availability == HermesCommandAvailability::enabled) {
        return {};
    }

    switch (availability) {
    case HermesCommandAvailability::requiresAudioTrack:
        return "Requires a selected audio track.";
    case HermesCommandAvailability::requiresAudioFile:
        return "Selected audio track requires an imported WAV file.";
    case HermesCommandAvailability::requiresMidiTrack:
        return "Requires a selected MIDI track.";
    case HermesCommandAvailability::requiresAudioAndMidi:
        if (command == HermesCommand::synchronizeMidiWithWav
            || command == HermesCommand::bassMakeRepairMidiFromWav) {
            return "Select exactly one audio track with WAV and one non-empty MIDI track.";
        }

        return "Required source track selection is not available.";
    case HermesCommandAvailability::enabled:
    default:
        return {};
    }
}

}  // namespace dawhermes::hermes
