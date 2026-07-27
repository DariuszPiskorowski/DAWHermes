#pragma once

#include <cstddef>
#include <cstdint>
#include <vector>

#include "core/TimelineViewport.h"
#include "core/Track.h"

namespace dawhermes::core {

struct TimelineLaneGeometry {
    std::uint64_t trackId { 0 };
    TrackType trackType { TrackType::audio };
    int rowIndex { 0 };
    int y { 0 };
    int height { 0 };
};

std::vector<TimelineLaneGeometry> buildTimelineLaneGeometry(
    const std::vector<Track>& tracks,
    int rowHeight,
    int startY = 0);

const TimelineLaneGeometry* findTimelineLaneGeometry(
    const std::vector<TimelineLaneGeometry>& lanes,
    std::uint64_t trackId);

struct PitchViewportState {
    double highestVisiblePitch { 108.0 };
    double visiblePitchSpan { 60.0 };
    double minVisiblePitchSpan { 12.0 };
    double maxVisiblePitchSpan { 128.0 };
};

PitchViewportState sanitizePitchViewportState(const PitchViewportState& state);
PitchViewportState scrollPitchViewport(const PitchViewportState& state, double deltaSemitones);
PitchViewportState zoomPitchViewport(const PitchViewportState& state, double pivotPitch, double zoomFactor);
PitchViewportState fitPitchViewportToNotes(const std::vector<MidiNote>& notes, double paddingSemitones = 2.0);

double pitchToY(double pitch, int height, const PitchViewportState& state);
double yToPitch(double y, int height, const PitchViewportState& state);

struct MidiNoteGeometry {
    std::size_t noteIndex { 0 };
    double x { 0.0 };
    double y { 0.0 };
    double width { 0.0 };
    double height { 0.0 };
};

std::vector<MidiNoteGeometry> computeVisibleNoteGeometry(
    const std::vector<MidiNote>& notes,
    int width,
    int height,
    const TimelineViewportState& horizontal,
    const PitchViewportState& vertical,
    std::size_t maxNotes = 8192);

}  // namespace dawhermes::core
