#include "core/MidiNoteEditing.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <map>

namespace dawhermes::core {

namespace {

constexpr double kEpsilon = 1.0e-9;

bool containsNoteId(const std::vector<std::uint64_t>& selectedNoteIds, std::uint64_t noteId)
{
    return std::find(selectedNoteIds.begin(), selectedNoteIds.end(), noteId) != selectedNoteIds.end();
}

bool hasSelectedIds(const std::vector<std::uint64_t>& selectedNoteIds)
{
    return std::any_of(selectedNoteIds.begin(), selectedNoteIds.end(), [](const std::uint64_t noteId) {
        return noteId != 0;
    });
}

}  // namespace

int clampMidiPitch(int pitch)
{
    return std::clamp(pitch, 0, 127);
}

int clampMidiVelocity(int velocity)
{
    return std::clamp(velocity, 1, 127);
}

int clampMidiChannel(int channel)
{
    return std::clamp(channel, 1, 16);
}

double sanitizeGridStepBeats(double gridStepBeats)
{
    if (!std::isfinite(gridStepBeats)) {
        return 0.25;
    }

    return std::max(kMinimumMidiNoteDurationBeats, std::abs(gridStepBeats));
}

double snapBeatToGrid(double beat, double gridStepBeats)
{
    const auto step = sanitizeGridStepBeats(gridStepBeats);
    const auto normalized = beat / step;
    const auto snapped = std::round(normalized) * step;
    if (!std::isfinite(snapped)) {
        return 0.0;
    }

    return std::max(0.0, snapped);
}

int mostCommonMidiChannel(const std::vector<MidiNote>& notes)
{
    if (notes.empty()) {
        return 1;
    }

    std::array<int, 17> counts {};
    for (const auto& note : notes) {
        const auto channel = clampMidiChannel(note.channel);
        ++counts[static_cast<std::size_t>(channel)];
    }

    int bestChannel = 1;
    int bestCount = -1;
    for (int channel = 1; channel <= 16; ++channel) {
        const auto count = counts[static_cast<std::size_t>(channel)];
        if (count > bestCount) {
            bestCount = count;
            bestChannel = channel;
        }
    }

    return bestChannel;
}

void sortMidiNotesByStart(std::vector<MidiNote>& notes)
{
    std::stable_sort(notes.begin(), notes.end(), [](const MidiNote& left, const MidiNote& right) {
        if (std::abs(left.startBeat - right.startBeat) > kEpsilon) {
            return left.startBeat < right.startBeat;
        }

        if (left.pitch != right.pitch) {
            return left.pitch < right.pitch;
        }

        if (left.channel != right.channel) {
            return left.channel < right.channel;
        }

        return left.id < right.id;
    });
}

MidiNote makeCreatedMidiNote(const MidiNoteCreationRequest& request)
{
    MidiNote note;
    note.pitch = clampMidiPitch(request.clickedPitch);
    note.velocity = clampMidiVelocity(request.defaultVelocity);
    note.channel = clampMidiChannel(request.defaultChannel);

    const auto step = sanitizeGridStepBeats(request.gridStepBeats);
    note.startBeat = request.snapEnabled
        ? snapBeatToGrid(request.clickedBeat, step)
        : std::max(0.0, request.clickedBeat);
    note.durationBeats = std::max(kMinimumMidiNoteDurationBeats, step);
    note.id = 0;
    return note;
}

bool isMeaningfulMarqueeDrag(double deltaX, double deltaY, double minimumPixels)
{
    const auto threshold = std::max(1.0, std::abs(minimumPixels));
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) {
        return false;
    }

    return std::hypot(deltaX, deltaY) >= threshold;
}

bool isMidiNoteRightEdgeHit(double noteX, double noteWidth, double pointX, double zonePixels)
{
    if (!std::isfinite(noteX) || !std::isfinite(noteWidth) || !std::isfinite(pointX)) {
        return false;
    }

    const auto width = std::max(1.0, noteWidth);
    const auto zone = std::clamp(std::abs(zonePixels), 2.0, width);
    const auto right = noteX + width;
    return pointX >= (right - zone) && pointX <= right;
}

