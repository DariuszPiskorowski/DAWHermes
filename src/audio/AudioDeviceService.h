#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <optional>
#include <string>

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_data_structures/juce_data_structures.h>

#include "audio/MidiAuditionEngine.h"
#include "plugins/Vst3InstrumentHost.h"

namespace dawhermes::audio {

constexpr const char* kAudioDeviceSettingsKey =
    "audio.deviceStateV1";

struct AudioDeviceSettingsState {
    std::string deviceStateXml;

    bool operator==(const AudioDeviceSettingsState& other) const = default;
};

std::string serializeAudioDeviceSettings(
    const AudioDeviceSettingsState& settings);
std::optional<AudioDeviceSettingsState> deserializeAudioDeviceSettings(
    const std::string& serialized);

enum class AudioDeviceOpenState {
    restored,
    defaultFallback,
    noDevice
};

struct AudioDeviceOpenResult {
    AudioDeviceOpenState state { AudioDeviceOpenState::noDevice };
    bool restoreAttempted { false };
    bool fallbackAttempted { false };
    std::string error;

    bool deviceAvailable() const noexcept;
};

AudioDeviceOpenResult runAudioDeviceOpenSequence(
    bool hasSavedConfiguration,
    const std::function<std::string()>& restore,
    const std::function<std::string()>& openDefault);

struct AudioDeviceStatus {
    std::string deviceType;
    std::string outputDeviceName;
    std::string inputDeviceName;
    double sampleRate { 0.0 };
    int bufferSizeSamples { 0 };
    int activeInputChannels { 0 };
    int activeOutputChannels { 0 };
    int inputLatencySamples { 0 };
    int outputLatencySamples { 0 };
    bool open { false };
    bool running { false };
    std::string error;
};

std::string formatAudioDeviceStatus(
    const AudioDeviceStatus& status);
std::string formatAudioDeviceSummary(
    const AudioDeviceStatus& status);

class AudioDeviceService final : private juce::ChangeListener {
public:
    explicit AudioDeviceService(
        juce::PropertiesFile* settings,
        bool initializeHardware = true);
    ~AudioDeviceService() override;

    AudioDeviceService(const AudioDeviceService&) = delete;
    AudioDeviceService& operator=(const AudioDeviceService&) = delete;

    MidiAuditionEngine& playbackEngine() noexcept;
    const MidiAuditionEngine& playbackEngine() const noexcept;
    juce::AudioDeviceManager& deviceManager() noexcept;
    plugins::Vst3InstrumentHost& instrumentHost() noexcept;
    const plugins::Vst3InstrumentHost& instrumentHost() const noexcept;

    AudioDeviceOpenResult initialize();
    AudioDeviceOpenResult restart();
    AudioDeviceStatus status() const;
    std::string currentStatusMessage() const;
    std::uint64_t statusGeneration() const noexcept;

    bool testOutput();
    bool isTestOutputActive() const noexcept;
    int registeredProjectCallbackCount() const noexcept;

private:
    void changeListenerCallback(
        juce::ChangeBroadcaster* source) override;
    std::optional<AudioDeviceSettingsState> loadSettings() const;
    void saveSettings();
    std::string restoreSavedConfiguration(
        const AudioDeviceSettingsState& settings);
    std::string openDefaultOutput();
    void setStatusMessage(std::string message);

    juce::PropertiesFile* settings_ { nullptr };
    juce::AudioDeviceManager deviceManager_;
    plugins::Vst3InstrumentHost instrumentHost_;
    MidiAuditionEngine playbackEngine_;
    bool callbackRegistered_ { false };
    bool initializeHardware_ { true };
    std::string statusMessage_;
    std::atomic<std::uint64_t> statusGeneration_ { 0 };
};

}  // namespace dawhermes::audio
