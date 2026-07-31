#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "core/Track.h"

namespace dawhermes::audio {

enum class MidiPlaybackEventKind {
    noteOff,
    noteOn
};

struct MidiPlaybackEvent {
    double timeSeconds { 0.0 };
    MidiPlaybackEventKind kind { MidiPlaybackEventKind::noteOn };
    int pitch { 60 };
    float amplitude { 0.0f };
    std::uint64_t noteInstanceId { 0 };
    std::uint64_t sourceTrackId { 0 };
    int channel { 1 };
};

struct MidiPlaybackSnapshot {
    std::vector<MidiPlaybackEvent> events;
    std::vector<core::MidiTempoEvent> tempoMap;
    double durationSeconds { 0.0 };
    std::uint64_t sourceTrackId { 0 };
    std::string sourceTrackName;
};

struct MidiPlaybackSnapshotResult {
    bool ok { false };
    std::string message;
    MidiPlaybackSnapshot snapshot;
};

struct MidiTransportCommandState {
    bool playEnabled { false };
    bool stopEnabled { false };
    bool panicEnabled { true };
};

double midiBeatToSeconds(double beat, const std::vector<core::MidiTempoEvent>& tempoMap);
double midiSecondsToBeat(double seconds, const std::vector<core::MidiTempoEvent>& tempoMap);

MidiPlaybackSnapshotResult createMidiPlaybackSnapshot(const core::Track& track);
MidiPlaybackSnapshotResult createMidiPlaybackSnapshot(
    const core::Track& track,
    const std::vector<core::MidiTempoEvent>& playbackTempoMapOverride);
bool canAuditionMidiTrack(const core::Track& track);
MidiTransportCommandState midiTransportCommandState(
    const std::optional<core::Track>& primarySelectedTrack,
    bool isPlaying);

}  // namespace dawhermes::audio
