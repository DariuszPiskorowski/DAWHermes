#include "audio/SelectionPlaybackModel.h"

#include <algorithm>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/MidiTimeMap.h"

namespace dawhermes::audio {

namespace {

std::string sourceFileName(const std::string& sourcePath)
{
    if (sourcePath.empty()) {
        return {};
    }

    return std::filesystem::path(sourcePath).filename().string();
}

std::string countLabel(std::size_t count, const char* singular, const char* plural)
{
    return std::to_string(count) + " " + (count == 1 ? singular : plural);
}

const core::Track* primarySelectedMidiTrack(
    const core::ProjectModel& project,
    const core::SelectionState& selection)
{
    for (const auto selectedId : selection.selectedTrackIds()) {
        const auto* track = project.findTrackById(selectedId);
        if (track != nullptr
            && track->type == core::TrackType::midi
            && !track->midiNotes.empty()) {
            return track;
        }
    }

    return nullptr;
}

std::optional<AudioStemPlaybackSnapshot> loadAudioStem(
    const core::Track& track,
    SkippedAudioTrack& skipped)
{
    skipped.sourceTrackId = track.id;
    skipped.sourceTrackName = track.name;
    skipped.sourceFileName = sourceFileName(track.audioSourcePath);

    if (track.audioSourcePath.empty()) {
        skipped.reason = "no WAV source is assigned";
        return std::nullopt;
    }

    const juce::File sourceFile(track.audioSourcePath);
    if (!sourceFile.existsAsFile()) {
        skipped.reason = "file is missing";
        return std::nullopt;
    }

    juce::WavAudioFormat wavFormat;
    auto inputStream = sourceFile.createInputStream();
    if (inputStream == nullptr) {
        skipped.reason = "file could not be opened";
        return std::nullopt;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(inputStream.release(), true));
    if (reader == nullptr) {
        skipped.reason = "file is not a readable WAV";
        return std::nullopt;
    }

    if (reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0
        || reader->numChannels < 1
        || reader->numChannels > 2
        || reader->lengthInSamples > static_cast<juce::int64>(std::numeric_limits<int>::max())) {
        skipped.reason = "WAV must contain mono or stereo audio";
        return std::nullopt;
    }

    const auto frameCount = static_cast<int>(reader->lengthInSamples);
    const auto channelCount = static_cast<int>(reader->numChannels);
    juce::AudioBuffer<float> decoded(channelCount, frameCount);
    if (!reader->read(&decoded, 0, frameCount, 0, true, channelCount > 1)) {
        skipped.reason = "WAV data could not be decoded";
        return std::nullopt;
    }

    AudioStemPlaybackSnapshot snapshot;
    snapshot.sourceTrackId = track.id;
    snapshot.sourceTrackName = track.name;
    snapshot.sourcePath = track.audioSourcePath;
    snapshot.sourceSampleRate = reader->sampleRate;
    snapshot.frameCount = static_cast<std::uint64_t>(frameCount);
    snapshot.channels.resize(static_cast<std::size_t>(channelCount));

    for (int channel = 0; channel < channelCount; ++channel) {
        auto& destination = snapshot.channels[static_cast<std::size_t>(channel)];
        destination.assign(
            decoded.getReadPointer(channel),
            decoded.getReadPointer(channel) + frameCount);
    }

    if (!snapshot.isPlayable()) {
        skipped.reason = "decoded WAV data is empty";
        return std::nullopt;
    }

    return snapshot;
}

bool selectionHasPotentialPlayback(
    const core::ProjectModel& project,
    const core::SelectionState& selection)
{
    if (primarySelectedMidiTrack(project, selection) != nullptr) {
        return true;
    }

    return std::any_of(
        selection.selectedTrackIds().begin(),
        selection.selectedTrackIds().end(),
        [&project](std::uint64_t selectedId) {
            const auto* track = project.findTrackById(selectedId);
            return track != nullptr
                && track->type == core::TrackType::audio
                && !track->audioSourcePath.empty();
        });
}

}  // namespace

double AudioStemPlaybackSnapshot::durationSeconds() const noexcept
{
    if (sourceSampleRate <= 0.0) {
        return 0.0;
    }

    return static_cast<double>(frameCount) / sourceSampleRate;
}

bool AudioStemPlaybackSnapshot::isPlayable() const noexcept
{
    if (sourceSampleRate <= 0.0 || frameCount == 0 || channels.empty() || channels.size() > 2) {
        return false;
    }

    return std::all_of(channels.begin(), channels.end(), [this](const auto& channel) {
        return channel.size() == frameCount;
    });
}

bool SelectionPlaybackSnapshot::isPlayable() const noexcept
{
    const auto hasMidi = midi.has_value() && !midi->events.empty();
    const auto hasAudio = std::any_of(audioStems.begin(), audioStems.end(), [](const auto& stem) {
        return stem.isPlayable();
    });
    return hasMidi || hasAudio;
}

std::size_t SelectionPlaybackSnapshot::midiTrackCount() const noexcept
{
    return midi.has_value() && !midi->events.empty() ? 1U : 0U;
}

std::size_t SelectionPlaybackSnapshot::audioTrackCount() const noexcept
{
    return static_cast<std::size_t>(std::count_if(
        audioStems.begin(),
        audioStems.end(),
        [](const auto& stem) { return stem.isPlayable(); }));
}

void SelectionPlaybackState::start(
    std::shared_ptr<const SelectionPlaybackSnapshot> snapshot) noexcept
{
    const auto playable = snapshot != nullptr && snapshot->isPlayable();
    snapshot_.store(playable ? std::move(snapshot) : nullptr, std::memory_order_release);
    playing_.store(playable, std::memory_order_release);
}

void SelectionPlaybackState::stop() noexcept
{
    snapshot_.store(nullptr, std::memory_order_release);
    playing_.store(false, std::memory_order_release);
}

void SelectionPlaybackState::panic() noexcept
{
    stop();
}

void SelectionPlaybackState::finish() noexcept
{
    playing_.store(false, std::memory_order_release);
}

bool SelectionPlaybackState::isPlaying() const noexcept
{
    return playing_.load(std::memory_order_acquire);
}

bool SelectionPlaybackState::hasPreparedPlayback() const noexcept
{
    return snapshot_.load(std::memory_order_acquire) != nullptr;
}

std::shared_ptr<const SelectionPlaybackSnapshot> SelectionPlaybackState::snapshot() const noexcept
{
    return snapshot_.load(std::memory_order_acquire);
}

SelectionPlaybackSnapshotResult createSelectionPlaybackSnapshot(
    const core::ProjectModel& project,
    const core::SelectionState& selection)
{
    SelectionPlaybackSnapshotResult result;

    if (const auto* midiTrack = primarySelectedMidiTrack(project, selection);
        midiTrack != nullptr) {
        auto midiResult = createMidiPlaybackSnapshot(*midiTrack);
        if (midiResult.ok) {
            result.snapshot.playheadTempoMap = midiResult.snapshot.tempoMap;
            result.snapshot.durationSeconds = midiResult.snapshot.durationSeconds;
            result.snapshot.midi = std::move(midiResult.snapshot);
        }
    }

    for (const auto selectedId : selection.selectedTrackIds()) {
        const auto* track = project.findTrackById(selectedId);
        if (track == nullptr
            || track->type != core::TrackType::audio
            || track->audioSourcePath.empty()) {
            continue;
        }

        SkippedAudioTrack skipped;
        auto stem = loadAudioStem(*track, skipped);
        if (!stem.has_value()) {
            result.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }

        result.snapshot.durationSeconds = std::max(
            result.snapshot.durationSeconds,
            stem->durationSeconds());
        result.snapshot.audioStems.push_back(std::move(stem.value()));
    }

    if (result.snapshot.midi.has_value()) {
        result.snapshot.playheadTempoMap = result.snapshot.midi->tempoMap;
    } else {
        result.snapshot.playheadTempoMap = core::resolveMidiTimelineInfo(
            {},
            project.tracks()).tempoMap;
    }

    result.ok = result.snapshot.isPlayable();
    result.message = result.ok
        ? describeSelectionPlayback(result.snapshot)
        : (result.skippedAudioTracks.empty()
               ? "No playable MIDI or assigned audio track is selected."
               : describeSkippedAudioTrack(result.skippedAudioTracks.front()));
    return result;
}

SelectionTransportCommandState selectionTransportCommandState(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    bool isPlaying)
{
    SelectionTransportCommandState state;
    state.playEnabled = !isPlaying && selectionHasPotentialPlayback(project, selection);
    state.stopEnabled = isPlaying;
    state.panicEnabled = true;
    return state;
}

double selectionPlayheadBeat(
    double transportSeconds,
    const SelectionPlaybackSnapshot& snapshot)
{
    return midiSecondsToBeat(transportSeconds, snapshot.playheadTempoMap);
}

double audioSourceFramePosition(
    double transportSeconds,
    double sourceSampleRate) noexcept
{
    const auto seconds = std::isfinite(transportSeconds)
        ? std::max(0.0, transportSeconds)
        : 0.0;
    const auto sampleRate = std::isfinite(sourceSampleRate)
        ? std::max(0.0, sourceSampleRate)
        : 0.0;
    return seconds * sampleRate;
}

std::string describeSelectionPlayback(const SelectionPlaybackSnapshot& snapshot)
{
    std::string description = "Playing selection: ";
    if (snapshot.midiTrackCount() > 0) {
        description += countLabel(snapshot.midiTrackCount(), "MIDI track", "MIDI tracks");
    }
    if (snapshot.midiTrackCount() > 0 && snapshot.audioTrackCount() > 0) {
        description += ", ";
    }
    if (snapshot.audioTrackCount() > 0) {
        description += countLabel(snapshot.audioTrackCount(), "audio track", "audio tracks");
    }
    return description;
}

std::string describeSkippedAudioTrack(const SkippedAudioTrack& skipped)
{
    const auto displayName = skipped.sourceFileName.empty()
        ? skipped.sourceTrackName
        : skipped.sourceFileName;
    return "Skipped unreadable audio track: " + displayName;
}

}  // namespace dawhermes::audio
