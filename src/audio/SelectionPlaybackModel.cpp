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

double sanitizedFallbackBpm(double bpm)
{
    return std::isfinite(bpm) && bpm > 0.0 ? bpm : kFallbackPlaybackBpm;
}

std::vector<core::MidiTempoEvent> singleTempoMap(double bpm)
{
    const auto sanitizedBpm = sanitizedFallbackBpm(bpm);
    const auto microseconds = static_cast<int>(std::clamp(
        std::llround(60000000.0 / sanitizedBpm),
        1LL,
        static_cast<long long>(std::numeric_limits<int>::max())));
    return core::sanitizeTempoMap({ core::MidiTempoEvent { 0.0, microseconds } });
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

std::vector<core::MidiTempoEvent> playbackTempoMapForSelection(
    const core::Track* midiTrack,
    const SelectionPlaybackOptions& options,
    PlaybackTempoSource& source)
{
    if (midiTrack != nullptr && hasExplicitMidiTempo(*midiTrack)) {
        source = PlaybackTempoSource::explicitMidi;
        return core::sanitizeTempoMap(midiTrack->midiSourceMetadata->tempoMap);
    }

    if (options.detectedWavBpm.has_value()
        && std::isfinite(options.detectedWavBpm.value())
        && options.detectedWavBpm.value() >= 60.0
        && options.detectedWavBpm.value() <= 200.0) {
        source = PlaybackTempoSource::detectedWav;
        return singleTempoMap(options.detectedWavBpm.value());
    }

    source = PlaybackTempoSource::fallback;
    return singleTempoMap(options.fallbackBpm);
}

std::optional<double> inspectAudioStemDuration(
    const core::Track& track,
    SkippedAudioTrack& skipped)
{
    skipped.sourceTrackId = track.id;
    skipped.sourceTrackName = track.name;
    skipped.sourceFileName = sourceFileName(track.audioSourcePath);

    if (track.audioSourcePath.empty()) {
        skipped.reason = "audio file is unavailable";
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
    if (reader == nullptr
        || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0
        || reader->numChannels < 1
        || reader->numChannels > 2) {
        skipped.reason = "file is not a readable mono or stereo WAV";
        return std::nullopt;
    }

    return static_cast<double>(reader->lengthInSamples) / reader->sampleRate;
}

std::optional<AudioStemPlaybackSnapshot> loadAudioStem(
    const core::Track& track,
    SkippedAudioTrack& skipped)
{
    skipped.sourceTrackId = track.id;
    skipped.sourceTrackName = track.name;
    skipped.sourceFileName = sourceFileName(track.audioSourcePath);

    if (track.audioSourcePath.empty()) {
        skipped.reason = "audio file is unavailable";
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

SelectionPlaybackSnapshotResult createSelectionPlaybackSnapshot(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    const SelectionPlaybackOptions& options)
{
    SelectionPlaybackSnapshotResult result;
    const auto* midiTrack = primarySelectedMidiTrack(project, selection);
    result.snapshot.playheadTempoMap = playbackTempoMapForSelection(
        midiTrack,
        options,
        result.snapshot.tempoSource);

    if (midiTrack != nullptr) {
        auto midiResult = createMidiPlaybackSnapshot(
            *midiTrack,
            result.snapshot.playheadTempoMap);
        if (midiResult.ok) {
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

    result.ok = result.snapshot.isPlayable();
    result.message = result.ok
        ? describeSelectionPlayback(result.snapshot)
        : (result.skippedAudioTracks.empty()
               ? "No playable MIDI or imported audio track is selected."
               : describeSkippedAudioTrack(result.skippedAudioTracks.front()));
    return result;
}

SelectionPlaybackSummary createSelectionPlaybackSummary(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    const SelectionPlaybackOptions& options)
{
    SelectionPlaybackSummary summary;
    const auto* midiTrack = primarySelectedMidiTrack(project, selection);
    summary.tempoMap = playbackTempoMapForSelection(
        midiTrack,
        options,
        summary.tempoSource);

    if (midiTrack != nullptr) {
        const auto midi = createMidiPlaybackSnapshot(*midiTrack, summary.tempoMap);
        if (midi.ok) {
            summary.midiTrackCount = 1;
            summary.durationSeconds = midi.snapshot.durationSeconds;
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
        const auto duration = inspectAudioStemDuration(*track, skipped);
        if (!duration.has_value()) {
            summary.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }

        ++summary.audioTrackCount;
        summary.durationSeconds = std::max(summary.durationSeconds, duration.value());
        if (!summary.firstReadableAudioPath.has_value()) {
            summary.firstReadableAudioPath = track->audioSourcePath;
        }
    }

    summary.playable = summary.midiTrackCount > 0 || summary.audioTrackCount > 0;
    return summary;
}

SelectionTransportCommandState selectionTransportCommandState(
    const core::ProjectModel& project,
    const core::SelectionState& selection,
    TransportMode mode,
    double playableDurationSeconds)
{
    SelectionTransportCommandState state;
    state.playEnabled = mode == TransportMode::paused
        || (mode == TransportMode::stopped
            && selectionHasPotentialPlayback(project, selection));
    state.pauseEnabled = mode == TransportMode::playing;
    state.stopEnabled = mode != TransportMode::stopped;
    state.rewindEnabled = playableDurationSeconds > 0.0;
    state.fastForwardEnabled = playableDurationSeconds > 0.0;
    state.panicEnabled = true;
    return state;
}

double selectionPlayheadBeat(
    double transportSeconds,
    const SelectionPlaybackSnapshot& snapshot)
{
    return midiSecondsToBeat(transportSeconds, snapshot.playheadTempoMap);
}

double playbackBpmAtBeat(
    double beat,
    const std::vector<core::MidiTempoEvent>& tempoMap)
{
    const auto map = core::sanitizeTempoMap(tempoMap);
    const auto targetBeat = std::isfinite(beat) ? std::max(0.0, beat) : 0.0;
    auto active = map.front();
    for (const auto& event : map) {
        if (event.beatPosition > targetBeat + 1.0e-9) {
            break;
        }
        active = event;
    }

    return 60000000.0
        / static_cast<double>(std::max(1, active.microsecondsPerQuarterNote));
}

double selectionPlaybackBpm(
    double transportSeconds,
    const SelectionPlaybackSnapshot& snapshot)
{
    const auto beat = selectionPlayheadBeat(transportSeconds, snapshot);
    return playbackBpmAtBeat(beat, snapshot.playheadTempoMap);
}

bool hasExplicitMidiTempo(const core::Track& track) noexcept
{
    return track.type == core::TrackType::midi
        && track.midiSourceMetadata.has_value()
        && track.midiSourceMetadata->containsExplicitTempoEvents.value_or(
            !track.midiSourceMetadata->tempoMap.empty());
}

MidiResumeState createMidiResumeState(
    const MidiPlaybackSnapshot& snapshot,
    double transportSeconds)
{
    MidiResumeState state;
    const auto target = clampTransportSeconds(
        transportSeconds,
        snapshot.durationSeconds);

    for (const auto& event : snapshot.events) {
        if (event.timeSeconds > target + 1.0e-9) {
            break;
        }

        ++state.nextEventIndex;
        const auto existing = std::find_if(
            state.activeNoteOns.begin(),
            state.activeNoteOns.end(),
            [&event](const auto& active) {
                return active.noteInstanceId == event.noteInstanceId;
            });

        if (event.kind == MidiPlaybackEventKind::noteOn) {
            if (existing == state.activeNoteOns.end()) {
                state.activeNoteOns.push_back(event);
            } else {
                *existing = event;
            }
        } else if (existing != state.activeNoteOns.end()) {
            state.activeNoteOns.erase(existing);
        }
    }
    return state;
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
