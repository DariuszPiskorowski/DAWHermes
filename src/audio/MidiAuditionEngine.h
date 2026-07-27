#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <string>

#include <juce_audio_devices/juce_audio_devices.h>

#include "audio/MidiPlaybackModel.h"

namespace dawhermes::audio {

class MidiAuditionEngine final : private juce::AudioIODeviceCallback {
public:
    MidiAuditionEngine() = default;
    ~MidiAuditionEngine() override;

    MidiAuditionEngine(const MidiAuditionEngine&) = delete;
    MidiAuditionEngine& operator=(const MidiAuditionEngine&) = delete;

    bool startPlayback(MidiPlaybackSnapshot snapshot, std::string& error);
    void stop();
    void panic();

    void setVolume(float normalizedVolume);
    float volume() const noexcept;
    bool isPlaying() const noexcept;
    bool isAudioDeviceReady() const noexcept;
    double playheadSeconds() const noexcept;
    double playheadBeat() const;

private:
    struct Voice {
        bool active { false };
        bool releasing { false };
        std::uint64_t noteInstanceId { 0 };
        double phase { 0.0 };
        double phaseIncrement { 0.0 };
        float amplitude { 0.0f };
    };

    bool initializeDefaultAudioDevice(std::string& error);
    void clearVoices() noexcept;
    void startVoice(const MidiPlaybackEvent& event) noexcept;
    void releaseVoice(std::uint64_t noteInstanceId) noexcept;
    float renderVoices() noexcept;
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
    std::atomic<bool> playing_ { false };
    std::atomic<bool> stopRequested_ { false };
    std::atomic<bool> panicRequested_ { false };
    std::atomic<float> volume_ { 0.25f };
    std::atomic<double> playheadSeconds_ { 0.0 };
    std::atomic<std::uint64_t> requestedGeneration_ { 0 };

    std::atomic<std::shared_ptr<const MidiPlaybackSnapshot>> requestedSnapshot_;
    std::shared_ptr<const MidiPlaybackSnapshot> activeSnapshot_;
    std::uint64_t activeGeneration_ { 0 };
    std::size_t nextEventIndex_ { 0 };
    std::uint64_t playbackSamplePosition_ { 0 };
    double sampleRate_ { 44100.0 };
    float releaseMultiplier_ { 0.99f };
    std::array<Voice, 64> voices_ {};
};

}  // namespace dawhermes::audio
