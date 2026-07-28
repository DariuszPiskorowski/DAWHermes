#pragma once

#include <atomic>
#include <memory>
#include <string>

namespace dawhermes::audio {

struct SelectionPlaybackSnapshot;

enum class TransportMode {
    stopped,
    playing,
    paused
};

enum class PlaybackTempoSource {
    explicitMidi,
    detectedWav,
    fallback
};

constexpr double kFallbackPlaybackBpm = 120.0;
constexpr double kTransportSeekSeconds = 5.0;

struct SelectionTransportCommandState {
    bool rewindEnabled { false };
    bool playEnabled { false };
    bool pauseEnabled { false };
    bool stopEnabled { false };
    bool fastForwardEnabled { false };
    bool panicEnabled { true };
};

class SharedTransportState {
public:
    void setPreviewDuration(double durationSeconds) noexcept;
    void prepare(
        std::shared_ptr<const SelectionPlaybackSnapshot> snapshot,
        double startSeconds) noexcept;
    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;
    void panic() noexcept;
    void complete() noexcept;
    void seek(double targetSeconds) noexcept;
    void updatePositionFromAudio(double currentSeconds) noexcept;

    TransportMode mode() const noexcept;
    bool isPlaying() const noexcept;
    bool isPaused() const noexcept;
    double currentSeconds() const noexcept;
    double totalSeconds() const noexcept;
    bool hasPreparedPlayback() const noexcept;
    std::shared_ptr<const SelectionPlaybackSnapshot> snapshot() const noexcept;

private:
    std::atomic<std::shared_ptr<const SelectionPlaybackSnapshot>> snapshot_;
    std::atomic<TransportMode> mode_ { TransportMode::stopped };
    std::atomic<double> currentSeconds_ { 0.0 };
    std::atomic<double> totalSeconds_ { 0.0 };
};

double clampTransportSeconds(double seconds, double totalSeconds) noexcept;
double seekTransportSeconds(
    double currentSeconds,
    double deltaSeconds,
    double totalSeconds) noexcept;

bool shouldAutomaticallyFollowPlayhead(TransportMode mode) noexcept;

std::string formatTransportCounter(
    double currentSeconds,
    double totalSeconds);

}  // namespace dawhermes::audio
