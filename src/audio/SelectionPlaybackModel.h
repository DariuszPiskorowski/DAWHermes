#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "audio/MidiPlaybackModel.h"
#include "audio/TransportModel.h"
#include "core/ProjectModel.h"
#include "core/SelectionState.h"

namespace dawhermes::audio {

struct AudioStemPlaybackSnapshot {
    std::uint64_t sourceTrackId { 0 };
    std::string sourceTrackName;
    std::string sourcePath;
    double sourceSampleRate { 0.0 };
    std::uint64_t frameCount { 0 };
    std::vector<std::vector<float>> channels;

    double durationSeconds() const noexcept;
    bool isPlayable() const noexcept;
};

struct SkippedAudioTrack {
    std::uint64_t sourceTrackId { 0 };
    std::string sourceTrackName;
    std::string sourceFileName;
    std::string reason;
};

struct SelectionPlaybackSnapshot {
    std::optional<MidiPlaybackSnapshot> midi;
    std::vector<AudioStemPlaybackSnapshot> audioStems;
    std::vector<core::MidiTempoEvent> playheadTempoMap;
    PlaybackTempoSource tempoSource { PlaybackTempoSource::fallback };
    double durationSeconds { 0.0 };

    bool isPlayable() const noexcept;
    std::size_t midiTrackCount() const noexcept;
    std::size_t audioTrackCount() const noexcept;
};

struct SelectionPlaybackSnapshotResult {
    bool ok { false };
    std::string message;
    SelectionPlaybackSnapshot snapshot;
    std::vector<SkippedAudioTrack> skippedAudioTracks;
};

struct SelectionPlaybackOptions {
    std::optional<double> detectedWavBpm;
    double fallbackBpm { kFallbackPlaybackBpm };
};

struct SelectionPlaybackSummary {
    bool playable { false };
    std::size_t midiTrackCount { 0 };
    std::size_t audioTrackCount { 0 };
    double durationSeconds { 0.0 };
    std::vector<core::MidiTempoEvent> tempoMap;
    PlaybackTempoSource tempoSource { PlaybackTempoSource::fallback };
    std::optional<std::string> firstReadableAudioPath;
    std::vector<SkippedAudioTrack> skippedAudioTracks;
};

struct MidiResumeState {
    std::size_t nextEventIndex { 0 };
    std::vector<MidiPlaybackEvent> activeNoteOns;
};

SelectionPlaybackSnapshotResult createSelectionPlaybackSnapshot(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    const SelectionPlaybackOptions& options = {});

SelectionPlaybackSummary createSelectionPlaybackSummary(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    const SelectionPlaybackOptions& options = {});

SelectionTransportCommandState selectionTransportCommandState(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    TransportMode mode,
    double playableDurationSeconds);

double selectionPlayheadBeat(
    double transportSeconds,
    const SelectionPlaybackSnapshot& snapshot);

double playbackBpmAtBeat(
    double beat,
    const std::vector<core::MidiTempoEvent>& tempoMap);
double selectionPlaybackBpm(
    double transportSeconds,
    const SelectionPlaybackSnapshot& snapshot);
bool hasExplicitMidiTempo(const core::Track& track) noexcept;
MidiResumeState createMidiResumeState(
    const MidiPlaybackSnapshot& snapshot,
    double transportSeconds);

double audioSourceFramePosition(
    double transportSeconds,
    double sourceSampleRate) noexcept;

std::string describeSelectionPlayback(const SelectionPlaybackSnapshot& snapshot);
std::string describeSkippedAudioTrack(const SkippedAudioTrack& skipped);

}  // namespace dawhermes::audio
