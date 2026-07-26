#include "hermes/HermesCommandAvailability.h"

namespace dawhermes::hermes {

HermesCommandAvailability getHermesCommandAvailability(
    HermesCommand command,
    const core::ProjectModel& project,
    const core::SelectionState& selection)
{
    if (command == HermesCommand::drumsMapping) {
        return HermesCommandAvailability::enabled;
    }

    const auto selectedId = selection.selectedTrackId();
    if (!selectedId.has_value()) {
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

    const auto* track = project.findTrackById(selectedId.value());
    if (track == nullptr) {
        return HermesCommandAvailability::requiresAudioAndMidi;
    }

    switch (command) {
    case HermesCommand::drumsMakeMidiFromWav:
    case HermesCommand::bassMakeRepairMidiFromWav:
        if (track->type != core::TrackType::audio) {
            return HermesCommandAvailability::requiresAudioTrack;
        }

        return track->audioSourcePath.empty() ? HermesCommandAvailability::requiresAudioFile
                                              : HermesCommandAvailability::enabled;
    case HermesCommand::setFixBpm:
        return track->type == core::TrackType::midi ? HermesCommandAvailability::enabled
                                                     : HermesCommandAvailability::requiresMidiTrack;
    case HermesCommand::synchronizeMidiWithWav:
        return HermesCommandAvailability::requiresAudioAndMidi;
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
        return "Selected audio track requires an assigned WAV source file.";
    case HermesCommandAvailability::requiresMidiTrack:
        return "Requires a selected MIDI track.";
    case HermesCommandAvailability::requiresAudioAndMidi:
        if (command == HermesCommand::synchronizeMidiWithWav) {
            return "Requires both audio and MIDI selections; multi-selection is not available in Milestone 1.";
        }

        return "Required source track selection is not available.";
    case HermesCommandAvailability::enabled:
    default:
        return {};
    }
}

}  // namespace dawhermes::hermes
