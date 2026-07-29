#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "audio/MidiPlaybackModel.h"
#include "audio/TransportModel.h"
#include "core/ProjectModel.h"
#include "core/SelectionState.h"

namespace dawhermes::audio {

constexpr std::uint64_t kMaximumDecodedWavBytes =
    512ULL * 1024ULL * 1024ULL;
constexpr int kWavDecodeBlockFrames = 4096;

std::optional<std::uint64_t> decodedWavBytes(
    std::uint64_t frameCount,
    int channelCount) noexcept;

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

struct ProjectPlaybackTrackIdentity {
    std::uint64_t trackId { 0 };
    std::uint64_t parentTrackId { 0 };
    core::TrackType type { core::TrackType::audio };

    bool operator==(
        const ProjectPlaybackTrackIdentity& other) const = default;
};

struct SelectionPlaybackSnapshot {
    std::optional<MidiPlaybackSnapshot> midi;
    std::vector<std::uint64_t> midiTrackIds;
    std::vector<AudioStemPlaybackSnapshot> audioStems;
    std::vector<ProjectPlaybackTrackIdentity>
        projectTrackRoutingIdentity;
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
    std::uint64_t estimatedSelectedAudioBytes { 0 };
    std::uint64_t decodedAudioBytes { 0 };
    bool selectedAudioByteEstimateOverflow { false };
};

struct SelectionPlaybackOptions {
    std::optional<double> detectedWavBpm;
    std::map<std::uint64_t, double> detectedWavBpms;
    double fallbackBpm { kFallbackPlaybackBpm };
    std::uint64_t maximumDecodedAudioBytes { kMaximumDecodedWavBytes };
};

struct PlayableSelectionIdentity {
    std::optional<std::uint64_t> primaryMidiTrackId;
    std::vector<std::uint64_t> midiTrackIds;
    std::vector<std::uint64_t> readableAudioTrackIds;

    bool operator==(const PlayableSelectionIdentity& other) const = default;
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
    PlayableSelectionIdentity identity;
    bool conflictingExplicitMidiTempo { false };
    std::string diagnostic;
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

SelectionPlaybackSnapshotResult createProjectPlaybackSnapshot(
    const core::ProjectModel& project,
    const SelectionPlaybackOptions& options = {});

SelectionPlaybackSummary createProjectPlaybackSummary(
    const core::ProjectModel& project,
    const SelectionPlaybackOptions& options = {});

SelectionTransportCommandState selectionTransportCommandState(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    TransportMode mode,
    double playableDurationSeconds);

SelectionTransportCommandState projectTransportCommandState(
    const core::ProjectModel& project,
    TransportMode mode,
    double playableDurationSeconds);
SelectionTransportCommandState projectTransportCommandState(
    bool projectPlayable,
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
double selectionSummaryBpm(
    double transportSeconds,
    const SelectionPlaybackSummary& summary);
bool hasExplicitMidiTempo(const core::Track& track) noexcept;
MidiResumeState createMidiResumeState(
    const MidiPlaybackSnapshot& snapshot,
    double transportSeconds);

double audioSourceFramePosition(
    double transportSeconds,
    double sourceSampleRate) noexcept;

std::string describeSelectionPlayback(const SelectionPlaybackSnapshot& snapshot);
std::string describeProjectPlayback(const SelectionPlaybackSnapshot& snapshot);
std::string describeSkippedAudioTrack(const SkippedAudioTrack& skipped);

}  // namespace dawhermes::audio
