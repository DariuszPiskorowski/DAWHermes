#include "core/TimelineViewport.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dawhermes::core {

namespace {

double clampFinite(double value, double minimum, double maximum)
{
    if (!std::isfinite(value)) {
        return minimum;
    }

    return std::clamp(value, minimum, maximum);
}

TimelineViewportState clampToContent(const TimelineViewportState& state, double contentEndBeat)
{
    auto clamped = state;
    clamped.startBeat = std::max(0.0, clamped.startBeat);

    if (contentEndBeat <= 0.0 || !std::isfinite(contentEndBeat)) {
        return clamped;
    }

    const auto maxStart = std::max(0.0, contentEndBeat - clamped.visibleBeats);
    clamped.startBeat = std::clamp(clamped.startBeat, 0.0, maxStart);
    return clamped;
}

}  // namespace

TimelineViewportState sanitizeTimelineViewportState(const TimelineViewportState& state)
{
    TimelineViewportState output = state;

    output.minVisibleBeats = clampFinite(output.minVisibleBeats, 1.0 / 256.0, 8192.0);
    output.maxVisibleBeats = clampFinite(output.maxVisibleBeats, output.minVisibleBeats, 8192.0);
    output.visibleBeats = clampFinite(output.visibleBeats, output.minVisibleBeats, output.maxVisibleBeats);
    output.startBeat = std::max(0.0, std::isfinite(output.startBeat) ? output.startBeat : 0.0);

    return output;
}

TimelineViewportState scrollTimelineViewport(
    const TimelineViewportState& state,
    double deltaBeats,
    double contentEndBeat)
{
    auto output = sanitizeTimelineViewportState(state);
    if (!std::isfinite(deltaBeats)) {
        deltaBeats = 0.0;
    }

    output.startBeat += deltaBeats;
    return clampToContent(output, contentEndBeat);
}

TimelineViewportState zoomTimelineViewport(
    const TimelineViewportState& state,
    double pivotBeat,
    double zoomFactor,
    double contentEndBeat)
{
    auto output = sanitizeTimelineViewportState(state);

    if (!std::isfinite(zoomFactor) || zoomFactor <= std::numeric_limits<double>::epsilon()) {
        return output;
    }

    if (!std::isfinite(pivotBeat)) {
        pivotBeat = output.startBeat + (output.visibleBeats * 0.5);
    }

    const auto oldVisible = output.visibleBeats;
    const auto newVisible = clampFinite(oldVisible / zoomFactor, output.minVisibleBeats, output.maxVisibleBeats);

    if (std::abs(newVisible - oldVisible) < 1.0e-9) {
        return clampToContent(output, contentEndBeat);
    }

    const auto relative = (pivotBeat - output.startBeat) / oldVisible;
    output.visibleBeats = newVisible;
    output.startBeat = pivotBeat - (relative * newVisible);

    return clampToContent(output, contentEndBeat);
}

TimelineViewportState fitTimelineViewport(
    double contentStartBeat,
    double contentEndBeat,
    double paddingBeats,
    double minVisibleBeats,
    double maxVisibleBeats)
{
    TimelineViewportState state;
    state.minVisibleBeats = minVisibleBeats;
    state.maxVisibleBeats = maxVisibleBeats;
    state = sanitizeTimelineViewportState(state);

    const auto start = std::isfinite(contentStartBeat) ? contentStartBeat : 0.0;
    const auto end = std::isfinite(contentEndBeat) ? contentEndBeat : start;
    const auto padding = std::max(0.0, std::isfinite(paddingBeats) ? paddingBeats : 0.0);

    const auto minBeat = std::max(0.0, std::min(start, end) - padding);
    const auto maxBeat = std::max(minBeat, std::max(start, end) + padding);

    state.startBeat = minBeat;
    state.visibleBeats = clampFinite(maxBeat - minBeat, state.minVisibleBeats, state.maxVisibleBeats);

    return state;
}

TimelineViewportState followTimelinePlayhead(
    const TimelineViewportState& state,
    double playheadBeat,
    double contentEndBeat,
    double followThreshold,
    double targetPosition)
{
    auto output = sanitizeTimelineViewportState(state);
    if (!std::isfinite(playheadBeat)) {
        return output;
    }

    const auto threshold = std::clamp(followThreshold, 0.0, 1.0);
    const auto target = std::clamp(targetPosition, 0.0, 1.0);
    const auto thresholdBeat = output.startBeat + (output.visibleBeats * threshold);
    if (playheadBeat <= thresholdBeat) {
        return output;
    }

    output.startBeat = playheadBeat - (output.visibleBeats * target);
    return clampToContent(output, contentEndBeat);
}

TimelineViewportState ensureTimelineBeatVisible(
    const TimelineViewportState& state,
    double beat,
    double contentEndBeat,
    double leadingContext,
    double trailingContext)
{
    auto output = sanitizeTimelineViewportState(state);
    if (!std::isfinite(beat)) {
        return output;
    }

    const auto leading = std::clamp(leadingContext, 0.0, 0.49);
    const auto trailing = std::clamp(trailingContext, 0.0, 0.49);
    const auto visibleStart = output.startBeat;
    const auto visibleEnd = output.startBeat + output.visibleBeats;
    if (beat < visibleStart) {
        output.startBeat = beat - (output.visibleBeats * leading);
    } else if (beat > visibleEnd) {
        output.startBeat = beat - (output.visibleBeats * (1.0 - trailing));
    } else {
        return output;
    }

    return clampToContent(output, contentEndBeat);
}

TimelineVisibleRange timelineVisibleRange(const TimelineViewportState& state)
{
    const auto sanitized = sanitizeTimelineViewportState(state);
    return TimelineVisibleRange {
        sanitized.startBeat,
        sanitized.startBeat + sanitized.visibleBeats,
    };
}

double timelineBeatToX(double beat, int width, const TimelineViewportState& state)
{
    const auto sanitized = sanitizeTimelineViewportState(state);
    const auto pixels = std::max(width, 0);
    if (pixels == 0) {
        return 0.0;
    }

    const auto normalized = (beat - sanitized.startBeat) / sanitized.visibleBeats;
    return normalized * static_cast<double>(pixels);
}

double timelineXToBeat(double x, int width, const TimelineViewportState& state)
{
    const auto sanitized = sanitizeTimelineViewportState(state);
    const auto pixels = std::max(width, 0);
    if (pixels == 0) {
        return sanitized.startBeat;
    }

    const auto normalized = x / static_cast<double>(pixels);
    return sanitized.startBeat + (normalized * sanitized.visibleBeats);
}

}  // namespace dawhermes::core
