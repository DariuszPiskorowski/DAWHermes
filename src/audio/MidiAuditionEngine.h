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

namespace dawhermes::audio {

class MidiAuditionEngine final : private juce::AudioIODeviceCallback {
public:
    MidiAuditionEngine() = default;
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

private:
    struct StereoSample {
        float left { 0.0f };
        float right { 0.0f };
    };

    struct Voice {
        bool active { false };
        bool releasing { false };
        std::uint64_t noteInstanceId { 0 };
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

    bool initializeDefaultAudioDevice(std::string& error);
    std::shared_ptr<const PlaybackCursor> buildCursor(double startSeconds) const;
    void publishCursor(double startSeconds);
    void clearVoices() noexcept;
    void startVoice(const MidiPlaybackEvent& event) noexcept;
    void releaseVoice(std::uint64_t noteInstanceId) noexcept;
    float renderVoices() noexcept;
    StereoSample renderAudioStems(double transportSeconds) const noexcept;
    bool hasActiveVoices() const noexcept;

    void audioDeviceIOCallbackWithContext(
        const float* const* inputChannelData,
        int numInputChannels,
        float* const* outputChannelData,
        int numOutputChannels,
        int numSamples,
        const juce::AudioIODeviceCallbackContext& context) override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

    juce::AudioDeviceManager deviceManager_;
    bool callbackRegistered_ { false };
    std::atomic<bool> deviceReady_ { false };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> panicRequested_ { false };
    std::atomic<bool> pauseRequested_ { false };
    std::atomic<float> volume_ { 0.25f };
    std::atomic<std::uint64_t> requestedGeneration_ { 0 };
    std::atomic<std::shared_ptr<const PlaybackCursor>> requestedCursor_;

    SharedTransportState transportState_;
    std::vector<std::shared_ptr<const SelectionPlaybackSnapshot>> retainedSnapshots_;
    std::shared_ptr<const SelectionPlaybackSnapshot> activeSnapshot_;
    std::uint64_t activeGeneration_ { 0 };
    std::size_t nextEventIndex_ { 0 };
    std::uint64_t playbackSamplePosition_ { 0 };
    double sampleRate_ { 44100.0 };
    float releaseMultiplier_ { 0.99f };
    std::array<Voice, 64> voices_ {};
};

}  // namespace dawhermes::audio
