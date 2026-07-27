#include "core/MidiTimeMap.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace dawhermes::core {

namespace {

constexpr double kEpsilon = 1.0e-9;

bool isSaneTempoEvent(const MidiTempoEvent& event)
{
    return std::isfinite(event.beatPosition)
        && event.beatPosition >= 0.0
        && event.microsecondsPerQuarterNote > 0;
}

bool isSaneTimeSignatureEvent(const MidiTimeSignatureEvent& event)
{
    return std::isfinite(event.beatPosition)
        && event.beatPosition >= 0.0
        && event.numerator > 0
        && event.denominator > 0;
}

std::vector<MidiTimeSignatureEvent> ensureTimeSignatureDefaults(const std::vector<MidiTimeSignatureEvent>& input)
{
    auto map = sanitizeTimeSignatureMap(input);
    if (map.empty()) {
        map.push_back(MidiTimeSignatureEvent {});
    }

    if (map.front().beatPosition > kEpsilon) {
        map.insert(map.begin(), MidiTimeSignatureEvent {});
    }

    return map;
}

std::size_t activeTimeSignatureIndex(double beat, const std::vector<MidiTimeSignatureEvent>& map)
{
    if (map.empty()) {
        return 0;
    }

    const auto clampedBeat = std::max(0.0, beat);
    std::size_t index = 0;
    while ((index + 1) < map.size() && map[index + 1].beatPosition <= (clampedBeat + kEpsilon)) {
        ++index;
    }

    return index;
}

int countBarsInSegment(double segmentStart, double segmentEnd, double beatsPerBar)
{
    if (beatsPerBar <= kEpsilon || segmentEnd <= segmentStart) {
        return 0;
    }

    const auto span = std::max(0.0, segmentEnd - segmentStart);
    return static_cast<int>(std::floor((span / beatsPerBar) + kEpsilon));
}

}  // namespace

std::vector<MidiTempoEvent> sanitizeTempoMap(const std::vector<MidiTempoEvent>& tempoMap)
{
    std::vector<MidiTempoEvent> sanitized;
    sanitized.reserve(tempoMap.size());

    for (const auto& event : tempoMap) {
        if (!isSaneTempoEvent(event)) {
            continue;
        }

        sanitized.push_back(event);
    }

    std::sort(sanitized.begin(), sanitized.end(), [](const auto& left, const auto& right) {
        if (std::abs(left.beatPosition - right.beatPosition) < kEpsilon) {
            return left.microsecondsPerQuarterNote < right.microsecondsPerQuarterNote;
        }

        return left.beatPosition < right.beatPosition;
    });

    std::vector<MidiTempoEvent> deduplicated;
    deduplicated.reserve(sanitized.size());

    for (const auto& event : sanitized) {
        if (!deduplicated.empty() && std::abs(deduplicated.back().beatPosition - event.beatPosition) < kEpsilon) {
            deduplicated.back() = event;
            continue;
        }

        deduplicated.push_back(event);
    }

    if (deduplicated.empty()) {
        deduplicated.push_back(MidiTempoEvent {});
    }

    if (deduplicated.front().beatPosition > kEpsilon) {
        deduplicated.insert(deduplicated.begin(), MidiTempoEvent {});
    }

    return deduplicated;
}

