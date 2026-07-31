#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include <juce_audio_devices/juce_audio_devices.h>

#include "audio/MidiPlaybackModel.h"
#include "audio/SelectionPlaybackModel.h"
#include "core/TimelineLoop.h"
#include "core/TrackRouting.h"

namespace dawhermes::plugins {
class Vst3InstrumentHost;
}

namespace dawhermes::audio {

class MidiAuditionEngine final : public juce::AudioIODeviceCallback {
public:
    explicit MidiAuditionEngine(
        plugins::Vst3InstrumentHost* instrumentHost = nullptr);
    ~MidiAuditionEngine() override;

    MidiAuditionEngine(const MidiAuditionEngine&) = delete;
    MidiAuditionEngine& operator=(const MidiAuditionEngine&) = delete;

    bool startPlayback(MidiPlaybackSnapshot snapshot, std::string& error);
    bool startPlayback(SelectionPlaybackSnapshot snapshot, std::string& error);
    bool startPlayback(
        SelectionPlaybackSnapshot snapshot,
        double startSeconds,
        std::string& error);
    bool resume(std::string& error);
    void pause();
    void stop();
    void panic();
    void seekTo(double targetSeconds);
    void setPreviewDuration(
        double durationSeconds,
        std::uint64_t playableSelectionGeneration = 0);

    void setVolume(float normalizedVolume);
    float volume() const noexcept;
    TransportMode transportMode() const noexcept;
    bool isPlaying() const noexcept;
    bool isPaused() const noexcept;
    bool isAudioDeviceReady() const noexcept;
    double playheadSeconds() const noexcept;
    double totalDurationSeconds() const noexcept;
    bool isPlayheadVisible() const noexcept;
    double playheadBeat() const;
    bool hasPreparedPlayback() const noexcept;
    std::shared_ptr<const SelectionPlaybackSnapshot> playbackSnapshot() const noexcept;
    void collectRetiredSnapshots();
    void setProjectRoutingState(core::ProjectRoutingState routing);
    void setTimelineLoop(
        std::optional<core::TimelineLoopRange> range,
        bool enabled);
    bool isTimelineLoopEnabled() const noexcept;
    bool startTestTone(double durationSeconds = 0.5) noexcept;
    bool isTestToneActive() const noexcept;
    void prepareForOfflineTesting(double sampleRate);
    void renderOfflineForTesting(
        float* left,
        float* right,
        int numSamples);
    std::size_t activeVoiceCountForTesting() const noexcept;

private:
    struct StereoSample {
        float left { 0.0f };
        float right { 0.0f };
    };

    struct Voice {
        bool active { false };
        bool releasing { false };
        std::uint64_t noteInstanceId { 0 };
        std::uint64_t sourceTrackId { 0 };
        double phase { 0.0 };
        double phaseIncrement { 0.0 };
        float amplitude { 0.0f };
    };

    struct PlaybackCursor {
        std::shared_ptr<const SelectionPlaybackSnapshot> snapshot;
        double startSeconds { 0.0 };
        std::size_t nextEventIndex { 0 };
        std::array<MidiPlaybackEvent, 64> activeNoteOns {};
        std::size_t activeNoteCount { 0 };
    };

    struct PreparedLoop {
        bool enabled { false };
        core::TimelineLoopRange beats;
        double startSeconds { 0.0 };
        double endSeconds { 0.0 };
        std::size_t nextEventIndex { 0 };
        std::array<MidiPlaybackEvent, 64> activeNoteOns {};
        std::size_t activeNoteCount { 0 };
    };

    std::shared_ptr<const PlaybackCursor> buildCursor(double startSeconds) const;
    std::shared_ptr<const PreparedLoop> buildLoop(
        std::optional<core::TimelineLoopRange> range,
        bool enabled) const;
    void publishCursor(double startSeconds);
    bool isTrackAudible(std::uint64_t trackId) const noexcept;
    void reconstructVoices(
        const MidiPlaybackEvent* events,
        std::size_t count,
        int sampleOffset = 0) noexcept;
    void clearVoices() noexcept;
    void startVoice(const MidiPlaybackEvent& event) noexcept;
    void releaseVoice(std::uint64_t noteInstanceId) noexcept;
    float renderVoices() noexcept;
    StereoSample renderAudioStems(double transportSeconds) const noexcept;
    bool hasActiveVoices() const noexcept;
    void handleMidiEvent(
        const MidiPlaybackEvent& event,
        int sampleOffset) noexcept;
    StereoSample delayDryMix(
        StereoSample input,
        int delaySamples) noexcept;
    void resetDryDelay() noexcept;

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    std::atomic<bool> deviceReady_ { false };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> panicRequested_ { false };
    std::atomic<bool> pauseRequested_ { false };
    std::atomic<float> volume_ { 0.25f };
    std::atomic<std::int64_t> testToneSamplesRemaining_ { 0 };
    std::atomic<double> reportedSampleRate_ { 44100.0 };
    std::atomic<std::uint64_t> requestedGeneration_ { 0 };
    std::atomic<std::shared_ptr<const PlaybackCursor>> requestedCursor_;
    std::atomic<std::uint64_t> requestedRoutingGeneration_ { 0 };
    std::atomic<std::shared_ptr<const core::ProjectRoutingState>>
        requestedRouting_;
    std::atomic<std::uint64_t> requestedLoopGeneration_ { 0 };
    std::atomic<std::shared_ptr<const PreparedLoop>> requestedLoop_;
    std::atomic<bool> loopEnabled_ { false };

    SharedTransportState transportState_;
    std::vector<std::shared_ptr<const SelectionPlaybackSnapshot>> retainedSnapshots_;
    std::vector<std::shared_ptr<const core::ProjectRoutingState>>
        retainedRoutingStates_;
    std::vector<std::shared_ptr<const PreparedLoop>>
        retainedLoopStates_;
    std::shared_ptr<const SelectionPlaybackSnapshot> activeSnapshot_;
    std::shared_ptr<const core::ProjectRoutingState> activeRouting_;
    std::shared_ptr<const PreparedLoop> activeLoop_;
    std::uint64_t activeGeneration_ { 0 };
    std::uint64_t activeRoutingGeneration_ { 0 };
    std::uint64_t activeLoopGeneration_ { 0 };
    std::size_t nextEventIndex_ { 0 };
    std::uint64_t playbackSamplePosition_ { 0 };
    double sampleRate_ { 44100.0 };
    double testTonePhase_ { 0.0 };
    float releaseMultiplier_ { 0.99f };
    std::array<Voice, 64> voices_ {};
    plugins::Vst3InstrumentHost* instrumentHost_ { nullptr };
    std::array<std::vector<float>, 2> dryDelay_;
    std::size_t dryDelayWrite_ { 0 };
    std::size_t dryDelayValidSamples_ { 0 };
    std::uint64_t activeInstrumentGeneration_ { 0 };
};

}  // namespace dawhermes::audio