std::vector<std::uint64_t> findMidiNotesIntersectingRange(
    const std::vector<MidiNote>& notes,
    const MidiNoteMarqueeSelectionRequest& request)
{
    std::vector<std::uint64_t> noteIds;
    if (notes.empty()) {
        return noteIds;
    }

    const auto minBeat = std::max(0.0, std::min(request.startBeat, request.endBeat));
    const auto maxBeat = std::max(minBeat, std::max(request.startBeat, request.endBeat));
    const auto lowPitch = std::clamp(std::min(request.lowPitch, request.highPitch), 0.0, 128.0);
    const auto highPitch = std::clamp(std::max(request.lowPitch, request.highPitch), lowPitch, 128.0);

    for (const auto& note : notes) {
        if (note.id == 0) {
            continue;
        }

        const auto noteStart = std::max(0.0, note.startBeat);
        const auto noteEnd = noteStart + std::max(kMinimumMidiNoteDurationBeats, note.durationBeats);
        const auto noteLowPitch = static_cast<double>(clampMidiPitch(note.pitch));
        const auto noteHighPitch = noteLowPitch + 1.0;

        const auto intersectsBeat = noteEnd > minBeat && noteStart < maxBeat;
        const auto intersectsPitch = noteHighPitch > lowPitch && noteLowPitch < highPitch;
        if (intersectsBeat && intersectsPitch) {
            noteIds.push_back(note.id);
        }
    }

    return noteIds;
}

MoveSelectedNotesResult moveSelectedNotes(
    std::vector<MidiNote>& notes,
    const MoveSelectedNotesRequest& request)
{
    MoveSelectedNotesResult result;

    if (notes.empty() || !hasSelectedIds(request.selectedNoteIds)) {
        return result;
    }

    std::vector<MidiNote*> selected;
    selected.reserve(request.selectedNoteIds.size());

    for (auto& note : notes) {
        if (note.id != 0 && containsNoteId(request.selectedNoteIds, note.id)) {
            selected.push_back(&note);
        }
    }

    if (selected.empty()) {
        return result;
    }

    auto deltaBeats = std::isfinite(request.requestedDeltaBeats) ? request.requestedDeltaBeats : 0.0;

    if (request.snapEnabled) {
        const auto gridStep = sanitizeGridStepBeats(request.gridStepBeats);
        double minStart = std::numeric_limits<double>::max();
        for (const auto* note : selected) {
            minStart = std::min(minStart, note->startBeat);
        }

        const auto snappedAnchor = snapBeatToGrid(minStart + deltaBeats, gridStep);
        deltaBeats = snappedAnchor - minStart;
    }

    int minDeltaSemitones = std::numeric_limits<int>::min();
    int maxDeltaSemitones = std::numeric_limits<int>::max();
    double minStart = std::numeric_limits<double>::max();

    for (const auto* note : selected) {
        minDeltaSemitones = std::max(minDeltaSemitones, -note->pitch);
        maxDeltaSemitones = std::min(maxDeltaSemitones, 127 - note->pitch);
        minStart = std::min(minStart, note->startBeat);
    }

    const auto appliedDeltaSemitones = std::clamp(
        request.requestedDeltaSemitones,
        minDeltaSemitones,
        maxDeltaSemitones);

    if ((minStart + deltaBeats) < 0.0) {
        deltaBeats = -minStart;
    }

    if (std::abs(deltaBeats) <= kEpsilon && appliedDeltaSemitones == 0) {
        return result;
    }

    for (auto* note : selected) {
        note->startBeat = std::max(0.0, note->startBeat + deltaBeats);
        note->pitch = clampMidiPitch(note->pitch + appliedDeltaSemitones);
    }

    sortMidiNotesByStart(notes);

    result.changed = true;
    result.appliedDeltaBeats = deltaBeats;
    result.appliedDeltaSemitones = appliedDeltaSemitones;
    return result;
}

