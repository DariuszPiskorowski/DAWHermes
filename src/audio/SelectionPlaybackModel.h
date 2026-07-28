#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "audio/MidiPlaybackModel.h"
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

struct SelectionTransportCommandState {
    bool playEnabled { false };
    bool stopEnabled { false };
    bool panicEnabled { true };
};

class SelectionPlaybackState {
public:
    void start(std::shared_ptr<const SelectionPlaybackSnapshot> snapshot) noexcept;
    void stop() noexcept;
    void panic() noexcept;
    void finish() noexcept;

    bool isPlaying() const noexcept;
    bool hasPreparedPlayback() const noexcept;
    std::shared_ptr<const SelectionPlaybackSnapshot> snapshot() const noexcept;

private:
    std::atomic<std::shared_ptr<const SelectionPlaybackSnapshot>> snapshot_;
    std::atomic<bool> playing_ { false };
};

SelectionPlaybackSnapshotResult createSelectionPlaybackSnapshot(
    const core::ProjectModel& project,
    const core::SelectionState& selection);

SelectionTransportCommandState selectionTransportCommandState(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    bool isPlaying);

double selectionPlayheadBeat(
    double transportSeconds,
    const SelectionPlaybackSnapshot& snapshot);

double audioSourceFramePosition(
    double transportSeconds,
    double sourceSampleRate) noexcept;

std::string describeSelectionPlayback(const SelectionPlaybackSnapshot& snapshot);
std::string describeSkippedAudioTrack(const SkippedAudioTrack& skipped);

}  // namespace dawhermes::audio
