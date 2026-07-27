#include "audio/MidiAuditionEngine.h"

#include <algorithm>
#include <cmath>

namespace dawhermes::audio {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kReleaseSeconds = 0.02;
constexpr float kSynthGain = 0.18f;

double frequencyForMidiPitch(int pitch)
{
    return 440.0 * std::pow(2.0, (static_cast<double>(std::clamp(pitch, 0, 127)) - 69.0) / 12.0);
}

}  // namespace

MidiAuditionEngine::~MidiAuditionEngine()
{
    stop();
    if (callbackRegistered_) {
        deviceManager_.removeAudioCallback(this);
        callbackRegistered_ = false;
    }
    deviceManager_.closeAudioDevice();
}

bool MidiAuditionEngine::initializeDefaultAudioDevice(std::string& error)
{
    error.clear();
    if (deviceReady_.load(std::memory_order_acquire)) {
        return true;
    }

    const auto initializationError = deviceManager_.initialiseWithDefaultDevices(0, 2);
    if (initializationError.isNotEmpty() || deviceManager_.getCurrentAudioDevice() == nullptr) {
        error = initializationError.isNotEmpty()
            ? initializationError.toStdString()
            : "No default audio output device is available.";
        return false;
    }

    if (!callbackRegistered_) {
        deviceManager_.addAudioCallback(this);
        callbackRegistered_ = true;
    }

    deviceReady_.store(true, std::memory_order_release);
    return true;
}

bool MidiAuditionEngine::startPlayback(MidiPlaybackSnapshot snapshot, std::string& error)
{
    if (snapshot.events.empty()) {
        error = "The MIDI playback snapshot is empty.";
        return false;
    }

    if (!initializeDefaultAudioDevice(error)) {
        playing_.store(false, std::memory_order_release);
        return false;
    }

    auto immutableSnapshot = std::make_shared<const MidiPlaybackSnapshot>(std::move(snapshot));
    requestedSnapshot_.store(std::move(immutableSnapshot), std::memory_order_release);
    stopRequested_.store(false, std::memory_order_release);
    panicRequested_.store(false, std::memory_order_release);
    playheadSeconds_.store(0.0, std::memory_order_release);
    requestedGeneration_.fetch_add(1, std::memory_order_acq_rel);
    playing_.store(true, std::memory_order_release);
    return true;
}

void MidiAuditionEngine::stop()
{
    stopRequested_.store(true, std::memory_order_release);
    playing_.store(false, std::memory_order_release);
    playheadSeconds_.store(0.0, std::memory_order_release);
}

void MidiAuditionEngine::panic()
{
    panicRequested_.store(true, std::memory_order_release);
    playing_.store(false, std::memory_order_release);
    playheadSeconds_.store(0.0, std::memory_order_release);
}

void MidiAuditionEngine::setVolume(float normalizedVolume)
{
    volume_.store(std::clamp(normalizedVolume, 0.0f, 1.0f), std::memory_order_release);
}

float MidiAuditionEngine::volume() const noexcept
{
    return volume_.load(std::memory_order_acquire);
}

bool MidiAuditionEngine::isPlaying() const noexcept
{
    return playing_.load(std::memory_order_acquire);
}

bool MidiAuditionEngine::isAudioDeviceReady() const noexcept
{
    return deviceReady_.load(std::memory_order_acquire);
}

double MidiAuditionEngine::playheadSeconds() const noexcept
{
    return playheadSeconds_.load(std::memory_order_acquire);
}

double MidiAuditionEngine::playheadBeat() const
{
    const auto snapshot = requestedSnapshot_.load(std::memory_order_acquire);
    return snapshot == nullptr ? 0.0 : midiSecondsToBeat(playheadSeconds(), snapshot->tempoMap);
}

void MidiAuditionEngine::clearVoices() noexcept
{
    voices_.fill(Voice {});
}

void MidiAuditionEngine::startVoice(const MidiPlaybackEvent& event) noexcept
{
    auto voiceIt = std::find_if(voices_.begin(), voices_.end(), [](const auto& candidate) {
        return !candidate.active;
    });
    if (voiceIt == voices_.end()) {
        voiceIt = voices_.begin();
    }
    auto* voice = &(*voiceIt);

    voice->active = true;
    voice->releasing = false;
    voice->noteInstanceId = event.noteInstanceId;
    voice->phase = 0.0;
    voice->phaseIncrement = kTwoPi * frequencyForMidiPitch(event.pitch) / std::max(1.0, sampleRate_);
    voice->amplitude = std::clamp(event.amplitude, 0.0f, 1.0f) * kSynthGain;
}

