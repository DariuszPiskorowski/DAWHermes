#include "midi/MidiTrackExporter.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>
#include <utility>
#include <vector>

#include "core/MidiNoteEditing.h"
#include "core/MidiTimeMap.h"

namespace dawhermes::midi {

namespace {

constexpr double kEpsilon = 1.0e-9;
constexpr int kTrackNameMetaEventType = 0x03;

enum class ExportEventKind {
    trackName,
    tempo,
    timeSignature,
    noteOff,
    noteOn
};

struct ExportEvent {
    int tick { 0 };
    ExportEventKind kind { ExportEventKind::noteOn };
    int channel { 0 };
    int pitch { 0 };
    int velocity { 0 };
    juce::MidiMessage message;
};

int sanitizeTicksPerQuarterNote(const core::Track& track, const MidiTrackExportOptions& options)
{
    if (track.midiSourceMetadata.has_value() && track.midiSourceMetadata->ticksPerQuarterNote > 0) {
        return track.midiSourceMetadata->ticksPerQuarterNote;
    }

    return std::max(1, options.fallbackTicksPerQuarterNote);
}

int sanitizeMidiFileType(const core::Track& track, const MidiTrackExportOptions& options)
{
    const auto requestedType = track.midiSourceMetadata.has_value()
        ? track.midiSourceMetadata->midiFileType
        : options.fallbackMidiFileType;

    if (requestedType >= 0 && requestedType <= 2) {
        return requestedType;
    }

    return 1;
}

int beatToTick(double beat, int ticksPerQuarterNote)
{
    const auto safeBeat = std::isfinite(beat) ? std::max(0.0, beat) : 0.0;
    const auto ticks = std::llround(safeBeat * static_cast<double>(std::max(1, ticksPerQuarterNote)));
    if (ticks < 0) {
        return 0;
    }

    if (ticks > static_cast<long long>(std::numeric_limits<int>::max())) {
        return std::numeric_limits<int>::max();
    }

    return static_cast<int>(ticks);
}

int durationToTicks(double durationBeats, int ticksPerQuarterNote)
{
    const auto safeDuration = std::isfinite(durationBeats) ? std::max(0.0, durationBeats) : 0.0;
    const auto ticks = std::llround(safeDuration * static_cast<double>(std::max(1, ticksPerQuarterNote)));
    return std::max(1, static_cast<int>(std::min<long long>(
                           std::max<long long>(1, ticks),
                           static_cast<long long>(std::numeric_limits<int>::max()))));
}

int eventKindPriority(ExportEventKind kind)
{
    switch (kind) {
    case ExportEventKind::trackName:
        return 0;
    case ExportEventKind::tempo:
        return 1;
    case ExportEventKind::timeSignature:
        return 2;
    case ExportEventKind::noteOff:
        return 3;
    case ExportEventKind::noteOn:
        return 4;
    default:
        return 99;
    }
}

void addSortedEvents(juce::MidiMessageSequence& sequence, std::vector<ExportEvent> events)
{
    std::stable_sort(events.begin(), events.end(), [](const auto& left, const auto& right) {
        if (left.tick != right.tick) {
            return left.tick < right.tick;
        }

        const auto leftPriority = eventKindPriority(left.kind);
        const auto rightPriority = eventKindPriority(right.kind);
        if (leftPriority != rightPriority) {
            return leftPriority < rightPriority;
        }

        if (left.channel != right.channel) {
            return left.channel < right.channel;
        }

        if (left.pitch != right.pitch) {
            return left.pitch < right.pitch;
        }

        return left.velocity < right.velocity;
    });

    for (auto& event : events) {
        event.message.setTimeStamp(static_cast<double>(std::max(0, event.tick)));
        sequence.addEvent(event.message);
    }

    sequence.updateMatchedPairs();
}

std::vector<core::MidiTempoEvent> tempoMapForTrack(const core::Track& track)
{
    if (track.midiSourceMetadata.has_value()) {
        return core::sanitizeTempoMap(track.midiSourceMetadata->tempoMap);
    }

    return core::sanitizeTempoMap({});
}

std::vector<core::MidiTimeSignatureEvent> timeSignatureMapForTrack(const core::Track& track)
{
    if (track.midiSourceMetadata.has_value()) {
        return core::sanitizeTimeSignatureMap(track.midiSourceMetadata->timeSignatureMap);
    }

    return core::sanitizeTimeSignatureMap({});
}

std::string trackNameForExport(const core::Track& track)
{
    if (!track.name.empty()) {
        return track.name;
    }

    if (track.midiSourceMetadata.has_value() && !track.midiSourceMetadata->sourceTrackName.empty()) {
        return track.midiSourceMetadata->sourceTrackName;
    }

    return "MIDI Track";
}

void addMetaEvents(
    juce::MidiMessageSequence& sequence,
    const core::Track& track,
    int ticksPerQuarterNote,
    bool includeTrackName)
{
    std::vector<ExportEvent> events;

    if (includeTrackName) {
        ExportEvent trackNameEvent;
        trackNameEvent.kind = ExportEventKind::trackName;
        trackNameEvent.message = juce::MidiMessage::textMetaEvent(
            kTrackNameMetaEventType,
            juce::String(trackNameForExport(track)));
        events.push_back(std::move(trackNameEvent));
    }

    for (const auto& tempo : tempoMapForTrack(track)) {
        ExportEvent event;
        event.tick = beatToTick(tempo.beatPosition, ticksPerQuarterNote);
        event.kind = ExportEventKind::tempo;
        event.message = juce::MidiMessage::tempoMetaEvent(std::max(1, tempo.microsecondsPerQuarterNote));
        events.push_back(std::move(event));
    }

    for (const auto& signature : timeSignatureMapForTrack(track)) {
        ExportEvent event;
        event.tick = beatToTick(signature.beatPosition, ticksPerQuarterNote);
        event.kind = ExportEventKind::timeSignature;
        event.message = juce::MidiMessage::timeSignatureMetaEvent(
            std::clamp(signature.numerator, 1, 32),
            std::clamp(signature.denominator, 1, 32));
        events.push_back(std::move(event));
    }

    addSortedEvents(sequence, std::move(events));
}

void addNoteEvents(
    juce::MidiMessageSequence& sequence,
    const core::Track& track,
    int ticksPerQuarterNote,
    bool includeTrackName)
{
    std::vector<ExportEvent> events;
    events.reserve(track.midiNotes.size() * 2 + 1);

    if (includeTrackName) {
        ExportEvent trackNameEvent;
        trackNameEvent.kind = ExportEventKind::trackName;
        trackNameEvent.message = juce::MidiMessage::textMetaEvent(
            kTrackNameMetaEventType,
            juce::String(trackNameForExport(track)));
        events.push_back(std::move(trackNameEvent));
    }

    for (const auto& note : track.midiNotes) {
        const auto channel = core::clampMidiChannel(note.channel);
        const auto pitch = core::clampMidiPitch(note.pitch);
        const auto velocity = core::clampMidiVelocity(note.velocity);
        const auto startTick = beatToTick(note.startBeat, ticksPerQuarterNote);
        const auto durationTicks = durationToTicks(note.durationBeats, ticksPerQuarterNote);
        const auto endTick = durationTicks > std::numeric_limits<int>::max() - startTick
            ? std::numeric_limits<int>::max()
            : startTick + durationTicks;

        ExportEvent noteOffEvent;
        noteOffEvent.tick = endTick;
        noteOffEvent.kind = ExportEventKind::noteOff;
        noteOffEvent.channel = channel;
        noteOffEvent.pitch = pitch;
        noteOffEvent.message = juce::MidiMessage::noteOff(channel, pitch);
        events.push_back(std::move(noteOffEvent));

        ExportEvent noteOnEvent;
        noteOnEvent.tick = startTick;
        noteOnEvent.kind = ExportEventKind::noteOn;
        noteOnEvent.channel = channel;
        noteOnEvent.pitch = pitch;
        noteOnEvent.velocity = velocity;
        noteOnEvent.message = juce::MidiMessage::noteOn(channel, pitch, static_cast<juce::uint8>(velocity));
        events.push_back(std::move(noteOnEvent));
    }

    addSortedEvents(sequence, std::move(events));
}

}  // namespace

bool canExportMidiTrack(const core::Track& track)
{
    return track.type == core::TrackType::midi && !track.midiNotes.empty();
}

std::optional<juce::MidiFile> createMidiFileForTrack(
    const core::Track& track,
    const MidiTrackExportOptions& options,
    MidiTrackExportResult& result)
{
    result = MidiTrackExportResult {};
    result.ticksPerQuarterNote = sanitizeTicksPerQuarterNote(track, options);
    result.midiFileType = sanitizeMidiFileType(track, options);

    if (track.type != core::TrackType::midi) {
        result.message = "Selected track is not a MIDI track.";
        return std::nullopt;
    }

    if (track.midiNotes.empty()) {
        result.message = "Selected MIDI track is empty.";
        return std::nullopt;
    }

    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(result.ticksPerQuarterNote);

    if (result.midiFileType == 0) {
        juce::MidiMessageSequence singleTrack;
        addMetaEvents(singleTrack, track, result.ticksPerQuarterNote, true);
        addNoteEvents(singleTrack, track, result.ticksPerQuarterNote, false);
        midiFile.addTrack(singleTrack);
    } else {
        juce::MidiMessageSequence metaTrack;
        addMetaEvents(metaTrack, track, result.ticksPerQuarterNote, false);
        midiFile.addTrack(metaTrack);

        juce::MidiMessageSequence noteTrack;
        addNoteEvents(noteTrack, track, result.ticksPerQuarterNote, true);
        midiFile.addTrack(noteTrack);
    }

    result.ok = true;
    result.message = "MIDI track exported.";
    result.exportedNoteCount = track.midiNotes.size();
    return midiFile;
}

MidiTrackExportResult exportMidiTrackToFile(
    const core::Track& track,
    const std::filesystem::path& outputPath,
    const MidiTrackExportOptions& options)
{
    MidiTrackExportResult result;

    if (outputPath.empty()) {
        result.message = "No output MIDI file path was provided.";
        return result;
    }

    const auto midiFile = createMidiFileForTrack(track, options, result);
    if (!midiFile.has_value()) {
        return result;
    }

    juce::File outputFile(outputPath.string());
    juce::FileOutputStream stream(outputFile);
    if (!stream.openedOk()) {
        result.ok = false;
        result.message = "Unable to open MIDI export file for writing.";
        return result;
    }

    if (!stream.setPosition(0) || stream.truncate().failed()) {
        result.ok = false;
        result.message = "Unable to replace the existing MIDI export file.";
        return result;
    }

    if (!midiFile->writeTo(stream, result.midiFileType)) {
        result.ok = false;
        result.message = "Unable to write MIDI export file.";
        return result;
    }

    stream.flush();
    result.ok = true;
    result.message = "MIDI track exported.";
    return result;
}

}  // namespace dawhermes::midi
