#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/ProjectController.h"
#include "hermes/HermesTypes.h"

namespace dawhermes::hermes {

struct AppliedHermesTrack {
    std::string displayName;
    std::vector<core::MidiNote> notes;
};

struct AppliedHermesResult {
    std::string groupId;
    std::vector<AppliedHermesTrack> tracks;
    std::vector<std::uint64_t> trackIds;
};

struct ApplyToProjectResult {
    bool ok { false };
    std::string message;
    std::size_t insertedTrackCount { 0 };
    std::size_t insertedNoteCount { 0 };
};

bool isValidMidiNoteEvent(const core::MidiNote& note, std::string& reason);

ApplyToProjectResult applyHermesResultToProject(
    const HermesTrackContext& sourceContext,
    const HermesOperationResult& operationResult,
    const std::string& groupId,
    core::ProjectController& projectController,
    AppliedHermesResult& appliedResult);

bool undoAppliedHermesResult(core::ProjectController& projectController, const AppliedHermesResult& appliedResult);
bool redoAppliedHermesResult(core::ProjectController& projectController, AppliedHermesResult& appliedResult);

std::size_t countAppliedHermesNotes(const AppliedHermesResult& appliedResult);

}  // namespace dawhermes::hermes