void MidiAuditionEngine::releaseVoice(std::uint64_t noteInstanceId) noexcept
{
    for (auto& voice : voices_) {
        if (voice.active && voice.noteInstanceId == noteInstanceId) {
            voice.releasing = true;
        }
    }
}

float MidiAuditionEngine::renderVoices() noexcept
{
    float sample = 0.0f;
    for (auto& voice : voices_) {
        if (!voice.active) {
            continue;
        }

        sample += static_cast<float>(std::sin(voice.phase)) * voice.amplitude;
        voice.phase += voice.phaseIncrement;
        if (voice.phase >= kTwoPi) {
            voice.phase -= kTwoPi;
        }

        if (voice.releasing) {
            voice.amplitude *= releaseMultiplier_;
            if (voice.amplitude < 0.00001f) {
                voice = Voice {};
            }
        }
    }

    return std::clamp(sample * volume_.load(std::memory_order_relaxed), -0.95f, 0.95f);
}

bool MidiAuditionEngine::hasActiveVoices() const noexcept
{
    return std::any_of(voices_.begin(), voices_.end(), [](const auto& voice) {
        return voice.active;
    });
}

void MidiAuditionEngine::audioDeviceIOCallbackWithContext(
    const float* const*,
    int,
    float* const* outputChannelData,
    int numOutputChannels,
    int numSamples,
    const juce::AudioIODeviceCallbackContext&)
{
    for (int channel = 0; channel < numOutputChannels; ++channel) {
        if (outputChannelData[channel] != nullptr) {
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
        }
    }

    if (stopRequested_.exchange(false, std::memory_order_acq_rel)
        || panicRequested_.exchange(false, std::memory_order_acq_rel)) {
        clearVoices();
        activeSnapshot_.reset();
        nextEventIndex_ = 0;
        playbackSamplePosition_ = 0;
        playing_.store(false, std::memory_order_release);
        playheadSeconds_.store(0.0, std::memory_order_release);
        return;
    }

    const auto requestedGeneration = requestedGeneration_.load(std::memory_order_acquire);
    if (requestedGeneration != activeGeneration_) {
        activeSnapshot_ = requestedSnapshot_.load(std::memory_order_acquire);
        activeGeneration_ = requestedGeneration;
        nextEventIndex_ = 0;
        playbackSamplePosition_ = 0;
        clearVoices();
    }

    if (!playing_.load(std::memory_order_acquire) || activeSnapshot_ == nullptr) {
        return;
    }

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex) {
        const auto currentSeconds = static_cast<double>(playbackSamplePosition_) / std::max(1.0, sampleRate_);
        while (nextEventIndex_ < activeSnapshot_->events.size()
               && activeSnapshot_->events[nextEventIndex_].timeSeconds <= currentSeconds + 1.0e-9) {
            const auto& event = activeSnapshot_->events[nextEventIndex_];
            if (event.kind == MidiPlaybackEventKind::noteOff) {
                releaseVoice(event.noteInstanceId);
            } else {
                startVoice(event);
            }
            ++nextEventIndex_;
        }

        const auto outputSample = renderVoices();
        for (int channel = 0; channel < numOutputChannels; ++channel) {
            if (outputChannelData[channel] != nullptr) {
                outputChannelData[channel][sampleIndex] = outputSample;
            }
        }

        ++playbackSamplePosition_;
    }

    const auto currentSeconds = static_cast<double>(playbackSamplePosition_) / std::max(1.0, sampleRate_);
    playheadSeconds_.store(
        std::min(currentSeconds, activeSnapshot_->durationSeconds),
        std::memory_order_release);

    if (nextEventIndex_ >= activeSnapshot_->events.size() && !hasActiveVoices()) {
        playing_.store(false, std::memory_order_release);
        playheadSeconds_.store(activeSnapshot_->durationSeconds, std::memory_order_release);
    }
}

void MidiAuditionEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate_ = device == nullptr ? 44100.0 : std::max(1.0, device->getCurrentSampleRate());
    releaseMultiplier_ = static_cast<float>(
        std::exp(std::log(0.001) / (kReleaseSeconds * sampleRate_)));
    clearVoices();
}

void MidiAuditionEngine::audioDeviceStopped()
{
    clearVoices();
    playing_.store(false, std::memory_order_release);
    deviceReady_.store(false, std::memory_order_release);
}

}  // namespace dawhermes::audio