std::vector<MidiTimeSignatureEvent> sanitizeTimeSignatureMap(const std::vector<MidiTimeSignatureEvent>& timeSignatureMap)
{
    std::vector<MidiTimeSignatureEvent> sanitized;
    sanitized.reserve(timeSignatureMap.size());

    for (const auto& event : timeSignatureMap) {
        if (!isSaneTimeSignatureEvent(event)) {
            continue;
        }

        auto clamped = event;
        clamped.numerator = std::clamp(clamped.numerator, 1, 32);
        clamped.denominator = std::clamp(clamped.denominator, 1, 32);
        sanitized.push_back(clamped);
    }

    std::sort(sanitized.begin(), sanitized.end(), [](const auto& left, const auto& right) {
        if (std::abs(left.beatPosition - right.beatPosition) < kEpsilon) {
            if (left.numerator == right.numerator) {
                return left.denominator < right.denominator;
            }

            return left.numerator < right.numerator;
        }

        return left.beatPosition < right.beatPosition;
    });

    std::vector<MidiTimeSignatureEvent> deduplicated;
    deduplicated.reserve(sanitized.size());

    for (const auto& event : sanitized) {
        if (!deduplicated.empty() && std::abs(deduplicated.back().beatPosition - event.beatPosition) < kEpsilon) {
            deduplicated.back() = event;
            continue;
        }

        deduplicated.push_back(event);
    }

    if (deduplicated.empty()) {
        deduplicated.push_back(MidiTimeSignatureEvent {});
    }

    if (deduplicated.front().beatPosition > kEpsilon) {
        deduplicated.insert(deduplicated.begin(), MidiTimeSignatureEvent {});
    }

    return deduplicated;
}

ResolvedMidiTimelineInfo resolveMidiTimelineInfo(
    const std::vector<const Track*>& prioritizedTracks,
    const std::vector<Track>& allTracks)
{
    ResolvedMidiTimelineInfo info;

    auto resolveFromTrack = [&info](const Track* track) {
        if (track == nullptr || track->type != TrackType::midi || !track->midiSourceMetadata.has_value()) {
            return;
        }

        const auto& metadata = track->midiSourceMetadata.value();
        if (metadata.ticksPerQuarterNote > 0 && info.ticksPerQuarterNote <= 0) {
            info.ticksPerQuarterNote = metadata.ticksPerQuarterNote;
        }

        if (info.tempoMap.empty() && !metadata.tempoMap.empty()) {
            info.tempoMap = metadata.tempoMap;
        }

        if (info.timeSignatureMap.empty() && !metadata.timeSignatureMap.empty()) {
            info.timeSignatureMap = metadata.timeSignatureMap;
        }
    };

    info.ticksPerQuarterNote = 0;

    for (const auto* track : prioritizedTracks) {
        resolveFromTrack(track);
    }

    for (const auto& track : allTracks) {
        resolveFromTrack(&track);
    }

    if (info.ticksPerQuarterNote <= 0) {
        info.ticksPerQuarterNote = 960;
    }

    info.tempoMap = sanitizeTempoMap(info.tempoMap);
    info.timeSignatureMap = ensureTimeSignatureDefaults(info.timeSignatureMap);

    return info;
}

double beatsPerBarForTimeSignature(const MidiTimeSignatureEvent& signature)
{
    const auto numerator = std::max(signature.numerator, 1);
    const auto denominator = std::max(signature.denominator, 1);
    return static_cast<double>(numerator) * (4.0 / static_cast<double>(denominator));
}

double beatsPerBarAt(double beat, const std::vector<MidiTimeSignatureEvent>& timeSignatureMap)
{
    const auto map = ensureTimeSignatureDefaults(timeSignatureMap);
    const auto index = activeTimeSignatureIndex(beat, map);
    return beatsPerBarForTimeSignature(map[index]);
}

double barStartBeatAt(double beat, const std::vector<MidiTimeSignatureEvent>& timeSignatureMap)
{
    const auto map = ensureTimeSignatureDefaults(timeSignatureMap);
    const auto clampedBeat = std::max(0.0, beat);
    const auto index = activeTimeSignatureIndex(clampedBeat, map);

    const auto& signature = map[index];
    const auto beatsPerBar = beatsPerBarForTimeSignature(signature);
    if (beatsPerBar <= kEpsilon) {
        return signature.beatPosition;
    }

    const auto offset = std::max(0.0, clampedBeat - signature.beatPosition);
    const auto barOffset = std::floor((offset / beatsPerBar) + kEpsilon) * beatsPerBar;
    return signature.beatPosition + barOffset;
}

