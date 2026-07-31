#include "audio/SelectionPlaybackModel.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <filesystem>
#include <limits>
#include <memory>
#include <new>
#include <unordered_map>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/MidiTimeMap.h"
#include "core/Utf8Path.h"

namespace dawhermes::audio {

namespace {

std::string sourceFileName(const std::string& sourcePath)
{
    return core::filenameFromUtf8Path(sourcePath);
}

juce::File juceFileFromUtf8(const std::string& sourcePath)
{
    return juce::File(juce::String::fromUTF8(
        sourcePath.data(),
        static_cast<int>(sourcePath.size())));
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

struct AudioStemDecodePlan {
    const core::Track* track { nullptr };
    double sourceSampleRate { 0.0 };
    std::uint64_t frameCount { 0 };
    int channelCount { 0 };
    std::uint64_t requiredBytes { 0 };
};

std::optional<AudioStemDecodePlan> inspectAudioStem(
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

    const auto sourceFile = juceFileFromUtf8(track.audioSourcePath);
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

    const auto frameCount = static_cast<std::uint64_t>(reader->lengthInSamples);
    const auto channelCount = static_cast<int>(reader->numChannels);
    const auto requiredBytes = decodedWavBytes(frameCount, channelCount);
    if (!requiredBytes.has_value()) {
        skipped.reason = "selected audio size cannot be represented safely";
        return std::nullopt;
    }

    return AudioStemDecodePlan {
        &track,
        reader->sampleRate,
        frameCount,
        channelCount,
        requiredBytes.value(),
    };
}

std::optional<AudioStemPlaybackSnapshot> loadAudioStem(
    const AudioStemDecodePlan& plan,
    SkippedAudioTrack& skipped)
{
    const auto& track = *plan.track;
    skipped.sourceTrackId = track.id;
    skipped.sourceTrackName = track.name;
    skipped.sourceFileName = sourceFileName(track.audioSourcePath);

    if (track.audioSourcePath.empty()) {
        skipped.reason = "audio file is unavailable";
        return std::nullopt;
    }

    const auto sourceFile = juceFileFromUtf8(track.audioSourcePath);
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
        || reader->numChannels > 2) {
        skipped.reason = "WAV must contain mono or stereo audio";
        return std::nullopt;
    }

    const auto frameCount = static_cast<std::uint64_t>(reader->lengthInSamples);
    const auto channelCount = static_cast<int>(reader->numChannels);
    const auto requiredBytes = decodedWavBytes(frameCount, channelCount);
    if (!requiredBytes.has_value()
        || requiredBytes.value() != plan.requiredBytes
        || frameCount != plan.frameCount
        || channelCount != plan.channelCount
        || std::abs(reader->sampleRate - plan.sourceSampleRate) > 1.0e-9) {
        skipped.reason = "WAV changed while playback was being prepared";
        return std::nullopt;
    }

    AudioStemPlaybackSnapshot snapshot;
    snapshot.sourceTrackId = track.id;
    snapshot.sourceTrackName = track.name;
    snapshot.sourcePath = track.audioSourcePath;
    snapshot.sourceSampleRate = reader->sampleRate;
    snapshot.frameCount = frameCount;
    snapshot.channels.resize(static_cast<std::size_t>(channelCount));

    try {
        for (auto& channel : snapshot.channels) {
            channel.resize(static_cast<std::size_t>(frameCount));
        }
    } catch (const std::bad_alloc&) {
        skipped.reason = "audition memory could not be allocated";
        return std::nullopt;
    }

    std::uint64_t frameOffset = 0;
    while (frameOffset < frameCount) {
        const auto framesToRead = static_cast<int>(std::min<std::uint64_t>(
            frameCount - frameOffset,
            static_cast<std::uint64_t>(kWavDecodeBlockFrames)));
        std::array<float*, 2> channelPointers {};
        for (int channel = 0; channel < channelCount; ++channel) {
            channelPointers[static_cast<std::size_t>(channel)] =
                snapshot.channels[static_cast<std::size_t>(channel)].data()
                + static_cast<std::size_t>(frameOffset);
        }
        juce::AudioBuffer<float> destination(
            channelPointers.data(),
            channelCount,
            framesToRead);
        if (!reader->read(
                &destination,
                0,
                framesToRead,
                static_cast<juce::int64>(frameOffset),
                true,
                channelCount > 1)) {
            skipped.reason = "WAV data could not be decoded";
            return std::nullopt;
        }
        frameOffset += static_cast<std::uint64_t>(framesToRead);
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

bool projectHasPotentialPlayback(const core::ProjectModel& project)
{
    for (const auto& track : project.tracks()) {
        if (track.type == core::TrackType::midi
            && !track.midiNotes.empty()) {
            return true;
        }
        if (track.type == core::TrackType::audio
            && !track.audioSourcePath.empty()) {
            SkippedAudioTrack skipped;
            if (inspectAudioStem(track, skipped).has_value()) {
                return true;
            }
        }
    }
    return false;
}

std::vector<core::MidiTempoEvent> playbackTempoMapForProject(
    const core::ProjectModel& project,
    const SelectionPlaybackOptions& options,
    PlaybackTempoSource& source,
    bool& conflictingExplicitMidiTempo)
{
    std::optional<std::vector<core::MidiTempoEvent>> firstExplicit;
    for (const auto& track : project.tracks()) {
        if (track.type != core::TrackType::midi
            || track.midiNotes.empty()
            || !hasExplicitMidiTempo(track)) {
            continue;
        }

        const auto map =
            core::sanitizeTempoMap(track.midiSourceMetadata->tempoMap);
        if (!firstExplicit.has_value()) {
            firstExplicit = map;
        } else if (firstExplicit.value() != map) {
            conflictingExplicitMidiTempo = true;
        }
    }

    if (firstExplicit.has_value()) {
        source = PlaybackTempoSource::explicitMidi;
        return firstExplicit.value();
    }

    for (const auto& track : project.tracks()) {
        if (track.type != core::TrackType::audio
            || track.audioSourcePath.empty()) {
            continue;
        }
        SkippedAudioTrack skipped;
        if (!inspectAudioStem(track, skipped).has_value()) {
            continue;
        }
        const auto detected = options.detectedWavBpms.find(track.id);
        if (detected != options.detectedWavBpms.end()
            && std::isfinite(detected->second)
            && detected->second >= 60.0
            && detected->second <= 200.0) {
            source = PlaybackTempoSource::detectedWav;
            return singleTempoMap(detected->second);
        }
    }

    source = PlaybackTempoSource::fallback;
    return singleTempoMap(options.fallbackBpm);
}

void appendMidiTrack(
    const core::Track& track,
    const std::vector<core::MidiTempoEvent>& tempoMap,
    SelectionPlaybackSnapshot& destination,
    std::uint64_t& nextInstanceId)
{
    auto result = createMidiPlaybackSnapshot(track, tempoMap);
    if (!result.ok) {
        return;
    }

    if (!destination.midi.has_value()) {
        MidiPlaybackSnapshot aggregate;
        aggregate.tempoMap = tempoMap;
        aggregate.sourceTrackName = "Project MIDI";
        destination.midi = std::move(aggregate);
    }

    auto& aggregate = destination.midi.value();
    std::unordered_map<std::uint64_t, std::uint64_t>
        globalInstanceIds;
    for (auto event : result.snapshot.events) {
        const auto [instance, inserted] =
            globalInstanceIds.emplace(
                event.noteInstanceId,
                nextInstanceId);
        if (inserted) {
            ++nextInstanceId;
        }
        event.noteInstanceId = instance->second;
        aggregate.events.push_back(event);
    }
    aggregate.durationSeconds = std::max(
        aggregate.durationSeconds,
        result.snapshot.durationSeconds);
    destination.durationSeconds = std::max(
        destination.durationSeconds,
        result.snapshot.durationSeconds);
    destination.midiTrackIds.push_back(track.id);
}

void sortProjectMidiEvents(MidiPlaybackSnapshot& snapshot)
{
    constexpr double epsilon = 1.0e-9;
    std::stable_sort(
        snapshot.events.begin(),
        snapshot.events.end(),
        [](const auto& left, const auto& right) {
            if (std::abs(left.timeSeconds - right.timeSeconds) > epsilon) {
                return left.timeSeconds < right.timeSeconds;
            }
            if (left.kind != right.kind) {
                return left.kind == MidiPlaybackEventKind::noteOff;
            }
            return left.noteInstanceId < right.noteInstanceId;
        });
}

}  // namespace

std::optional<std::uint64_t> decodedWavBytes(
    std::uint64_t frameCount,
    int channelCount) noexcept
{
    if (frameCount == 0 || channelCount < 1 || channelCount > 2) {
        return std::nullopt;
    }

    const auto channels = static_cast<std::uint64_t>(channelCount);
    if (frameCount > std::numeric_limits<std::uint64_t>::max() / channels) {
        return std::nullopt;
    }
    const auto samples = frameCount * channels;
    if (samples > std::numeric_limits<std::uint64_t>::max() / sizeof(float)) {
        return std::nullopt;
    }
    const auto bytes = samples * sizeof(float);
    if (frameCount > static_cast<std::uint64_t>(
                         std::numeric_limits<std::size_t>::max())) {
        return std::nullopt;
    }
    return bytes;
}

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
    if (!midiTrackIds.empty()) {
        return midiTrackIds.size();
    }
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
    if (midiTrack != nullptr
        && midiTrack->midiSourceMetadata.has_value()) {
        result.snapshot.playheadTimeSignatureMap =
            midiTrack->midiSourceMetadata
                ->timeSignatureMap;
    }
    if (result.snapshot.playheadTimeSignatureMap.empty()) {
        result.snapshot.playheadTimeSignatureMap.push_back(
            core::MidiTimeSignatureEvent {});
    }

    if (midiTrack != nullptr) {
        auto midiResult = createMidiPlaybackSnapshot(
            *midiTrack,
            result.snapshot.playheadTempoMap);
        if (midiResult.ok) {
            result.snapshot.durationSeconds = midiResult.snapshot.durationSeconds;
            result.snapshot.midi = std::move(midiResult.snapshot);
            result.snapshot.midiTrackIds.push_back(midiTrack->id);
        }
    }

    std::vector<AudioStemDecodePlan> decodePlans;
    for (const auto selectedId : selection.selectedTrackIds()) {
        const auto* track = project.findTrackById(selectedId);
        if (track == nullptr
            || track->type != core::TrackType::audio
            || track->audioSourcePath.empty()) {
            continue;
        }

        SkippedAudioTrack skipped;
        auto plan = inspectAudioStem(*track, skipped);
        if (!plan.has_value()) {
            result.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }

        if (result.estimatedSelectedAudioBytes
            > std::numeric_limits<std::uint64_t>::max()
                - plan->requiredBytes) {
            result.selectedAudioByteEstimateOverflow = true;
        } else {
            result.estimatedSelectedAudioBytes += plan->requiredBytes;
        }
        decodePlans.push_back(plan.value());
    }

    for (const auto& plan : decodePlans) {
        SkippedAudioTrack skipped;
        const auto decodedAudioBudget = std::min(
            options.maximumDecodedAudioBytes,
            kMaximumDecodedWavBytes);
        if (plan.requiredBytes > decodedAudioBudget
            - result.decodedAudioBytes) {
            skipped.sourceTrackId = plan.track->id;
            skipped.sourceTrackName = plan.track->name;
            skipped.sourceFileName = sourceFileName(
                plan.track->audioSourcePath);
            skipped.reason =
                "selected audio exceeds the 512 MiB audition memory limit";
            result.skippedAudioTracks.insert(
                result.skippedAudioTracks.begin(),
                std::move(skipped));
            continue;
        }

        auto stem = loadAudioStem(plan, skipped);
        if (!stem.has_value()) {
            result.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }

        result.decodedAudioBytes += plan.requiredBytes;
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
            summary.identity.primaryMidiTrackId = midiTrack->id;
            summary.identity.midiTrackIds.push_back(midiTrack->id);
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
        const auto plan = inspectAudioStem(*track, skipped);
        if (!plan.has_value()) {
            summary.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }

        ++summary.audioTrackCount;
        summary.durationSeconds = std::max(
            summary.durationSeconds,
            static_cast<double>(plan->frameCount) / plan->sourceSampleRate);
        summary.identity.readableAudioTrackIds.push_back(track->id);
        if (!summary.firstReadableAudioPath.has_value()) {
            summary.firstReadableAudioPath = track->audioSourcePath;
        }
    }

    std::sort(
        summary.identity.readableAudioTrackIds.begin(),
        summary.identity.readableAudioTrackIds.end());
    summary.playable = summary.midiTrackCount > 0 || summary.audioTrackCount > 0;
    return summary;
}

SelectionPlaybackSnapshotResult createProjectPlaybackSnapshot(
    const core::ProjectModel& project,
    const SelectionPlaybackOptions& options)
{
    SelectionPlaybackSnapshotResult result;
    bool conflictingTempo = false;
    result.snapshot.playheadTempoMap = playbackTempoMapForProject(
        project,
        options,
        result.snapshot.tempoSource,
        conflictingTempo);
    for (const auto& track : project.tracks()) {
        if (track.type == core::TrackType::midi
            && track.midiSourceMetadata.has_value()
            && !track.midiSourceMetadata
                    ->timeSignatureMap.empty()) {
            result.snapshot.playheadTimeSignatureMap =
                track.midiSourceMetadata
                    ->timeSignatureMap;
            break;
        }
    }
    if (result.snapshot.playheadTimeSignatureMap.empty()) {
        result.snapshot.playheadTimeSignatureMap.push_back(
            core::MidiTimeSignatureEvent {});
    }
    for (const auto& track : project.tracks()) {
        result.snapshot.projectTrackRoutingIdentity.push_back({
            track.id,
            track.parentTrackId,
            track.type,
        });
    }

    std::uint64_t nextInstanceId = 1;
    for (const auto& track : project.tracks()) {
        if (track.type == core::TrackType::midi
            && !track.midiNotes.empty()) {
            appendMidiTrack(
                track,
                result.snapshot.playheadTempoMap,
                result.snapshot,
                nextInstanceId);
        }
    }
    if (result.snapshot.midi.has_value()) {
        sortProjectMidiEvents(result.snapshot.midi.value());
    }

    const auto decodedAudioBudget = std::min(
        options.maximumDecodedAudioBytes,
        kMaximumDecodedWavBytes);
    for (const auto& track : project.tracks()) {
        if (track.type != core::TrackType::audio
            || track.audioSourcePath.empty()) {
            continue;
        }

        SkippedAudioTrack skipped;
        const auto plan = inspectAudioStem(track, skipped);
        if (!plan.has_value()) {
            result.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }
        result.snapshot.durationSeconds = std::max(
            result.snapshot.durationSeconds,
            static_cast<double>(plan->frameCount)
                / plan->sourceSampleRate);

        if (result.estimatedSelectedAudioBytes
            > std::numeric_limits<std::uint64_t>::max()
                - plan->requiredBytes) {
            result.selectedAudioByteEstimateOverflow = true;
        } else {
            result.estimatedSelectedAudioBytes += plan->requiredBytes;
        }

        if (plan->requiredBytes > decodedAudioBudget
            - result.decodedAudioBytes) {
            skipped.sourceTrackId = track.id;
            skipped.sourceTrackName = track.name;
            skipped.sourceFileName = sourceFileName(track.audioSourcePath);
            skipped.reason =
                "project audio exceeds the 512 MiB playback memory limit";
            result.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }

        auto stem = loadAudioStem(plan.value(), skipped);
        if (!stem.has_value()) {
            result.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }
        result.decodedAudioBytes += plan->requiredBytes;
        result.snapshot.audioStems.push_back(std::move(stem.value()));
    }

    result.ok = result.snapshot.isPlayable();
    result.message = result.ok
        ? describeProjectPlayback(result.snapshot)
        : (result.skippedAudioTracks.empty()
               ? "The project contains no playable MIDI or WAV tracks."
               : describeSkippedAudioTrack(result.skippedAudioTracks.front()));
    if (conflictingTempo) {
        result.message +=
            " Conflicting MIDI tempo metadata; using the first project track.";
    }
    return result;
}

SelectionPlaybackSummary createProjectPlaybackSummary(
    const core::ProjectModel& project,
    const SelectionPlaybackOptions& options)
{
    SelectionPlaybackSummary summary;
    summary.tempoMap = playbackTempoMapForProject(
        project,
        options,
        summary.tempoSource,
        summary.conflictingExplicitMidiTempo);
    if (summary.conflictingExplicitMidiTempo) {
        summary.diagnostic =
            "Conflicting MIDI tempo metadata; using the first project track.";
    }

    for (const auto& track : project.tracks()) {
        if (track.type == core::TrackType::midi
            && !track.midiNotes.empty()) {
            const auto midi =
                createMidiPlaybackSnapshot(track, summary.tempoMap);
            if (midi.ok) {
                ++summary.midiTrackCount;
                summary.durationSeconds = std::max(
                    summary.durationSeconds,
                    midi.snapshot.durationSeconds);
                summary.identity.midiTrackIds.push_back(track.id);
                if (!summary.identity.primaryMidiTrackId.has_value()) {
                    summary.identity.primaryMidiTrackId = track.id;
                }
            }
            continue;
        }

        if (track.type != core::TrackType::audio
            || track.audioSourcePath.empty()) {
            continue;
        }
        SkippedAudioTrack skipped;
        const auto plan = inspectAudioStem(track, skipped);
        if (!plan.has_value()) {
            summary.skippedAudioTracks.push_back(std::move(skipped));
            continue;
        }
        ++summary.audioTrackCount;
        summary.durationSeconds = std::max(
            summary.durationSeconds,
            static_cast<double>(plan->frameCount)
                / plan->sourceSampleRate);
        summary.identity.readableAudioTrackIds.push_back(track.id);
        if (!summary.firstReadableAudioPath.has_value()) {
            summary.firstReadableAudioPath = track.audioSourcePath;
        }
    }
    summary.playable =
        summary.midiTrackCount > 0 || summary.audioTrackCount > 0;
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

SelectionTransportCommandState projectTransportCommandState(
    const core::ProjectModel& project,
    TransportMode mode,
    double playableDurationSeconds)
{
    return projectTransportCommandState(
        projectHasPotentialPlayback(project),
        mode,
        playableDurationSeconds);
}

SelectionTransportCommandState projectTransportCommandState(
    bool projectPlayable,
    TransportMode mode,
    double playableDurationSeconds)
{
    SelectionTransportCommandState state;
    state.playEnabled = mode == TransportMode::paused
        || (mode == TransportMode::stopped
            && projectPlayable);
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

double selectionSummaryBpm(
    double transportSeconds,
    const SelectionPlaybackSummary& summary)
{
    const auto beat = midiSecondsToBeat(transportSeconds, summary.tempoMap);
    return playbackBpmAtBeat(beat, summary.tempoMap);
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

std::string describeProjectPlayback(const SelectionPlaybackSnapshot& snapshot)
{
    std::string description = "Playing project: ";
    if (snapshot.midiTrackCount() > 0) {
        description += countLabel(
            snapshot.midiTrackCount(),
            "MIDI track",
            "MIDI tracks");
    }
    if (snapshot.midiTrackCount() > 0
        && snapshot.audioTrackCount() > 0) {
        description += ", ";
    }
    if (snapshot.audioTrackCount() > 0) {
        description += countLabel(
            snapshot.audioTrackCount(),
            "audio track",
            "audio tracks");
    }
    return description;
}

std::string describeSkippedAudioTrack(const SkippedAudioTrack& skipped)
{
    const auto displayName = skipped.sourceFileName.empty()
        ? skipped.sourceTrackName
        : skipped.sourceFileName;
    if (skipped.reason
            == "selected audio exceeds the 512 MiB audition memory limit"
        || skipped.reason
            == "project audio exceeds the 512 MiB playback memory limit") {
        return "Skipped audio track " + displayName + ": " + skipped.reason;
    }
    return "Skipped unreadable audio track: " + displayName;
}

}  // namespace dawhermes::audio
