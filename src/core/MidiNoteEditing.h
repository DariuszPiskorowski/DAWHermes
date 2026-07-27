#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "core/Track.h"

namespace dawhermes::core {

constexpr double kMinimumMidiNoteDurationBeats = 0.125;  // 1/32 note in quarter-note beat space.

int clampMidiPitch(int pitch);
int clampMidiVelocity(int velocity);
int clampMidiChannel(int channel);

double sanitizeGridStepBeats(double gridStepBeats);
double snapBeatToGrid(double beat, double gridStepBeats);

int mostCommonMidiChannel(const std::vector<MidiNote>& notes);

void sortMidiNotesByStart(std::vector<MidiNote>& notes);

struct MidiNoteCreationRequest {
    double clickedBeat { 0.0 };
    int clickedPitch { 60 };
    int defaultVelocity { 100 };
    int defaultChannel { 1 };
    bool snapEnabled { true };
    double gridStepBeats { 0.25 };
};

MidiNote makeCreatedMidiNote(const MidiNoteCreationRequest& request);

struct MidiNoteMarqueeSelectionRequest {
    double startBeat { 0.0 };
    double endBeat { 0.0 };
    double lowPitch { 0.0 };
    double highPitch { 128.0 };
};

bool isMeaningfulMarqueeDrag(double deltaX, double deltaY, double minimumPixels = 4.0);

bool isMidiNoteRightEdgeHit(double noteX, double noteWidth, double pointX, double zonePixels = 6.0);

std::vector<std::uint64_t> findMidiNotesIntersectingRange(
    const std::vector<MidiNote>& notes,
    const MidiNoteMarqueeSelectionRequest& request);

struct MoveSelectedNotesRequest {
    std::vector<std::uint64_t> selectedNoteIds;
    double requestedDeltaBeats { 0.0 };
    int requestedDeltaSemitones { 0 };
    bool snapEnabled { true };
    double gridStepBeats { 0.25 };
};

struct MoveSelectedNotesResult {
    bool changed { false };
    double appliedDeltaBeats { 0.0 };
    int appliedDeltaSemitones { 0 };
};

MoveSelectedNotesResult moveSelectedNotes(
    std::vector<MidiNote>& notes,
    const MoveSelectedNotesRequest& request);

struct ResizeSelectedNotesRequest {
    std::vector<std::uint64_t> selectedNoteIds;
    std::uint64_t anchorNoteId { 0 };
    double requestedAnchorEndBeat { 0.0 };
    bool snapEnabled { true };
    double gridStepBeats { 0.25 };
    double minimumDurationBeats { kMinimumMidiNoteDurationBeats };
};

struct ResizeSelectedNotesResult {
    bool changed { false };
    double appliedDurationDeltaBeats { 0.0 };
};

ResizeSelectedNotesResult resizeSelectedNotes(
    std::vector<MidiNote>& notes,
    const ResizeSelectedNotesRequest& request);

std::size_t deleteSelectedNotes(
    std::vector<MidiNote>& notes,
    const std::vector<std::uint64_t>& selectedNoteIds);

std::size_t applyVelocityToSelectedNotes(
    std::vector<MidiNote>& notes,
    const std::vector<std::uint64_t>& selectedNoteIds,
    int velocity);

std::size_t quantizeSelectedNoteStarts(
    std::vector<MidiNote>& notes,
    const std::vector<std::uint64_t>& selectedNoteIds,
    double gridStepBeats);

std::optional<std::size_t> findNoteIndexById(
    const std::vector<MidiNote>& notes,
    std::uint64_t noteId);

}  // namespace dawhermes::core
