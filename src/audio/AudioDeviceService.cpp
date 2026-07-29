#include "audio/AudioDeviceService.h"

#include <algorithm>
#include <cmath>
#include <memory>
#include <sstream>

#include <juce_core/juce_core.h>

namespace dawhermes::audio {

namespace {

constexpr const char* kSettingsRootTag =
    "DAWHermesAudioDeviceSettings";
constexpr int kSettingsVersion = 1;

std::string toUtf8(const juce::String& value)
{
    return {
        value.toRawUTF8(),
        static_cast<std::size_t>(value.getNumBytesAsUTF8())
    };
}

std::string errorOrFallback(
    const juce::String& error,
    const char* fallback)
{
    return error.isNotEmpty()
        ? toUtf8(error)
        : std::string(fallback);
}

void logAudio(const std::string& message)
{
    juce::Logger::writeToLog(
        "[DAWHermes] " + juce::String(message));
}

}  // namespace

std::string serializeAudioDeviceSettings(
    const AudioDeviceSettingsState& settings)
{
    juce::XmlElement root(kSettingsRootTag);
    root.setAttribute("version", kSettingsVersion);
    if (const auto deviceState = juce::parseXML(
            juce::String::fromUTF8(
                settings.deviceStateXml.data(),
                static_cast<int>(settings.deviceStateXml.size())));
        deviceState != nullptr) {
        root.addChildElement(new juce::XmlElement(*deviceState));
    }
    return toUtf8(root.toString());
}

std::optional<AudioDeviceSettingsState> deserializeAudioDeviceSettings(
    const std::string& serialized)
{
    if (serialized.empty()) {
        return std::nullopt;
    }
    const auto root = juce::parseXML(juce::String::fromUTF8(
        serialized.data(),
        static_cast<int>(serialized.size())));
    if (root == nullptr
        || !root->hasTagName(kSettingsRootTag)
        || root->getIntAttribute("version") != kSettingsVersion
        || root->getFirstChildElement() == nullptr) {
        return std::nullopt;
    }
    return AudioDeviceSettingsState {
        toUtf8(root->getFirstChildElement()->toString())
    };
}

bool AudioDeviceOpenResult::deviceAvailable() const noexcept
{
    return state != AudioDeviceOpenState::noDevice;
}

AudioDeviceOpenResult runAudioDeviceOpenSequence(
    bool hasSavedConfiguration,
    const std::function<std::string()>& restore,
    const std::function<std::string()>& openDefault)
{
    AudioDeviceOpenResult result;
    if (hasSavedConfiguration) {
        result.restoreAttempted = true;
        const auto restoreError = restore();
        if (restoreError.empty()) {
            result.state = AudioDeviceOpenState::restored;
            return result;
        }
        result.error = restoreError;
    }

    result.fallbackAttempted = true;
    const auto fallbackError = openDefault();
    if (fallbackError.empty()) {
        result.state = AudioDeviceOpenState::defaultFallback;
        return result;
    }

    result.state = AudioDeviceOpenState::noDevice;
    if (!result.error.empty()) {
        result.error += " | ";
    }
    result.error += fallbackError;
    return result;
}

std::string formatAudioDeviceStatus(
    const AudioDeviceStatus& status)
{
    std::ostringstream text;
    text << "Device system/type: "
         << (status.deviceType.empty() ? "None" : status.deviceType)
         << "\nOutput device: "
         << (status.outputDeviceName.empty()
                 ? "None"
                 : status.outputDeviceName)
         << "\nInput device: "
         << (status.inputDeviceName.empty()
                 ? "None"
                 : status.inputDeviceName)
         << "\nSample rate: ";
    if (status.sampleRate > 0.0) {
        text << std::llround(status.sampleRate) << " Hz";
    } else {
        text << "Unavailable";
    }
    text << "\nBuffer size: ";
    if (status.bufferSizeSamples > 0) {
        text << status.bufferSizeSamples << " samples";
    } else {
        text << "Unavailable";
    }
    text << "\nActive output channels: "
         << status.activeOutputChannels
         << "\nActive input channels: "
         << status.activeInputChannels
         << "\nOutput latency: "
         << status.outputLatencySamples
         << " samples\nInput latency: "
         << status.inputLatencySamples
         << " samples\nDevice state: "
         << (status.open
                 ? (status.running ? "Open / running" : "Open / stopped")
                 : "No device");
    if (!status.error.empty()) {
        text << "\nError: " << status.error;
    }
    return text.str();
}

std::string formatAudioDeviceSummary(
    const AudioDeviceStatus& status)
{
    if (!status.open || status.outputDeviceName.empty()) {
        return status.error.empty()
            ? "Audio: No output device"
            : "Audio: No output device (" + status.error + ")";
    }
    std::ostringstream text;
    text << "Audio: " << status.outputDeviceName;
    if (status.sampleRate > 0.0) {
        text << " | " << std::llround(status.sampleRate) << " Hz";
    }
    if (status.bufferSizeSamples > 0) {
        text << " | " << status.bufferSizeSamples << " samples";
    }
    return text.str();
}

AudioDeviceService::AudioDeviceService(
    juce::PropertiesFile* settings,
    bool initializeHardware)
    : settings_(settings),
      initializeHardware_(initializeHardware)
{
    deviceManager_.addChangeListener(this);
    deviceManager_.addAudioCallback(&playbackEngine_);
    callbackRegistered_ = true;
    if (initializeHardware_) {
        initialize();
    } else {
        setStatusMessage("Audio device initialization disabled.");
    }
}

AudioDeviceService::~AudioDeviceService()
{
    playbackEngine_.panic();
    if (callbackRegistered_) {
        deviceManager_.removeAudioCallback(&playbackEngine_);
        callbackRegistered_ = false;
    }
    deviceManager_.removeChangeListener(this);
    deviceManager_.closeAudioDevice();
}

MidiAuditionEngine& AudioDeviceService::playbackEngine() noexcept
{
    return playbackEngine_;
}

const MidiAuditionEngine&
AudioDeviceService::playbackEngine() const noexcept
{
    return playbackEngine_;
}

juce::AudioDeviceManager&
AudioDeviceService::deviceManager() noexcept
{
    return deviceManager_;
}

AudioDeviceOpenResult AudioDeviceService::initialize()
{
    playbackEngine_.panic();
    const auto saved = loadSettings();
    const auto result = runAudioDeviceOpenSequence(
        saved.has_value(),
        [this, &saved]() {
            return restoreSavedConfiguration(saved.value());
        },
        [this]() {
            return openDefaultOutput();
        });

    if (result.state == AudioDeviceOpenState::restored) {
        setStatusMessage(
            "Restored saved audio device configuration.");
    } else if (result.state
               == AudioDeviceOpenState::defaultFallback) {
        const auto message = saved.has_value()
            ? "Saved audio device was unavailable; using the default output."
            : "Using the default audio output.";
        setStatusMessage(message);
        if (saved.has_value()) {
            logAudio(
                "Audio device restore failed; default output fallback opened.");
        }
    } else {
        const auto message =
            "No audio output device is available: " + result.error;
        setStatusMessage(message);
        logAudio(message);
    }
    return result;
}

AudioDeviceOpenResult AudioDeviceService::restart()
{
    playbackEngine_.panic();
    const auto current = deviceManager_.createStateXml();
    AudioDeviceSettingsState state;
    if (current != nullptr) {
        state.deviceStateXml = toUtf8(current->toString());
    } else if (const auto saved = loadSettings(); saved.has_value()) {
        state = saved.value();
    }

    deviceManager_.closeAudioDevice();
    const auto result = runAudioDeviceOpenSequence(
        !state.deviceStateXml.empty(),
        [this, &state]() {
            return restoreSavedConfiguration(state);
        },
        [this]() {
            return openDefaultOutput();
        });

    if (result.state == AudioDeviceOpenState::restored) {
        setStatusMessage(formatAudioDeviceSummary(status()));
    } else if (result.state
               == AudioDeviceOpenState::defaultFallback) {
        setStatusMessage(formatAudioDeviceSummary(status()));
        logAudio(
            "Audio restart restore failed; default output fallback opened.");
    } else {
        const auto message =
            "Audio device restart failed: " + result.error;
        setStatusMessage(message);
        logAudio(message);
    }
    return result;
}

AudioDeviceStatus AudioDeviceService::status() const
{
    AudioDeviceStatus result;
    result.deviceType = toUtf8(
        deviceManager_.getCurrentAudioDeviceType());
    const auto setup = deviceManager_.getAudioDeviceSetup();
    result.outputDeviceName = toUtf8(setup.outputDeviceName);
    result.inputDeviceName = toUtf8(setup.inputDeviceName);

    auto* device = deviceManager_.getCurrentAudioDevice();
    if (device == nullptr) {
        return result;
    }

    result.open = device->isOpen();
    result.running = device->isPlaying();
    result.sampleRate = device->getCurrentSampleRate();
    result.bufferSizeSamples =
        device->getCurrentBufferSizeSamples();
    result.activeInputChannels =
        device->getActiveInputChannels().countNumberOfSetBits();
    result.activeOutputChannels =
        device->getActiveOutputChannels().countNumberOfSetBits();
    result.inputLatencySamples =
        device->getInputLatencyInSamples();
    result.outputLatencySamples =
        device->getOutputLatencyInSamples();
    result.error = toUtf8(device->getLastError());
    if (result.activeOutputChannels == 0
        && result.error.empty()) {
        result.error = "No active output channel.";
    }
    return result;
}

std::string AudioDeviceService::currentStatusMessage() const
{
    return statusMessage_;
}

std::uint64_t AudioDeviceService::statusGeneration() const noexcept
{
    return statusGeneration_.load(std::memory_order_acquire);
}

bool AudioDeviceService::testOutput()
{
    if (playbackEngine_.transportMode()
        != TransportMode::stopped) {
        setStatusMessage(
            "Test Output is disabled during project playback.");
        return false;
    }
    if (!playbackEngine_.startTestTone(0.5)) {
        setStatusMessage(
            "Test Output unavailable: no active audio output.");
        return false;
    }
    return true;
}

bool AudioDeviceService::isTestOutputActive() const noexcept
{
    return playbackEngine_.isTestToneActive();
}

int AudioDeviceService::registeredProjectCallbackCount() const noexcept
{
    return callbackRegistered_ ? 1 : 0;
}

void AudioDeviceService::changeListenerCallback(
    juce::ChangeBroadcaster*)
{
    if (playbackEngine_.transportMode()
        != TransportMode::stopped) {
        playbackEngine_.panic();
    }
    saveSettings();
    const auto current = status();
    if (!current.open || current.activeOutputChannels == 0) {
        playbackEngine_.stop();
        setStatusMessage(formatAudioDeviceSummary(current));
        logAudio(formatAudioDeviceStatus(current));
        return;
    }
    setStatusMessage(formatAudioDeviceSummary(current));
}

std::optional<AudioDeviceSettingsState>
AudioDeviceService::loadSettings() const
{
    if (settings_ == nullptr) {
        return std::nullopt;
    }
    return deserializeAudioDeviceSettings(toUtf8(
        settings_->getValue(kAudioDeviceSettingsKey)));
}

void AudioDeviceService::saveSettings()
{
    if (settings_ == nullptr) {
        return;
    }
    const auto state = deviceManager_.createStateXml();
    if (state == nullptr) {
        return;
    }
    const auto serialized = serializeAudioDeviceSettings({
        toUtf8(state->toString())
    });
    settings_->setValue(
        kAudioDeviceSettingsKey,
        juce::String::fromUTF8(serialized.data(),
                               static_cast<int>(serialized.size())));
    settings_->saveIfNeeded();
}

std::string AudioDeviceService::restoreSavedConfiguration(
    const AudioDeviceSettingsState& settings)
{
    const auto state = juce::parseXML(
        juce::String::fromUTF8(
            settings.deviceStateXml.data(),
            static_cast<int>(settings.deviceStateXml.size())));
    if (state == nullptr) {
        return "Saved audio-device settings are invalid.";
    }
    const auto error = deviceManager_.initialise(
        0,
        2,
        state.get(),
        false);
    if (error.isNotEmpty()
        || deviceManager_.getCurrentAudioDevice() == nullptr) {
        return errorOrFallback(
            error,
            "Saved audio output device is unavailable.");
    }
    return {};
}

std::string AudioDeviceService::openDefaultOutput()
{
    deviceManager_.closeAudioDevice();
    const auto error =
        deviceManager_.initialiseWithDefaultDevices(0, 2);
    if (error.isNotEmpty()
        || deviceManager_.getCurrentAudioDevice() == nullptr) {
        return errorOrFallback(
            error,
            "No default audio output device is available.");
    }
    return {};
}

void AudioDeviceService::setStatusMessage(std::string message)
{
    statusMessage_ = std::move(message);
    statusGeneration_.fetch_add(1, std::memory_order_acq_rel);
}

}  // namespace dawhermes::audio
