#include "audio/MidiPlaybackModel.h"

#include <algorithm>
#include <cmath>
#include <limits>

#include "core/MidiNoteEditing.h"
#include "core/MidiTimeMap.h"

namespace dawhermes::audio {

namespace {

constexpr double kMinimumNoteDurationBeats = 1.0 / 960.0;
constexpr double kTimeEpsilon = 1.0e-9;

double secondsPerBeat(const core::MidiTempoEvent& event)
{
    return static_cast<double>(std::max(1, event.microsecondsPerQuarterNote)) / 1000000.0;
}

std::vector<core::MidiTempoEvent> playbackTempoMap(const std::vector<core::MidiTempoEvent>& tempoMap)
{
    return core::sanitizeTempoMap(tempoMap);
}

}  // namespace

double midiBeatToSeconds(double beat, const std::vector<core::MidiTempoEvent>& tempoMap)
{
    const auto targetBeat = std::isfinite(beat) ? std::max(0.0, beat) : 0.0;
    const auto map = playbackTempoMap(tempoMap);

    double seconds = 0.0;
    double segmentBeat = 0.0;
    auto activeTempo = map.front();

    for (std::size_t index = 1; index < map.size(); ++index) {
        const auto nextBeat = map[index].beatPosition;
        if (nextBeat >= targetBeat) {
            break;
        }

        seconds += std::max(0.0, nextBeat - segmentBeat) * secondsPerBeat(activeTempo);
        segmentBeat = nextBeat;
        activeTempo = map[index];
    }

    seconds += std::max(0.0, targetBeat - segmentBeat) * secondsPerBeat(activeTempo);
    return std::max(0.0, seconds);
}

double midiSecondsToBeat(double seconds, const std::vector<core::MidiTempoEvent>& tempoMap)
{
    auto remainingSeconds = std::isfinite(seconds) ? std::max(0.0, seconds) : 0.0;
    const auto map = playbackTempoMap(tempoMap);

    double segmentBeat = 0.0;
    auto activeTempo = map.front();

    for (std::size_t index = 1; index < map.size(); ++index) {
        const auto nextBeat = map[index].beatPosition;
        const auto segmentSeconds = std::max(0.0, nextBeat - segmentBeat) * secondsPerBeat(activeTempo);
        if (remainingSeconds <= segmentSeconds + kTimeEpsilon) {
            return segmentBeat + (remainingSeconds / secondsPerBeat(activeTempo));
        }

        remainingSeconds -= segmentSeconds;
        segmentBeat = nextBeat;
        activeTempo = map[index];
    }

    return segmentBeat + (remainingSeconds / secondsPerBeat(activeTempo));
}

bool canAuditionMidiTrack(const core::Track& track)
{
    return track.type == core::TrackType::midi && !track.midiNotes.empty();
}

MidiPlaybackSnapshotResult createMidiPlaybackSnapshot(const core::Track& track)
{
    MidiPlaybackSnapshotResult result;
    if (track.type != core::TrackType::midi) {
        result.message = "Selected track is not a MIDI track.";
        return result;
    }

    if (track.midiNotes.empty()) {
        result.message = "Selected MIDI track is empty.";
        return result;
    }

    result.snapshot.sourceTrackId = track.id;
    result.snapshot.sourceTrackName = track.name;
    result.snapshot.tempoMap = track.midiSourceMetadata.has_value()
        ? playbackTempoMap(track.midiSourceMetadata->tempoMap)
        : playbackTempoMap({});
    result.snapshot.events.reserve(track.midiNotes.size() * 2);

    std::uint64_t instanceId = 1;
    for (const auto& note : track.midiNotes) {
        const auto startBeat = std::isfinite(note.startBeat) ? std::max(0.0, note.startBeat) : 0.0;
        const auto durationBeats = std::isfinite(note.durationBeats)
            ? std::max(kMinimumNoteDurationBeats, note.durationBeats)
            : kMinimumNoteDurationBeats;
        const auto endBeat = startBeat + durationBeats;
        const auto pitch = core::clampMidiPitch(note.pitch);
        const auto velocity = core::clampMidiVelocity(note.velocity);
        const auto amplitude = static_cast<float>(velocity) / 127.0f;

        result.snapshot.events.push_back(MidiPlaybackEvent {
            midiBeatToSeconds(startBeat, result.snapshot.tempoMap),
            MidiPlaybackEventKind::noteOn,
            pitch,
            amplitude,
            instanceId
        });
        result.snapshot.events.push_back(MidiPlaybackEvent {
            midiBeatToSeconds(endBeat, result.snapshot.tempoMap),
            MidiPlaybackEventKind::noteOff,
            pitch,
            0.0f,
            instanceId
        });
        ++instanceId;
    }

    std::stable_sort(result.snapshot.events.begin(), result.snapshot.events.end(), [](const auto& left, const auto& right) {
        if (std::abs(left.timeSeconds - right.timeSeconds) > kTimeEpsilon) {
            return left.timeSeconds < right.timeSeconds;
        }

        if (left.kind != right.kind) {
            return left.kind == MidiPlaybackEventKind::noteOff;
        }

        if (left.pitch != right.pitch) {
            return left.pitch < right.pitch;
        }

        return left.noteInstanceId < right.noteInstanceId;
    });

    result.snapshot.durationSeconds = result.snapshot.events.empty()
        ? 0.0
        : result.snapshot.events.back().timeSeconds;
    result.ok = true;
    result.message = "MIDI playback snapshot created.";
    return result;
}

MidiTransportCommandState midiTransportCommandState(
    const std::optional<core::Track>& primarySelectedTrack,
    bool isPlaying)
{
    MidiTransportCommandState state;
    state.playEnabled = !isPlaying
        && primarySelectedTrack.has_value()
        && canAuditionMidiTrack(primarySelectedTrack.value());
    state.stopEnabled = isPlaying;
    state.panicEnabled = true;
    return state;
}

}  // namespace dawhermes::audio
