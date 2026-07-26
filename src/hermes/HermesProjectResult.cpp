#include "hermes/HermesProjectResult.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <utility>

namespace dawhermes::hermes {

namespace {

std::string buildTargetTrackName(const HermesTrackContext& sourceContext, const HermesGeneratedMidiTrack& generatedTrack)
{
    if (generatedTrack.trackName.empty()) {
        return sourceContext.trackName + " - Drums";
    }

    return sourceContext.trackName + " - " + generatedTrack.trackName;
}

void rollbackCreatedTracks(core::ProjectController& projectController, const std::vector<std::uint64_t>& trackIds)
{
    for (auto it = trackIds.rbegin(); it != trackIds.rend(); ++it) {
        projectController.deleteTrackById(*it);
    }
}

}  // namespace

bool isValidMidiNoteEvent(const core::MidiNote& note, std::string& reason)
{
    if (note.pitch < 0 || note.pitch > 127) {
        reason = "Pitch must be in range 0..127.";
        return false;
    }

    if (note.velocity < 1 || note.velocity > 127) {
        reason = "Velocity must be in range 1..127.";
        return false;
    }

    if (!std::isfinite(note.startBeat) || note.startBeat < 0.0) {
        reason = "Start beat must be finite and >= 0.";
        return false;
    }

    if (!std::isfinite(note.durationBeats) || note.durationBeats <= 0.0) {
        reason = "Duration must be finite and > 0.";
        return false;
    }

    if (note.channel < 1 || note.channel > 16) {
        reason = "MIDI channel must be in range 1..16.";
        return false;
    }

    reason.clear();
    return true;
}

ApplyToProjectResult applyHermesResultToProject(
    const HermesTrackContext& sourceContext,
    const HermesOperationResult& operationResult,
    const std::string& groupId,
    core::ProjectController& projectController,
    AppliedHermesResult& appliedResult)
{
    appliedResult = AppliedHermesResult {};

    if (!operationResult.isSuccess()) {
        return ApplyToProjectResult { false, "Hermes operation did not succeed.", 0, 0 };
    }

    if (operationResult.generatedMidiTracks.empty()) {
        return ApplyToProjectResult {
            false,
            "Embedded Hermes returned success but no MIDI tracks were generated.",
            0,
            0
        };
    }

    std::size_t totalNotes = 0;
    for (const auto& generatedTrack : operationResult.generatedMidiTracks) {
        for (const auto& note : generatedTrack.notes) {
            std::string reason;
            if (!isValidMidiNoteEvent(note, reason)) {
                return ApplyToProjectResult {
                    false,
                    "Generated MIDI note validation failed: " + reason,
                    0,
                    0
                };
            }

            ++totalNotes;
        }
    }

    if (totalNotes == 0) {
        return ApplyToProjectResult {
            false,
            "Embedded Hermes returned success but no MIDI notes were generated.",
            0,
            0
        };
    }

    std::vector<AppliedHermesTrack> createdTrackSnapshots;
    std::vector<std::uint64_t> createdTrackIds;
    createdTrackSnapshots.reserve(operationResult.generatedMidiTracks.size());
    createdTrackIds.reserve(operationResult.generatedMidiTracks.size());

    for (const auto& generatedTrack : operationResult.generatedMidiTracks) {
        const auto displayName = buildTargetTrackName(sourceContext, generatedTrack);
        const auto& midiTrack = projectController.addTrack(core::TrackType::midi, displayName);

        if (!projectController.replaceMidiNotesOnTrack(midiTrack.id, generatedTrack.notes)) {
            rollbackCreatedTracks(projectController, createdTrackIds);
            return ApplyToProjectResult { false, "Failed to insert generated MIDI notes into project model.", 0, 0 };
        }

        if (!projectController.setGeneratedGroupId(midiTrack.id, groupId)) {
            rollbackCreatedTracks(projectController, createdTrackIds);
            return ApplyToProjectResult { false, "Failed to assign Hermes result group metadata.", 0, 0 };
        }

        createdTrackIds.push_back(midiTrack.id);
        createdTrackSnapshots.push_back(AppliedHermesTrack { displayName, generatedTrack.notes });
    }

    appliedResult.groupId = groupId;
    appliedResult.tracks = std::move(createdTrackSnapshots);
    appliedResult.trackIds = std::move(createdTrackIds);

    return ApplyToProjectResult {
        true,
        "Hermes MIDI tracks inserted into project model.",
        appliedResult.tracks.size(),
        totalNotes
    };
}

bool undoAppliedHermesResult(core::ProjectController& projectController, const AppliedHermesResult& appliedResult)
{
    if (appliedResult.trackIds.empty()) {
        return false;
    }

    bool allRemoved = true;
    for (auto it = appliedResult.trackIds.rbegin(); it != appliedResult.trackIds.rend(); ++it) {
        allRemoved = projectController.deleteTrackById(*it) && allRemoved;
    }

    return allRemoved;
}

bool redoAppliedHermesResult(core::ProjectController& projectController, AppliedHermesResult& appliedResult)
{
    if (appliedResult.tracks.empty()) {
        return false;
    }

    std::vector<std::uint64_t> recreatedTrackIds;
    recreatedTrackIds.reserve(appliedResult.tracks.size());

    for (const auto& storedTrack : appliedResult.tracks) {
        const auto& midiTrack = projectController.addTrack(core::TrackType::midi, storedTrack.displayName);

        if (!projectController.replaceMidiNotesOnTrack(midiTrack.id, storedTrack.notes)) {
            rollbackCreatedTracks(projectController, recreatedTrackIds);
            return false;
        }

        if (!projectController.setGeneratedGroupId(midiTrack.id, appliedResult.groupId)) {
            rollbackCreatedTracks(projectController, recreatedTrackIds);
            return false;
        }

        recreatedTrackIds.push_back(midiTrack.id);
    }

    appliedResult.trackIds = std::move(recreatedTrackIds);
    return true;
}

std::size_t countAppliedHermesNotes(const AppliedHermesResult& appliedResult)
{
    std::size_t count = 0;
    for (const auto& track : appliedResult.tracks) {
        count += track.notes.size();
    }

    return count;
}

}  // namespace dawhermes::hermes