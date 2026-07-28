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
    SelectionPlaybackSnapshot selectionSnapshot;
    selectionSnapshot.playheadTempoMap = snapshot.tempoMap;
    selectionSnapshot.durationSeconds = snapshot.durationSeconds;
    selectionSnapshot.midi = std::move(snapshot);
    return startPlayback(std::move(selectionSnapshot), error);
}

bool MidiAuditionEngine::startPlayback(SelectionPlaybackSnapshot snapshot, std::string& error)
{
    if (!snapshot.isPlayable()) {
        error = "The selection playback snapshot is empty.";
        return false;
    }

    if (!initializeDefaultAudioDevice(error)) {
        playbackState_.finish();
        return false;
    }

    stopRequested_.store(false, std::memory_order_release);
    panicRequested_.store(false, std::memory_order_release);
    playheadSeconds_.store(0.0, std::memory_order_release);
    auto immutableSnapshot = std::make_shared<const SelectionPlaybackSnapshot>(std::move(snapshot));
    retainedSnapshots_.push_back(immutableSnapshot);
    playbackState_.start(std::move(immutableSnapshot));
    requestedGeneration_.fetch_add(1, std::memory_order_acq_rel);
    return true;
}

void MidiAuditionEngine::stop()
{
    stopRequested_.store(true, std::memory_order_release);
    playbackState_.stop();
    playheadSeconds_.store(0.0, std::memory_order_release);
}

void MidiAuditionEngine::panic()
{
    panicRequested_.store(true, std::memory_order_release);
    playbackState_.panic();
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
    return playbackState_.isPlaying();
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
    const auto snapshot = playbackState_.snapshot();
    return snapshot == nullptr
        ? 0.0
        : selectionPlayheadBeat(playheadSeconds(), *snapshot);
}

bool MidiAuditionEngine::hasPreparedPlayback() const noexcept
{
    return playbackState_.hasPreparedPlayback();
}

void MidiAuditionEngine::collectRetiredSnapshots()
{
    retainedSnapshots_.erase(
        std::remove_if(
            retainedSnapshots_.begin(),
            retainedSnapshots_.end(),
            [](const auto& snapshot) { return snapshot.use_count() == 1; }),
        retainedSnapshots_.end());
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

    return sample;
}

MidiAuditionEngine::StereoSample MidiAuditionEngine::renderAudioStems(double transportSeconds) const noexcept
{
    StereoSample mix;
    if (activeSnapshot_ == nullptr) {
        return mix;
    }

    for (const auto& stem : activeSnapshot_->audioStems) {
        if (!stem.isPlayable()) {
            continue;
        }

        const auto sourcePosition = audioSourceFramePosition(
            transportSeconds,
            stem.sourceSampleRate);
        if (sourcePosition >= static_cast<double>(stem.frameCount)) {
            continue;
        }

        const auto firstFrame = static_cast<std::uint64_t>(std::floor(sourcePosition));
        const auto secondFrame = std::min(firstFrame + 1, stem.frameCount - 1);
        const auto fraction = static_cast<float>(sourcePosition - static_cast<double>(firstFrame));
        const auto interpolate = [firstFrame, secondFrame, fraction](const auto& samples) {
            const auto first = samples[static_cast<std::size_t>(firstFrame)];
            const auto second = samples[static_cast<std::size_t>(secondFrame)];
            return first + ((second - first) * fraction);
        };

        const auto left = interpolate(stem.channels.front());
        const auto right = stem.channels.size() == 1
            ? left
            : interpolate(stem.channels[1]);
        mix.left += left;
        mix.right += right;
    }

    return mix;
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
        playbackState_.finish();
        playheadSeconds_.store(0.0, std::memory_order_release);
        return;
    }

    const auto requestedGeneration = requestedGeneration_.load(std::memory_order_acquire);
    if (requestedGeneration != activeGeneration_) {
        activeSnapshot_ = playbackState_.snapshot();
        activeGeneration_ = requestedGeneration;
        nextEventIndex_ = 0;
        playbackSamplePosition_ = 0;
        clearVoices();
    }

    if (!playbackState_.isPlaying() || activeSnapshot_ == nullptr) {
        return;
    }

    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex) {
        const auto currentSeconds = static_cast<double>(playbackSamplePosition_) / std::max(1.0, sampleRate_);
        if (activeSnapshot_->midi.has_value()) {
            while (nextEventIndex_ < activeSnapshot_->midi->events.size()
                   && activeSnapshot_->midi->events[nextEventIndex_].timeSeconds <= currentSeconds + 1.0e-9) {
                const auto& event = activeSnapshot_->midi->events[nextEventIndex_];
                if (event.kind == MidiPlaybackEventKind::noteOff) {
                    releaseVoice(event.noteInstanceId);
                } else {
                    startVoice(event);
                }
                ++nextEventIndex_;
            }
        }

        const auto synthSample = renderVoices();
        const auto stemSample = renderAudioStems(currentSeconds);
        const auto masterGain = volume_.load(std::memory_order_relaxed);
        const auto leftSample = std::clamp(
            (synthSample + stemSample.left) * masterGain,
            -0.95f,
            0.95f);
        const auto rightSample = std::clamp(
            (synthSample + stemSample.right) * masterGain,
            -0.95f,
            0.95f);
        for (int channel = 0; channel < numOutputChannels; ++channel) {
            if (outputChannelData[channel] != nullptr) {
                outputChannelData[channel][sampleIndex] = channel == 0
                    ? leftSample
                    : rightSample;
            }
        }

        ++playbackSamplePosition_;
    }

    const auto currentSeconds = static_cast<double>(playbackSamplePosition_) / std::max(1.0, sampleRate_);
    playheadSeconds_.store(
        std::min(currentSeconds, activeSnapshot_->durationSeconds),
        std::memory_order_release);

    const auto midiFinished = !activeSnapshot_->midi.has_value()
        || nextEventIndex_ >= activeSnapshot_->midi->events.size();
    const auto reachedSelectionEnd = currentSeconds >= activeSnapshot_->durationSeconds;
    if (midiFinished && !hasActiveVoices() && reachedSelectionEnd) {
        playbackState_.finish();
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
    playbackState_.finish();
    deviceReady_.store(false, std::memory_order_release);
}

}  // namespace dawhermes::audio
