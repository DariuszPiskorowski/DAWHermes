#include "core/TimelineLoop.h"

#include <algorithm>
#include <cmath>

namespace dawhermes::core {

namespace {

double sanitizeProjectEnd(double projectEndBeat) noexcept
{
    return std::isfinite(projectEndBeat)
        ? std::max(0.0, projectEndBeat)
        : 0.0;
}

double minimumLength(
    bool snapEnabled,
    int gridDenominator) noexcept
{
    return snapEnabled
        ? timelineLoopGridStep(gridDenominator)
        : kMinimumUnsnappedLoopBeats;
}

double prepareBeat(
    double beat,
    double projectEndBeat,
    bool snapEnabled,
    int gridDenominator) noexcept
{
    const auto finiteBeat = std::isfinite(beat)
        ? beat
        : 0.0;
    const auto prepared = snapEnabled
        ? snapTimelineLoopBeat(finiteBeat, gridDenominator)
        : finiteBeat;
    return std::clamp(
        prepared,
        0.0,
        sanitizeProjectEnd(projectEndBeat));
}

}  // namespace

bool TimelineLoopRange::isValid() const noexcept
{
    return std::isfinite(startBeat)
        && std::isfinite(endBeat)
        && startBeat >= 0.0
        && endBeat > startBeat;
}

double TimelineLoopRange::lengthBeats() const noexcept
{
    return isValid() ? endBeat - startBeat : 0.0;
}

double timelineLoopGridStep(int gridDenominator) noexcept
{
    switch (gridDenominator) {
    case 4:
    case 8:
    case 16:
    case 32:
        return 4.0 / static_cast<double>(gridDenominator);
    default:
        return 0.25;
    }
}

double snapTimelineLoopBeat(
    double beat,
    int gridDenominator) noexcept
{
    if (!std::isfinite(beat)) {
        return 0.0;
    }
    const auto step = timelineLoopGridStep(gridDenominator);
    return std::max(0.0, std::round(beat / step) * step);
}

std::optional<TimelineLoopRange> createTimelineLoopRange(
    double anchorBeat,
    double currentBeat,
    double projectEndBeat,
    bool snapEnabled,
    int gridDenominator) noexcept
{
    const auto projectEnd = sanitizeProjectEnd(projectEndBeat);
    const auto minimum = minimumLength(
        snapEnabled,
        gridDenominator);
    if (projectEnd < minimum) {
        return std::nullopt;
    }

    auto start = prepareBeat(
        std::min(anchorBeat, currentBeat),
        projectEnd,
        snapEnabled,
        gridDenominator);
    auto end = prepareBeat(
        std::max(anchorBeat, currentBeat),
        projectEnd,
        snapEnabled,
        gridDenominator);
    if (end - start < minimum) {
        if (start + minimum <= projectEnd) {
            end = start + minimum;
        } else {
            start = projectEnd - minimum;
            end = projectEnd;
        }
    }

    TimelineLoopRange range { start, end };
    return range.isValid()
        ? std::optional<TimelineLoopRange> { range }
        : std::nullopt;
}

std::optional<TimelineLoopRange> resizeTimelineLoopRange(
    TimelineLoopRange range,
    TimelineLoopEdge edge,
    double targetBeat,
    double projectEndBeat,
    bool snapEnabled,
    int gridDenominator) noexcept
{
    const auto projectEnd = sanitizeProjectEnd(projectEndBeat);
    const auto minimum = minimumLength(
        snapEnabled,
        gridDenominator);
    if (!range.isValid() || projectEnd < minimum) {
        return std::nullopt;
    }

    range.startBeat = std::clamp(range.startBeat, 0.0, projectEnd);
    range.endBeat = std::clamp(range.endBeat, 0.0, projectEnd);
    const auto target = prepareBeat(
        targetBeat,
        projectEnd,
        snapEnabled,
        gridDenominator);
    if (edge == TimelineLoopEdge::start) {
        range.startBeat = std::min(
            target,
            range.endBeat - minimum);
    } else {
        range.endBeat = std::max(
            target,
            range.startBeat + minimum);
    }
    range.startBeat = std::clamp(
        range.startBeat,
        0.0,
        std::max(0.0, projectEnd - minimum));
    range.endBeat = std::clamp(
        range.endBeat,
        std::min(projectEnd, range.startBeat + minimum),
        projectEnd);
    return range.isValid()
        ? std::optional<TimelineLoopRange> { range }
        : std::nullopt;
}

std::optional<TimelineLoopRange> moveTimelineLoopRange(
    TimelineLoopRange range,
    double deltaBeats,
    double projectEndBeat,
    bool snapEnabled,
    int gridDenominator) noexcept
{
    const auto projectEnd = sanitizeProjectEnd(projectEndBeat);
    if (!range.isValid()
        || projectEnd < range.lengthBeats()) {
        return std::nullopt;
    }

    const auto length = range.lengthBeats();
    auto newStart = range.startBeat
        + (std::isfinite(deltaBeats) ? deltaBeats : 0.0);
    if (snapEnabled) {
        newStart = snapTimelineLoopBeat(
            newStart,
            gridDenominator);
    }
    newStart = std::clamp(
        newStart,
        0.0,
        projectEnd - length);
    return TimelineLoopRange {
        newStart,
        newStart + length,
    };
}

bool timelineLoopContains(
    const TimelineLoopRange& range,
    double beat) noexcept
{
    return range.isValid()
        && std::isfinite(beat)
        && beat >= range.startBeat
        && beat < range.endBeat;
}

double timelineLoopPlayStartBeat(
    double currentBeat,
    const std::optional<TimelineLoopRange>& range,
    bool enabled) noexcept
{
    const auto current = std::isfinite(currentBeat)
        ? std::max(0.0, currentBeat)
        : 0.0;
    if (!enabled
        || !range.has_value()
        || !range->isValid()
        || timelineLoopContains(range.value(), current)) {
        return current;
    }
    return range->startBeat;
}

}  // namespace dawhermes::core
