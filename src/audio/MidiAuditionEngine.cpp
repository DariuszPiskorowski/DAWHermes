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
    return startPlayback(std::move(selectionSnapshot), 0.0, error);
}

bool MidiAuditionEngine::startPlayback(
    SelectionPlaybackSnapshot snapshot,
    std::string& error)
{
    return startPlayback(std::move(snapshot), 0.0, error);
}

bool MidiAuditionEngine::startPlayback(
    SelectionPlaybackSnapshot snapshot,
    double startSeconds,
    std::string& error)
{
    if (!snapshot.isPlayable()) {
        error = "The selection playback snapshot is empty.";
        return false;
    }

    if (!initializeDefaultAudioDevice(error)) {
        return false;
    }

    stopRequested_.store(false, std::memory_order_release);
    panicRequested_.store(false, std::memory_order_release);
    pauseRequested_.store(false, std::memory_order_release);
    auto immutableSnapshot = std::make_shared<const SelectionPlaybackSnapshot>(std::move(snapshot));
    retainedSnapshots_.push_back(immutableSnapshot);
    transportState_.prepare(
        std::move(immutableSnapshot),
        startSeconds);
    transportState_.play();
    publishCursor(transportState_.currentSeconds());
    return true;
}

bool MidiAuditionEngine::resume(std::string& error)
{
    if (!transportState_.isPaused() || !transportState_.hasPreparedPlayback()) {
        error = "Playback is not paused.";
        return false;
    }

    if (!initializeDefaultAudioDevice(error)) {
        return false;
    }

    stopRequested_.store(false, std::memory_order_release);
    panicRequested_.store(false, std::memory_order_release);
    pauseRequested_.store(false, std::memory_order_release);
    transportState_.play();
    publishCursor(transportState_.currentSeconds());
    return true;
}

void MidiAuditionEngine::pause()
{
    if (!transportState_.isPlaying()) {
        return;
    }

    transportState_.pause();
    pauseRequested_.store(true, std::memory_order_release);
}

void MidiAuditionEngine::stop()
{
    stopRequested_.store(true, std::memory_order_release);
    pauseRequested_.store(false, std::memory_order_release);
    transportState_.stop();
}

void MidiAuditionEngine::panic()
{
    panicRequested_.store(true, std::memory_order_release);
    pauseRequested_.store(false, std::memory_order_release);
    transportState_.panic();
}

void MidiAuditionEngine::seekTo(double targetSeconds)
{
    const auto wasPlaying = transportState_.isPlaying();
    transportState_.seek(targetSeconds);
    if (!transportState_.hasPreparedPlayback()) {
        return;
    }

    if (wasPlaying
        && transportState_.currentSeconds() >= transportState_.totalSeconds() - 1.0e-9) {
        transportState_.complete();
    }
    publishCursor(transportState_.currentSeconds());
}

void MidiAuditionEngine::setPreviewDuration(double durationSeconds)
{
    transportState_.setPreviewDuration(durationSeconds);
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
    return transportState_.isPlaying();
}

TransportMode MidiAuditionEngine::transportMode() const noexcept
{
    return transportState_.mode();
}

bool MidiAuditionEngine::isPaused() const noexcept
{
    return transportState_.isPaused();
}

bool MidiAuditionEngine::isAudioDeviceReady() const noexcept
{
    return deviceReady_.load(std::memory_order_acquire);
}

double MidiAuditionEngine::playheadSeconds() const noexcept
{
    return transportState_.currentSeconds();
}

double MidiAuditionEngine::totalDurationSeconds() const noexcept
{
    return transportState_.totalSeconds();
}

double MidiAuditionEngine::playheadBeat() const
{
    const auto snapshot = transportState_.snapshot();
    return snapshot == nullptr
        ? 0.0
        : selectionPlayheadBeat(playheadSeconds(), *snapshot);
}

bool MidiAuditionEngine::hasPreparedPlayback() const noexcept
{
    return transportState_.hasPreparedPlayback();
}

std::shared_ptr<const SelectionPlaybackSnapshot>
MidiAuditionEngine::playbackSnapshot() const noexcept
{
    return transportState_.snapshot();
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

std::shared_ptr<const MidiAuditionEngine::PlaybackCursor>
MidiAuditionEngine::buildCursor(double startSeconds) const
{
    auto cursor = std::make_shared<PlaybackCursor>();
    cursor->snapshot = transportState_.snapshot();
    if (cursor->snapshot == nullptr) {
        return cursor;
    }

    cursor->startSeconds = clampTransportSeconds(
        startSeconds,
        cursor->snapshot->durationSeconds);
    if (!cursor->snapshot->midi.has_value()) {
        return cursor;
    }

    const auto resume = createMidiResumeState(
        cursor->snapshot->midi.value(),
        cursor->startSeconds);
    cursor->nextEventIndex = resume.nextEventIndex;
    cursor->activeNoteCount = std::min(
        resume.activeNoteOns.size(),
        cursor->activeNoteOns.size());
    for (std::size_t index = 0; index < cursor->activeNoteCount; ++index) {
        cursor->activeNoteOns[index] = resume.activeNoteOns[index];
    }

    return cursor;
}

void MidiAuditionEngine::publishCursor(double startSeconds)
{
    requestedCursor_.store(buildCursor(startSeconds), std::memory_order_release);
    requestedGeneration_.fetch_add(1, std::memory_order_acq_rel);
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
        playbackSamplePosition_ = static_cast<std::uint64_t>(std::llround(
            transportState_.currentSeconds() * std::max(1.0, sampleRate_)));
        return;
    }

    if (pauseRequested_.exchange(false, std::memory_order_acq_rel)) {
        if (requestedGeneration_.load(std::memory_order_acquire)
            == activeGeneration_) {
            transportState_.seek(
                static_cast<double>(playbackSamplePosition_)
                / std::max(1.0, sampleRate_));
        }
        clearVoices();
        return;
    }

    const auto requestedGeneration = requestedGeneration_.load(std::memory_order_acquire);
    if (requestedGeneration != activeGeneration_) {
        const auto cursor = requestedCursor_.load(std::memory_order_acquire);
        activeGeneration_ = requestedGeneration;
        clearVoices();
        if (cursor == nullptr || cursor->snapshot == nullptr) {
            activeSnapshot_.reset();
            nextEventIndex_ = 0;
            playbackSamplePosition_ = 0;
        } else {
            activeSnapshot_ = cursor->snapshot;
            nextEventIndex_ = cursor->nextEventIndex;
            playbackSamplePosition_ = static_cast<std::uint64_t>(std::llround(
                cursor->startSeconds * std::max(1.0, sampleRate_)));
            if (transportState_.isPlaying()) {
                for (std::size_t index = 0; index < cursor->activeNoteCount; ++index) {
                    startVoice(cursor->activeNoteOns[index]);
                }
            }
        }
    }

    if (!transportState_.isPlaying() || activeSnapshot_ == nullptr) {
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
    if (requestedGeneration_.load(std::memory_order_acquire) != activeGeneration_) {
        return;
    }
    transportState_.updatePositionFromAudio(
        std::min(currentSeconds, activeSnapshot_->durationSeconds));

    const auto midiFinished = !activeSnapshot_->midi.has_value()
        || nextEventIndex_ >= activeSnapshot_->midi->events.size();
    const auto reachedSelectionEnd = currentSeconds >= activeSnapshot_->durationSeconds;
    if (midiFinished && !hasActiveVoices() && reachedSelectionEnd) {
        transportState_.complete();
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
    transportState_.pause();
    deviceReady_.store(false, std::memory_order_release);
}

}  // namespace dawhermes::audio