ResizeSelectedNotesResult resizeSelectedNotes(
    std::vector<MidiNote>& notes,
    const ResizeSelectedNotesRequest& request)
{
    ResizeSelectedNotesResult result;

    if (notes.empty() || request.anchorNoteId == 0 || !hasSelectedIds(request.selectedNoteIds)) {
        return result;
    }

    auto anchorIt = std::find_if(notes.begin(), notes.end(), [noteId = request.anchorNoteId](const MidiNote& note) {
        return note.id == noteId;
    });
    if (anchorIt == notes.end()) {
        return result;
    }

    if (!containsNoteId(request.selectedNoteIds, anchorIt->id)) {
        return result;
    }

    const auto minimumDuration = std::max(kMinimumMidiNoteDurationBeats, request.minimumDurationBeats);
    const auto step = sanitizeGridStepBeats(request.gridStepBeats);

    auto requestedEndBeat = std::max(0.0, request.requestedAnchorEndBeat);
    if (request.snapEnabled) {
        requestedEndBeat = snapBeatToGrid(requestedEndBeat, step);
    }

    const auto anchorStartBeat = std::max(0.0, anchorIt->startBeat);
    const auto anchorCurrentDuration = std::max(minimumDuration, anchorIt->durationBeats);

    auto targetAnchorDuration = std::max(minimumDuration, requestedEndBeat - anchorStartBeat);
    auto appliedDelta = targetAnchorDuration - anchorCurrentDuration;

    if (std::abs(appliedDelta) <= kEpsilon) {
        return result;
    }

    std::vector<MidiNote*> selected;
    selected.reserve(request.selectedNoteIds.size());
    for (auto& note : notes) {
        if (note.id != 0 && containsNoteId(request.selectedNoteIds, note.id)) {
            selected.push_back(&note);
        }
    }

    if (selected.empty()) {
        return result;
    }

    bool changedAny = false;
    for (auto* note : selected) {
        const auto original = std::max(minimumDuration, note->durationBeats);
        const auto updated = std::max(minimumDuration, original + appliedDelta);
        if (std::abs(updated - original) > kEpsilon) {
            note->durationBeats = updated;
            changedAny = true;
        }
    }

    if (!changedAny) {
        return result;
    }

    sortMidiNotesByStart(notes);

    result.changed = true;
    result.appliedDurationDeltaBeats = appliedDelta;
    return result;
}

std::size_t deleteSelectedNotes(
    std::vector<MidiNote>& notes,
    const std::vector<std::uint64_t>& selectedNoteIds)
{
    if (notes.empty() || !hasSelectedIds(selectedNoteIds)) {
        return 0;
    }

    const auto beforeCount = notes.size();
    notes.erase(
        std::remove_if(notes.begin(), notes.end(), [&selectedNoteIds](const MidiNote& note) {
            return note.id != 0 && containsNoteId(selectedNoteIds, note.id);
        }),
        notes.end());

    return beforeCount - notes.size();
}

std::size_t applyVelocityToSelectedNotes(
    std::vector<MidiNote>& notes,
    const std::vector<std::uint64_t>& selectedNoteIds,
    int velocity)
{
    if (notes.empty() || !hasSelectedIds(selectedNoteIds)) {
        return 0;
    }

    const auto clampedVelocity = clampMidiVelocity(velocity);

    std::size_t changedCount = 0;
    for (auto& note : notes) {
        if (note.id == 0 || !containsNoteId(selectedNoteIds, note.id)) {
            continue;
        }

        if (note.velocity != clampedVelocity) {
            note.velocity = clampedVelocity;
            ++changedCount;
        }
    }

    return changedCount;
}

std::size_t quantizeSelectedNoteStarts(
    std::vector<MidiNote>& notes,
    const std::vector<std::uint64_t>& selectedNoteIds,
    double gridStepBeats)
{
    if (notes.empty() || !hasSelectedIds(selectedNoteIds)) {
        return 0;
    }

    const auto step = sanitizeGridStepBeats(gridStepBeats);
    std::size_t changedCount = 0;

    for (auto& note : notes) {
        if (note.id == 0 || !containsNoteId(selectedNoteIds, note.id)) {
            continue;
        }

        const auto snapped = snapBeatToGrid(note.startBeat, step);
        if (std::abs(snapped - note.startBeat) > kEpsilon) {
            note.startBeat = snapped;
            ++changedCount;
        }
    }

    if (changedCount > 0) {
        sortMidiNotesByStart(notes);
    }

    return changedCount;
}

std::optional<std::size_t> findNoteIndexById(
    const std::vector<MidiNote>& notes,
    std::uint64_t noteId)
{
    if (noteId == 0) {
        return std::nullopt;
    }

    for (std::size_t index = 0; index < notes.size(); ++index) {
        if (notes[index].id == noteId) {
            return index;
        }
    }

    return std::nullopt;
}

}  // namespace dawhermes::core
