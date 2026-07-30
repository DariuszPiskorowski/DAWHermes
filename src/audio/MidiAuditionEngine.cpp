#include "audio/MidiAuditionEngine.h"

#include <algorithm>
#include <cmath>

#include "plugins/Vst3InstrumentHost.h"

namespace dawhermes::audio {

namespace {

constexpr double kTwoPi = 6.28318530717958647692;
constexpr double kReleaseSeconds = 0.02;
constexpr float kSynthGain = 0.18f;
constexpr float kTestToneGain = 0.08f;
constexpr double kTestToneFrequency = 440.0;

double frequencyForMidiPitch(int pitch)
{
    return 440.0 * std::pow(2.0, (static_cast<double>(std::clamp(pitch, 0, 127)) - 69.0) / 12.0);
}

}  // namespace

MidiAuditionEngine::MidiAuditionEngine(
    plugins::Vst3InstrumentHost* instrumentHost)
    : instrumentHost_(instrumentHost)
{
}

MidiAuditionEngine::~MidiAuditionEngine()
{
    panic();
}

bool MidiAuditionEngine::startPlayback(MidiPlaybackSnapshot snapshot, std::string& error)
{
    SelectionPlaybackSnapshot selectionSnapshot;
    selectionSnapshot.playheadTempoMap = snapshot.tempoMap;
    selectionSnapshot.durationSeconds = snapshot.durationSeconds;
    selectionSnapshot.midiTrackIds.push_back(snapshot.sourceTrackId);
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

    if (!deviceReady_.load(std::memory_order_acquire)) {
        error = "No configured audio output device is available.";
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
    const auto existingLoop =
        requestedLoop_.load(std::memory_order_acquire);
    setTimelineLoop(
        existingLoop == nullptr
            ? std::optional<core::TimelineLoopRange> {}
            : std::optional<core::TimelineLoopRange> {
                  existingLoop->beats
              },
        loopEnabled_.load(std::memory_order_acquire));
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

    if (!deviceReady_.load(std::memory_order_acquire)) {
        error = "No configured audio output device is available.";
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
    testToneSamplesRemaining_.store(0, std::memory_order_release);
    transportState_.panic();
}

void MidiAuditionEngine::seekTo(double targetSeconds)
{
    const auto wasPlaying = transportState_.isPlaying();
    transportState_.seek(targetSeconds);
    if (!transportState_.hasPreparedPlayback()) {
        return;
    }

    const auto loop =
        requestedLoop_.load(std::memory_order_acquire);
    const auto looping = loop != nullptr && loop->enabled;
    if (wasPlaying
        && !looping
        && transportState_.currentSeconds() >= transportState_.totalSeconds() - 1.0e-9) {
        transportState_.complete();
    }
    publishCursor(transportState_.currentSeconds());
}

void MidiAuditionEngine::setPreviewDuration(
    double durationSeconds,
    std::uint64_t playableSelectionGeneration)
{
    synchronizeStoppedTransportPreview(
        transportState_,
        durationSeconds,
        playableSelectionGeneration);
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

bool MidiAuditionEngine::isPlayheadVisible() const noexcept
{
    return transportState_.isPlayheadVisible();
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
    retainedRoutingStates_.erase(
        std::remove_if(
            retainedRoutingStates_.begin(),
            retainedRoutingStates_.end(),
            [](const auto& state) { return state.use_count() == 1; }),
        retainedRoutingStates_.end());
    retainedLoopStates_.erase(
        std::remove_if(
            retainedLoopStates_.begin(),
            retainedLoopStates_.end(),
            [](const auto& state) { return state.use_count() == 1; }),
        retainedLoopStates_.end());
}

void MidiAuditionEngine::setProjectRoutingState(
    core::ProjectRoutingState routing)
{
    auto immutable =
        std::make_shared<const core::ProjectRoutingState>(
            std::move(routing));
    retainedRoutingStates_.push_back(immutable);
    requestedRouting_.store(immutable, std::memory_order_release);
    requestedRoutingGeneration_.fetch_add(
        1,
        std::memory_order_acq_rel);
    if (transportState_.hasPreparedPlayback()) {
        publishCursor(transportState_.currentSeconds());
    }
}

std::shared_ptr<const MidiAuditionEngine::PreparedLoop>
MidiAuditionEngine::buildLoop(
    std::optional<core::TimelineLoopRange> range,
    bool enabled) const
{
    auto prepared = std::make_shared<PreparedLoop>();
    prepared->enabled =
        enabled && range.has_value() && range->isValid();
    if (!prepared->enabled) {
        return prepared;
    }

    prepared->beats = range.value();
    const auto snapshot = transportState_.snapshot();
    if (snapshot == nullptr) {
        prepared->enabled = false;
        return prepared;
    }
    prepared->startSeconds = midiBeatToSeconds(
        range->startBeat,
        snapshot->playheadTempoMap);
    prepared->endSeconds = midiBeatToSeconds(
        range->endBeat,
        snapshot->playheadTempoMap);
    prepared->endSeconds = std::min(
        prepared->endSeconds,
        snapshot->durationSeconds);
    if (prepared->endSeconds
        <= prepared->startSeconds + 1.0e-9) {
        prepared->enabled = false;
        return prepared;
    }

    if (snapshot->midi.has_value()) {
        const auto resume = createMidiResumeState(
            snapshot->midi.value(),
            prepared->startSeconds);
        prepared->nextEventIndex = resume.nextEventIndex;
        prepared->activeNoteCount = std::min(
            resume.activeNoteOns.size(),
            prepared->activeNoteOns.size());
        for (std::size_t index = 0;
             index < prepared->activeNoteCount;
             ++index) {
            prepared->activeNoteOns[index] =
                resume.activeNoteOns[index];
        }
    }
    return prepared;
}

void MidiAuditionEngine::setTimelineLoop(
    std::optional<core::TimelineLoopRange> range,
    bool enabled)
{
    const auto prepared = buildLoop(range, enabled);
    retainedLoopStates_.push_back(prepared);
    loopEnabled_.store(
        enabled && range.has_value() && range->isValid(),
        std::memory_order_release);
    requestedLoop_.store(prepared, std::memory_order_release);
    requestedLoopGeneration_.fetch_add(
        1,
        std::memory_order_acq_rel);
}

bool MidiAuditionEngine::isTimelineLoopEnabled() const noexcept
{
    return loopEnabled_.load(std::memory_order_acquire);
}

bool MidiAuditionEngine::startTestTone(double durationSeconds) noexcept
{
    if (!deviceReady_.load(std::memory_order_acquire)
        || transportState_.mode() != TransportMode::stopped
        || !std::isfinite(durationSeconds)
        || durationSeconds <= 0.0) {
        return false;
    }

    const auto samples = static_cast<std::int64_t>(std::llround(
        std::clamp(durationSeconds, 0.05, 2.0)
        * reportedSampleRate_.load(std::memory_order_acquire)));
    testToneSamplesRemaining_.store(
        std::max<std::int64_t>(1, samples),
        std::memory_order_release);
    return true;
}

bool MidiAuditionEngine::isTestToneActive() const noexcept
{
    return testToneSamplesRemaining_.load(std::memory_order_acquire) > 0;
}

void MidiAuditionEngine::prepareForOfflineTesting(double sampleRate)
{
    sampleRate_ = std::max(1.0, sampleRate);
    reportedSampleRate_.store(sampleRate_, std::memory_order_release);
    releaseMultiplier_ = static_cast<float>(
        std::exp(
            std::log(0.001)
            / (kReleaseSeconds * sampleRate_)));
    constexpr int offlineBlockSize = 512;
    for (auto& delay : dryDelay_) {
        delay.assign(
            static_cast<std::size_t>(
                plugins::kMaximumHostedInstrumentLatencySamples
                + offlineBlockSize + 1),
            0.0f);
    }
    dryDelayWrite_ = 0;
    if (instrumentHost_ != nullptr) {
        instrumentHost_->prepareDevice(
            sampleRate_,
            offlineBlockSize);
    }
    deviceReady_.store(true, std::memory_order_release);
}

void MidiAuditionEngine::renderOfflineForTesting(
    float* left,
    float* right,
    int numSamples)
{
    float* outputs[] { left, right };
    audioDeviceIOCallbackWithContext(
        nullptr,
        0,
        outputs,
        2,
        numSamples,
        {});
}

std::size_t
MidiAuditionEngine::activeVoiceCountForTesting() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        voices_.begin(),
        voices_.end(),
        [](const auto& voice) { return voice.active; }));
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

bool MidiAuditionEngine::isTrackAudible(
    std::uint64_t trackId) const noexcept
{
    return activeRouting_ == nullptr
        || activeRouting_->isAudible(trackId);
}

void MidiAuditionEngine::reconstructVoices(
    const MidiPlaybackEvent* events,
    std::size_t count,
    int sampleOffset) noexcept
{
    for (std::size_t index = 0; index < count; ++index) {
        if (isTrackAudible(events[index].sourceTrackId)) {
            handleMidiEvent(events[index], sampleOffset);
        }
    }
}

void MidiAuditionEngine::handleMidiEvent(
    const MidiPlaybackEvent& event,
    int sampleOffset) noexcept
{
    const auto noteOn =
        event.kind == MidiPlaybackEventKind::noteOn;
    if (instrumentHost_ != nullptr
        && instrumentHost_->addMidiEventFromAudioThread(
            event.sourceTrackId,
            noteOn,
            event.channel,
            event.pitch,
            event.amplitude,
            sampleOffset)) {
        return;
    }
    if (noteOn) {
        startVoice(event);
    } else {
        releaseVoice(event.noteInstanceId);
    }
}

MidiAuditionEngine::StereoSample
MidiAuditionEngine::delayDryMix(
    StereoSample input,
    int delaySamples) noexcept
{
    if (dryDelay_[0].empty()) {
        return input;
    }
    const auto safeDelay = static_cast<std::size_t>(
        std::clamp(
            delaySamples,
            0,
            plugins::kMaximumHostedInstrumentLatencySamples));
    const auto size = dryDelay_[0].size();
    dryDelay_[0][dryDelayWrite_] = input.left;
    dryDelay_[1][dryDelayWrite_] = input.right;
    const auto read =
        (dryDelayWrite_ + size - safeDelay) % size;
    const StereoSample result {
        dryDelay_[0][read],
        dryDelay_[1][read]
    };
    dryDelayWrite_ = (dryDelayWrite_ + 1U) % size;
    return result;
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
    voice->sourceTrackId = event.sourceTrackId;
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
        if (!isTrackAudible(stem.sourceTrackId)) {
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

    plugins::PluginTransportPosition pluginPosition;
    auto pluginPositionSamples = playbackSamplePosition_;
    const auto pendingGeneration =
        requestedGeneration_.load(std::memory_order_acquire);
    if (pendingGeneration != activeGeneration_) {
        const auto pendingCursor =
            requestedCursor_.load(std::memory_order_acquire);
        if (pendingCursor != nullptr
            && pendingCursor->snapshot != nullptr) {
            pluginPositionSamples =
                static_cast<std::uint64_t>(std::llround(
                    pendingCursor->startSeconds
                    * std::max(1.0, sampleRate_)));
        }
    }
    pluginPosition.samplePosition =
        static_cast<std::int64_t>(pluginPositionSamples);
    pluginPosition.seconds =
        static_cast<double>(pluginPositionSamples)
        / std::max(1.0, sampleRate_);
    pluginPosition.playing = transportState_.isPlaying();
    if (const auto snapshot = transportState_.snapshot();
        snapshot != nullptr) {
        pluginPosition.ppqPosition = midiSecondsToBeat(
            pluginPosition.seconds,
            snapshot->playheadTempoMap);
        pluginPosition.bpm = selectionPlaybackBpm(
            pluginPosition.seconds,
            *snapshot);
        for (const auto& timeSignature :
             snapshot->playheadTimeSignatureMap) {
            if (timeSignature.beatPosition
                > pluginPosition.ppqPosition + 1.0e-9) {
                break;
            }
            pluginPosition.timeSignatureNumerator =
                std::max(1, timeSignature.numerator);
            pluginPosition.timeSignatureDenominator =
                std::max(1, timeSignature.denominator);
        }
    }
    const auto pluginLoop =
        requestedLoop_.load(std::memory_order_acquire);
    if (pluginLoop != nullptr && pluginLoop->enabled) {
        pluginPosition.looping = true;
        pluginPosition.loopStartPpq =
            pluginLoop->beats.startBeat;
        pluginPosition.loopEndPpq =
            pluginLoop->beats.endBeat;
    }
    if (instrumentHost_ != nullptr) {
        instrumentHost_->beginAudioBlock(
            numSamples,
            pluginPosition);
    }

    if (stopRequested_.exchange(false, std::memory_order_acq_rel)
        || panicRequested_.exchange(false, std::memory_order_acq_rel)) {
        clearVoices();
        if (instrumentHost_ != nullptr) {
            instrumentHost_->resetAllFromAudioThread();
            instrumentHost_->processAudioBlock(
                outputChannelData,
                numOutputChannels,
                numSamples,
                0.0f,
                false);
        }
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
        if (instrumentHost_ != nullptr) {
            instrumentHost_->resetAllFromAudioThread();
            instrumentHost_->processAudioBlock(
                outputChannelData,
                numOutputChannels,
                numSamples,
                0.0f,
                false);
        }
        return;
    }

    const auto requestedRoutingGeneration =
        requestedRoutingGeneration_.load(std::memory_order_acquire);
    if (requestedRoutingGeneration != activeRoutingGeneration_) {
        activeRouting_ =
            requestedRouting_.load(std::memory_order_acquire);
        activeRoutingGeneration_ = requestedRoutingGeneration;
    }

    const auto requestedLoopGeneration =
        requestedLoopGeneration_.load(std::memory_order_acquire);
    if (requestedLoopGeneration != activeLoopGeneration_) {
        activeLoop_ = requestedLoop_.load(std::memory_order_acquire);
        activeLoopGeneration_ = requestedLoopGeneration;
    }

    const auto requestedGeneration = requestedGeneration_.load(std::memory_order_acquire);
    if (requestedGeneration != activeGeneration_) {
        const auto cursor = requestedCursor_.load(std::memory_order_acquire);
        activeGeneration_ = requestedGeneration;
        clearVoices();
        if (instrumentHost_ != nullptr) {
            instrumentHost_->resetAllFromAudioThread();
        }
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
                reconstructVoices(
                    cursor->activeNoteOns.data(),
                    cursor->activeNoteCount,
                    0);
            }
        }
    }

    const auto renderProject =
        transportState_.isPlaying() && activeSnapshot_ != nullptr;
    if (!renderProject
        && testToneSamplesRemaining_.load(std::memory_order_acquire) <= 0) {
        return;
    }

    const auto dryLatencySamples =
        instrumentHost_ == nullptr
        ? 0
        : instrumentHost_->maximumLatencySamples();
    for (int sampleIndex = 0; sampleIndex < numSamples; ++sampleIndex) {
        auto currentSeconds =
            static_cast<double>(playbackSamplePosition_)
            / std::max(1.0, sampleRate_);
        if (renderProject
            && activeLoop_ != nullptr
            && activeLoop_->enabled
            && currentSeconds
                >= activeLoop_->endSeconds - 1.0e-9) {
            clearVoices();
            if (instrumentHost_ != nullptr) {
                instrumentHost_->resetAllFromAudioThread(
                    sampleIndex);
            }
            playbackSamplePosition_ =
                static_cast<std::uint64_t>(std::llround(
                    activeLoop_->startSeconds
                    * std::max(1.0, sampleRate_)));
            nextEventIndex_ = activeLoop_->nextEventIndex;
            reconstructVoices(
                activeLoop_->activeNoteOns.data(),
                activeLoop_->activeNoteCount,
                sampleIndex);
            currentSeconds = activeLoop_->startSeconds;
        }
        if (renderProject && activeSnapshot_->midi.has_value()) {
            while (nextEventIndex_ < activeSnapshot_->midi->events.size()
                   && activeSnapshot_->midi->events[nextEventIndex_].timeSeconds <= currentSeconds + 1.0e-9) {
                const auto& event = activeSnapshot_->midi->events[nextEventIndex_];
                if (event.kind == MidiPlaybackEventKind::noteOff
                    || isTrackAudible(event.sourceTrackId)) {
                    handleMidiEvent(event, sampleIndex);
                }
                ++nextEventIndex_;
            }
        }

        const auto synthSample = renderProject ? renderVoices() : 0.0f;
        const auto stemSample = renderProject
            ? renderAudioStems(currentSeconds)
            : StereoSample {};
        float testToneSample = 0.0f;
        auto toneSamples = testToneSamplesRemaining_.load(
            std::memory_order_relaxed);
        if (toneSamples > 0) {
            testToneSample = static_cast<float>(
                std::sin(testTonePhase_)) * kTestToneGain;
            testTonePhase_ += kTwoPi
                * kTestToneFrequency
                / std::max(1.0, sampleRate_);
            if (testTonePhase_ >= kTwoPi) {
                testTonePhase_ -= kTwoPi;
            }
            testToneSamplesRemaining_.store(
                toneSamples - 1,
                std::memory_order_relaxed);
        }
        const auto dry = delayDryMix(
            {
                synthSample + stemSample.left,
                synthSample + stemSample.right
            },
            dryLatencySamples);
        const auto masterGain =
            volume_.load(std::memory_order_relaxed);
        const auto leftSample = std::clamp(
            (dry.left * masterGain)
                + testToneSample,
            -0.95f,
            0.95f);
        const auto rightSample = std::clamp(
            (dry.right * masterGain)
                + testToneSample,
            -0.95f,
            0.95f);
        for (int channel = 0; channel < numOutputChannels; ++channel) {
            if (outputChannelData[channel] != nullptr) {
                outputChannelData[channel][sampleIndex] = channel == 0
                    ? leftSample
                    : rightSample;
            }
        }

        if (renderProject) {
            ++playbackSamplePosition_;
        }
    }

    if (renderProject && instrumentHost_ != nullptr) {
        instrumentHost_->processAudioBlock(
            outputChannelData,
            numOutputChannels,
            numSamples,
            volume_.load(std::memory_order_relaxed));
        for (int channel = 0;
             channel < numOutputChannels;
             ++channel) {
            if (outputChannelData[channel] == nullptr) {
                continue;
            }
            for (int sample = 0; sample < numSamples; ++sample) {
                outputChannelData[channel][sample] =
                    std::clamp(
                        outputChannelData[channel][sample],
                        -0.95f,
                        0.95f);
            }
        }
    }

    if (!renderProject) {
        return;
    }

    const auto currentSeconds = static_cast<double>(playbackSamplePosition_) / std::max(1.0, sampleRate_);
    if (requestedGeneration_.load(std::memory_order_acquire) != activeGeneration_) {
        return;
    }
    transportState_.updatePositionFromAudio(
        std::min(currentSeconds, activeSnapshot_->durationSeconds));

    const auto midiFinished = !activeSnapshot_->midi.has_value()
        || nextEventIndex_ >= activeSnapshot_->midi->events.size();
    const auto reachedSelectionEnd =
        currentSeconds >= activeSnapshot_->durationSeconds;
    const auto looping = activeLoop_ != nullptr
        && activeLoop_->enabled;
    if (!looping
        && midiFinished
        && !hasActiveVoices()
        && reachedSelectionEnd) {
        transportState_.complete();
    }
}

void MidiAuditionEngine::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    sampleRate_ = device == nullptr ? 44100.0 : std::max(1.0, device->getCurrentSampleRate());
    reportedSampleRate_.store(sampleRate_, std::memory_order_release);
    releaseMultiplier_ = static_cast<float>(
        std::exp(std::log(0.001) / (kReleaseSeconds * sampleRate_)));
    clearVoices();
    const auto maximumBlockSize = device == nullptr
        ? 512
        : std::max(1, device->getCurrentBufferSizeSamples());
    for (auto& delay : dryDelay_) {
        delay.assign(
            static_cast<std::size_t>(
                plugins::kMaximumHostedInstrumentLatencySamples
                + maximumBlockSize + 1),
            0.0f);
    }
    dryDelayWrite_ = 0;
    if (instrumentHost_ != nullptr) {
        instrumentHost_->prepareDevice(
            sampleRate_,
            maximumBlockSize);
    }
    testTonePhase_ = 0.0;
    deviceReady_.store(device != nullptr, std::memory_order_release);
}

void MidiAuditionEngine::audioDeviceStopped()
{
    clearVoices();
    testToneSamplesRemaining_.store(0, std::memory_order_release);
    transportState_.stop();
    deviceReady_.store(false, std::memory_order_release);
}

}  // namespace dawhermes::audio
