#pragma once

#include <cstddef>

namespace dawhermes::core {

struct TimelineViewportState {
    double startBeat { 0.0 };
    double visibleBeats { 16.0 };
    double minVisibleBeats { 1.0 };
    double maxVisibleBeats { 512.0 };
};

struct TimelineVisibleRange {
    double startBeat { 0.0 };
    double endBeat { 16.0 };
};

TimelineViewportState sanitizeTimelineViewportState(const TimelineViewportState& state);

TimelineViewportState scrollTimelineViewport(
    const TimelineViewportState& state,
    double deltaBeats,
    double contentEndBeat = 0.0);

TimelineViewportState zoomTimelineViewport(
    const TimelineViewportState& state,
    double pivotBeat,
    double zoomFactor,
    double contentEndBeat = 0.0);

TimelineViewportState fitTimelineViewport(
    double contentStartBeat,
    double contentEndBeat,
    double paddingBeats = 1.0,
    double minVisibleBeats = 1.0,
    double maxVisibleBeats = 512.0);

TimelineViewportState followTimelinePlayhead(
    const TimelineViewportState& state,
    double playheadBeat,
    double contentEndBeat,
    double followThreshold = 0.80,
    double targetPosition = 0.65);

TimelineViewportState ensureTimelineBeatVisible(
    const TimelineViewportState& state,
    double beat,
    double contentEndBeat,
    double leadingContext = 0.10,
    double trailingContext = 0.20);

TimelineVisibleRange timelineVisibleRange(const TimelineViewportState& state);

double timelineBeatToX(double beat, int width, const TimelineViewportState& state);
double timelineXToBeat(double x, int width, const TimelineViewportState& state);

}  // namespace dawhermes::core
