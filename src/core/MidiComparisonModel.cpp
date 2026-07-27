#include "core/MidiComparisonModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dawhermes::core {

namespace {

constexpr std::size_t kInvalidIndex = static_cast<std::size_t>(-1);

MidiComparisonTolerance sanitizeTolerance(const MidiComparisonTolerance& tolerance)
{
    MidiComparisonTolerance sanitized = tolerance;
    sanitized.startBeatTolerance = std::max(1.0 / 960.0, std::abs(sanitized.startBeatTolerance));
    sanitized.durationBeatTolerance = std::max(1.0 / 960.0, std::abs(sanitized.durationBeatTolerance));
    sanitized.pitchTolerance = std::max(0, sanitized.pitchTolerance);
    sanitized.velocityTolerance = std::max(0, sanitized.velocityTolerance);
    return sanitized;
}

double matchCost(const MidiNote& source, const MidiNote& target, const MidiComparisonTolerance& tolerance)
{
    const auto startDelta = std::abs(target.startBeat - source.startBeat) / tolerance.startBeatTolerance;
    const auto durationDelta = std::abs(target.durationBeats - source.durationBeats) / tolerance.durationBeatTolerance;
    const auto pitchDelta = static_cast<double>(std::abs(target.pitch - source.pitch));
    const auto velocityDelta = static_cast<double>(std::abs(target.velocity - source.velocity)) / 16.0;

    return startDelta + durationDelta + (pitchDelta * 1.75) + velocityDelta;
}

MidiComparisonCategory classifyMatch(
    const MidiComparisonMatch& match,
    const MidiComparisonTolerance& tolerance)
{
    if (std::abs(match.pitchDelta) > tolerance.pitchTolerance) {
        return MidiComparisonCategory::pitchChanged;
    }

    if (std::abs(match.startBeatDelta) > tolerance.startBeatTolerance
        || std::abs(match.durationBeatDelta) > tolerance.durationBeatTolerance) {
        return MidiComparisonCategory::timingAdjusted;
    }

    if (std::abs(match.velocityDelta) > tolerance.velocityTolerance) {
        return MidiComparisonCategory::velocityAdjusted;
    }

    return MidiComparisonCategory::unchanged;
}

}  // namespace

MidiComparisonResult compareMidiNotes(
    const std::vector<MidiNote>& source,
    const std::vector<MidiNote>& target,
    const MidiComparisonTolerance& tolerance)
{
    MidiComparisonResult result;
    const auto safeTolerance = sanitizeTolerance(tolerance);

    if (source.empty() && target.empty()) {
        return result;
    }

    std::vector<bool> targetUsed(target.size(), false);
    result.matches.reserve(std::min(source.size(), target.size()));

    const auto maxStartWindow = std::max(safeTolerance.startBeatTolerance * 8.0, 0.75);

    for (std::size_t sourceIndex = 0; sourceIndex < source.size(); ++sourceIndex) {
        const auto& sourceNote = source[sourceIndex];

        std::size_t bestTargetIndex = kInvalidIndex;
        auto bestCost = std::numeric_limits<double>::infinity();

        for (std::size_t targetIndex = 0; targetIndex < target.size(); ++targetIndex) {
            if (targetUsed[targetIndex]) {
                continue;
            }

            const auto& targetNote = target[targetIndex];
            if (std::abs(targetNote.startBeat - sourceNote.startBeat) > maxStartWindow) {
                continue;
            }

            if (std::abs(targetNote.pitch - sourceNote.pitch) > 24) {
                continue;
            }

            const auto cost = matchCost(sourceNote, targetNote, safeTolerance);
            if (cost < bestCost - 1.0e-9) {
                bestCost = cost;
                bestTargetIndex = targetIndex;
            }
        }

        if (bestTargetIndex == kInvalidIndex) {
            result.sourceOnlyIndices.push_back(sourceIndex);
            continue;
        }

        targetUsed[bestTargetIndex] = true;

        const auto& targetNote = target[bestTargetIndex];
        MidiComparisonMatch match;
        match.sourceIndex = sourceIndex;
        match.targetIndex = bestTargetIndex;
        match.startBeatDelta = targetNote.startBeat - sourceNote.startBeat;
        match.durationBeatDelta = targetNote.durationBeats - sourceNote.durationBeats;
        match.pitchDelta = targetNote.pitch - sourceNote.pitch;
        match.velocityDelta = targetNote.velocity - sourceNote.velocity;
        match.category = classifyMatch(match, safeTolerance);
        result.matches.push_back(match);
    }

    for (std::size_t targetIndex = 0; targetIndex < target.size(); ++targetIndex) {
        if (!targetUsed[targetIndex]) {
            result.targetOnlyIndices.push_back(targetIndex);
        }
    }

    std::sort(result.matches.begin(), result.matches.end(), [](const auto& left, const auto& right) {
        return left.sourceIndex < right.sourceIndex;
    });

    return result;
}

MidiComparisonSummary summarizeMidiComparison(const MidiComparisonResult& result)
{
    MidiComparisonSummary summary;

    for (const auto& match : result.matches) {
        switch (match.category) {
        case MidiComparisonCategory::unchanged:
            ++summary.unchangedCount;
            break;
        case MidiComparisonCategory::timingAdjusted:
            ++summary.timingAdjustedCount;
            break;
        case MidiComparisonCategory::velocityAdjusted:
            ++summary.velocityAdjustedCount;
            break;
        case MidiComparisonCategory::pitchChanged:
            ++summary.pitchChangedCount;
            break;
        case MidiComparisonCategory::added:
        case MidiComparisonCategory::removed:
            break;
        }
    }

    summary.addedCount = result.targetOnlyIndices.size();
    summary.removedCount = result.sourceOnlyIndices.size();

    return summary;
}

}  // namespace dawhermes::core
