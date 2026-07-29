#pragma once

#include <optional>

namespace dawhermes::core {

constexpr double kMinimumUnsnappedLoopBeats = 1.0 / 960.0;

struct TimelineLoopRange {
    double startBeat { 0.0 };
    double endBeat { 0.0 };

    bool isValid() const noexcept;
    double lengthBeats() const noexcept;
    bool operator==(const TimelineLoopRange& other) const = default;
};

enum class TimelineLoopEdge {
    start,
    end
};

double timelineLoopGridStep(int gridDenominator) noexcept;
double snapTimelineLoopBeat(
    double beat,
    int gridDenominator) noexcept;

std::optional<TimelineLoopRange> createTimelineLoopRange(
    double anchorBeat,
    double currentBeat,
    double projectEndBeat,
    bool snapEnabled,
    int gridDenominator) noexcept;

std::optional<TimelineLoopRange> resizeTimelineLoopRange(
    TimelineLoopRange range,
    TimelineLoopEdge edge,
    double targetBeat,
    double projectEndBeat,
    bool snapEnabled,
    int gridDenominator) noexcept;

std::optional<TimelineLoopRange> moveTimelineLoopRange(
    TimelineLoopRange range,
    double deltaBeats,
    double projectEndBeat,
    bool snapEnabled,
    int gridDenominator) noexcept;

bool timelineLoopContains(
    const TimelineLoopRange& range,
    double beat) noexcept;

double timelineLoopPlayStartBeat(
    double currentBeat,
    const std::optional<TimelineLoopRange>& range,
    bool enabled) noexcept;

}  // namespace dawhermes::core
