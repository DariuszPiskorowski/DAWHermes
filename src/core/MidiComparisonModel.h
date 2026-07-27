#pragma once

#include <cstddef>
#include <vector>

#include "core/Track.h"

namespace dawhermes::core {

enum class MidiComparisonCategory {
    unchanged,
    timingAdjusted,
    velocityAdjusted,
    pitchChanged,
    added,
    removed
};

struct MidiComparisonTolerance {
    double startBeatTolerance { 1.0 / 48.0 };
    double durationBeatTolerance { 1.0 / 48.0 };
    int pitchTolerance { 0 };
    int velocityTolerance { 8 };
};

struct MidiComparisonMatch {
    std::size_t sourceIndex { 0 };
    std::size_t targetIndex { 0 };
    MidiComparisonCategory category { MidiComparisonCategory::unchanged };
    double startBeatDelta { 0.0 };
    double durationBeatDelta { 0.0 };
    int pitchDelta { 0 };
    int velocityDelta { 0 };
};

struct MidiComparisonResult {
    std::vector<MidiComparisonMatch> matches;
    std::vector<std::size_t> sourceOnlyIndices;
    std::vector<std::size_t> targetOnlyIndices;
};

struct MidiComparisonSummary {
    std::size_t unchangedCount { 0 };
    std::size_t timingAdjustedCount { 0 };
    std::size_t velocityAdjustedCount { 0 };
    std::size_t pitchChangedCount { 0 };
    std::size_t addedCount { 0 };
    std::size_t removedCount { 0 };
};

MidiComparisonResult compareMidiNotes(
    const std::vector<MidiNote>& source,
    const std::vector<MidiNote>& target,
    const MidiComparisonTolerance& tolerance = MidiComparisonTolerance {});

MidiComparisonSummary summarizeMidiComparison(const MidiComparisonResult& result);

}  // namespace dawhermes::core
