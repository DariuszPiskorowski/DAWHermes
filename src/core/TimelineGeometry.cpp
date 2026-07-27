#include "core/TimelineGeometry.h"

#include <algorithm>
#include <cmath>

namespace dawhermes::core {

namespace {

double clampFinite(double value, double minimum, double maximum)
{
    if (!std::isfinite(value)) {
        return minimum;
    }

    return std::clamp(value, minimum, maximum);
}

}  // namespace

std::vector<TimelineLaneGeometry> buildTimelineLaneGeometry(
    const std::vector<Track>& tracks,
    int rowHeight,
    int startY)
{
    std::vector<TimelineLaneGeometry> lanes;
    const auto safeRowHeight = std::max(1, rowHeight);
    lanes.reserve(tracks.size());

    for (std::size_t index = 0; index < tracks.size(); ++index) {
        const auto& track = tracks[index];
        TimelineLaneGeometry lane;
        lane.trackId = track.id;
        lane.trackType = track.type;
        lane.rowIndex = static_cast<int>(index);
        lane.y = startY + (lane.rowIndex * safeRowHeight);
        lane.height = safeRowHeight;
        lanes.push_back(lane);
    }

    return lanes;
}

const TimelineLaneGeometry* findTimelineLaneGeometry(
    const std::vector<TimelineLaneGeometry>& lanes,
    std::uint64_t trackId)
{
    for (const auto& lane : lanes) {
        if (lane.trackId == trackId) {
            return &lane;
        }
    }

    return nullptr;
}

PitchViewportState sanitizePitchViewportState(const PitchViewportState& state)
{
    PitchViewportState output = state;

    output.minVisiblePitchSpan = clampFinite(output.minVisiblePitchSpan, 1.0, 128.0);
    output.maxVisiblePitchSpan = clampFinite(output.maxVisiblePitchSpan, output.minVisiblePitchSpan, 128.0);
    output.visiblePitchSpan = clampFinite(output.visiblePitchSpan, output.minVisiblePitchSpan, output.maxVisiblePitchSpan);

    const auto minHighestPitch = output.visiblePitchSpan - 1.0;
    output.highestVisiblePitch = clampFinite(output.highestVisiblePitch, minHighestPitch, 127.0);

    return output;
}

PitchViewportState scrollPitchViewport(const PitchViewportState& state, double deltaSemitones)
{
    auto output = sanitizePitchViewportState(state);
    if (!std::isfinite(deltaSemitones)) {
        return output;
    }

    output.highestVisiblePitch += deltaSemitones;
    return sanitizePitchViewportState(output);
}

PitchViewportState zoomPitchViewport(const PitchViewportState& state, double pivotPitch, double zoomFactor)
{
    auto output = sanitizePitchViewportState(state);
    if (!std::isfinite(zoomFactor) || zoomFactor <= 0.0) {
        return output;
    }

    if (!std::isfinite(pivotPitch)) {
        pivotPitch = output.highestVisiblePitch - (output.visiblePitchSpan * 0.5);
    }

    const auto oldSpan = output.visiblePitchSpan;
    const auto newSpan = clampFinite(oldSpan / zoomFactor, output.minVisiblePitchSpan, output.maxVisiblePitchSpan);
    if (std::abs(newSpan - oldSpan) < 1.0e-9) {
        return output;
    }

    const auto relative = (output.highestVisiblePitch - pivotPitch) / oldSpan;
    output.visiblePitchSpan = newSpan;
    output.highestVisiblePitch = pivotPitch + (relative * newSpan);

    return sanitizePitchViewportState(output);
}

PitchViewportState fitPitchViewportToNotes(const std::vector<MidiNote>& notes, double paddingSemitones)
{
    auto viewport = sanitizePitchViewportState(PitchViewportState {});
    if (notes.empty()) {
        return viewport;
    }

    int minPitch = 127;
    int maxPitch = 0;
    for (const auto& note : notes) {
        minPitch = std::min(minPitch, std::clamp(note.pitch, 0, 127));
        maxPitch = std::max(maxPitch, std::clamp(note.pitch, 0, 127));
    }

    const auto padding = std::max(0.0, std::isfinite(paddingSemitones) ? paddingSemitones : 0.0);
    const auto fittedMin = std::max(0.0, static_cast<double>(minPitch) - padding);
    const auto fittedMax = std::min(127.0, static_cast<double>(maxPitch) + padding + 1.0);

    viewport.visiblePitchSpan = std::max(1.0, fittedMax - fittedMin);
    viewport.highestVisiblePitch = fittedMax;
    return sanitizePitchViewportState(viewport);
}

double pitchToY(double pitch, int height, const PitchViewportState& state)
{
    const auto viewport = sanitizePitchViewportState(state);
    const auto pixels = std::max(height, 1);

    const auto normalized = (viewport.highestVisiblePitch - pitch) / viewport.visiblePitchSpan;
    return normalized * static_cast<double>(pixels);
}

double yToPitch(double y, int height, const PitchViewportState& state)
{
    const auto viewport = sanitizePitchViewportState(state);
    const auto pixels = std::max(height, 1);
    const auto normalized = y / static_cast<double>(pixels);
    return viewport.highestVisiblePitch - (normalized * viewport.visiblePitchSpan);
}

std::vector<MidiNoteGeometry> computeVisibleNoteGeometry(
    const std::vector<MidiNote>& notes,
    int width,
    int height,
    const TimelineViewportState& horizontal,
    const PitchViewportState& vertical,
    std::size_t maxNotes)
{
    std::vector<MidiNoteGeometry> geometry;
    if (notes.empty() || maxNotes == 0) {
        return geometry;
    }

    const auto timeline = sanitizeTimelineViewportState(horizontal);
    const auto pitchViewport = sanitizePitchViewportState(vertical);
    const auto visibleStartBeat = timeline.startBeat;
    const auto visibleEndBeat = timeline.startBeat + timeline.visibleBeats;

    geometry.reserve(std::min<std::size_t>(notes.size(), maxNotes));

    for (std::size_t index = 0; index < notes.size(); ++index) {
        if (geometry.size() >= maxNotes) {
            break;
        }

        const auto& note = notes[index];
        const auto noteStart = note.startBeat;
        const auto noteEnd = note.startBeat + std::max(1.0 / 960.0, note.durationBeats);
        if (noteEnd < visibleStartBeat || noteStart > visibleEndBeat) {
            continue;
        }

        const auto pitch = static_cast<double>(std::clamp(note.pitch, 0, 127));
        const auto minPitch = pitchViewport.highestVisiblePitch - pitchViewport.visiblePitchSpan;
        if (pitch + 1.0 < minPitch || pitch > pitchViewport.highestVisiblePitch + 1.0) {
            continue;
        }

        const auto x1 = timelineBeatToX(noteStart, width, timeline);
        const auto x2 = timelineBeatToX(noteEnd, width, timeline);

        const auto yTop = pitchToY(pitch + 1.0, height, pitchViewport);
        const auto yBottom = pitchToY(pitch, height, pitchViewport);

        MidiNoteGeometry entry;
        entry.noteIndex = index;
        entry.x = x1;
        entry.width = std::max(1.0, x2 - x1);
        entry.y = std::min(yTop, yBottom);
        entry.height = std::max(1.0, std::abs(yBottom - yTop));
        geometry.push_back(entry);
    }

    return geometry;
}

}  // namespace dawhermes::core