int barNumberAt(double beat, const std::vector<MidiTimeSignatureEvent>& timeSignatureMap)
{
    const auto map = ensureTimeSignatureDefaults(timeSignatureMap);
    const auto clampedBeat = std::max(0.0, beat);

    int barNumber = 1;

    for (std::size_t index = 0; index < map.size(); ++index) {
        const auto& signature = map[index];
        const auto beatsPerBar = beatsPerBarForTimeSignature(signature);
        const auto segmentStart = signature.beatPosition;
        const auto segmentEnd = (index + 1) < map.size()
            ? map[index + 1].beatPosition
            : std::numeric_limits<double>::infinity();

        if (clampedBeat < segmentEnd - kEpsilon) {
            if (beatsPerBar <= kEpsilon) {
                return barNumber;
            }

            const auto barsIntoSegment = static_cast<int>(std::floor(((clampedBeat - segmentStart) / beatsPerBar) + kEpsilon));
            return barNumber + std::max(0, barsIntoSegment);
        }

        barNumber += countBarsInSegment(segmentStart, segmentEnd, beatsPerBar);
    }

    return std::max(1, barNumber);
}

std::vector<double> buildBarStartBeats(
    double startBeat,
    double endBeat,
    const std::vector<MidiTimeSignatureEvent>& timeSignatureMap,
    std::size_t maxBars)
{
    std::vector<double> beats;
    if (maxBars == 0) {
        return beats;
    }

    const auto map = ensureTimeSignatureDefaults(timeSignatureMap);
    const auto minBeat = std::max(0.0, std::min(startBeat, endBeat));
    const auto maxBeat = std::max(minBeat, std::max(startBeat, endBeat));

    double barBeat = barStartBeatAt(minBeat, map);
    if (barBeat > minBeat + kEpsilon) {
        barBeat = minBeat;
    }

    beats.reserve(std::min<std::size_t>(maxBars, 512));

    while (beats.size() < maxBars && barBeat <= (maxBeat + kEpsilon)) {
        if (barBeat >= (minBeat - kEpsilon)) {
            beats.push_back(barBeat);
        }

        const auto index = activeTimeSignatureIndex(barBeat + kEpsilon, map);
        const auto step = std::max(1.0 / 64.0, beatsPerBarForTimeSignature(map[index]));
        auto nextBeat = barBeat + step;

        if ((index + 1) < map.size()) {
            const auto switchBeat = map[index + 1].beatPosition;
            if (switchBeat > barBeat + kEpsilon && switchBeat < nextBeat - kEpsilon) {
                nextBeat = switchBeat;
            }
        }

        if (nextBeat <= barBeat + kEpsilon) {
            nextBeat = barBeat + step;
        }

        barBeat = nextBeat;
    }

    return beats;
}

double gridStepBeats(int denominator)
{
    const auto safeDenominator = std::max(denominator, 1);
    return 4.0 / static_cast<double>(safeDenominator);
}

std::vector<double> buildGridBeatPositions(
    double startBeat,
    double endBeat,
    int denominator,
    std::size_t maxLines)
{
    std::vector<double> beats;
    if (maxLines == 0) {
        return beats;
    }

    const auto minBeat = std::max(0.0, std::min(startBeat, endBeat));
    const auto maxBeat = std::max(minBeat, std::max(startBeat, endBeat));
    const auto step = std::max(1.0 / 256.0, gridStepBeats(denominator));

    auto lineBeat = std::floor((minBeat / step) + kEpsilon) * step;
    if (lineBeat < minBeat - kEpsilon) {
        lineBeat += step;
    }

    beats.reserve(std::min<std::size_t>(maxLines, 2048));
    while (beats.size() < maxLines && lineBeat <= (maxBeat + kEpsilon)) {
        beats.push_back(lineBeat);
        lineBeat += step;
    }

    return beats;
}

}  // namespace dawhermes::core
