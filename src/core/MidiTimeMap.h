#pragma once

#include <cstddef>
#include <vector>

#include "core/Track.h"

namespace dawhermes::core {

struct ResolvedMidiTimelineInfo {
    int ticksPerQuarterNote { 960 };
    std::vector<MidiTempoEvent> tempoMap;
    std::vector<MidiTimeSignatureEvent> timeSignatureMap;
};

ResolvedMidiTimelineInfo resolveMidiTimelineInfo(
    const std::vector<const Track*>& prioritizedTracks,
    const std::vector<Track>& allTracks);

std::vector<MidiTempoEvent> sanitizeTempoMap(const std::vector<MidiTempoEvent>& tempoMap);
std::vector<MidiTimeSignatureEvent> sanitizeTimeSignatureMap(const std::vector<MidiTimeSignatureEvent>& timeSignatureMap);

double beatsPerBarForTimeSignature(const MidiTimeSignatureEvent& signature);
double beatsPerBarAt(double beat, const std::vector<MidiTimeSignatureEvent>& timeSignatureMap);
double barStartBeatAt(double beat, const std::vector<MidiTimeSignatureEvent>& timeSignatureMap);
int barNumberAt(double beat, const std::vector<MidiTimeSignatureEvent>& timeSignatureMap);

std::vector<double> buildBarStartBeats(
    double startBeat,
    double endBeat,
    const std::vector<MidiTimeSignatureEvent>& timeSignatureMap,
    std::size_t maxBars = 4096);

double gridStepBeats(int denominator);
std::vector<double> buildGridBeatPositions(
    double startBeat,
    double endBeat,
    int denominator,
    std::size_t maxLines = 16384);

}  // namespace dawhermes::core
