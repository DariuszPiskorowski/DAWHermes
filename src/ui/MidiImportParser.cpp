#include "ui/MidiImportParser.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <utility>

#include <juce_audio_formats/juce_audio_formats.h>

namespace dawhermes::ui {

namespace {

std::string defaultTrackName(int sourceTrackIndex)
{
    return "Track " + std::to_string(sourceTrackIndex + 1);
}

bool isNoteOffMessage(const juce::MidiMessage& message)
{
    if (message.isNoteOff()) {
        return true;
    }

    return message.isNoteOn() && message.getVelocity() <= 0;
}

std::vector<core::MidiTempoEvent> parseTempoMap(const juce::MidiFile& midiFile, int ticksPerQuarterNote)
{
    std::vector<core::MidiTempoEvent> tempoMap;

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex) {
        const auto* sequence = midiFile.getTrack(trackIndex);
        if (sequence == nullptr) {
            continue;
        }

        for (int eventIndex = 0; eventIndex < sequence->getNumEvents(); ++eventIndex) {
            const auto* event = sequence->getEventPointer(eventIndex);
            if (event == nullptr || !event->message.isTempoMetaEvent()) {
                continue;
            }

            core::MidiTempoEvent tempoEvent;
            tempoEvent.beatPosition = std::max(
                0.0,
                static_cast<double>(event->message.getTimeStamp()) / static_cast<double>(ticksPerQuarterNote));
            tempoEvent.microsecondsPerQuarterNote = std::max(
                1,
                static_cast<int>(std::llround(event->message.getTempoSecondsPerQuarterNote() * 1000000.0)));
            tempoMap.push_back(tempoEvent);
        }
    }

    if (tempoMap.empty()) {
        tempoMap.push_back(core::MidiTempoEvent {});
    }

    std::sort(tempoMap.begin(), tempoMap.end(), [](const auto& left, const auto& right) {
        if (left.beatPosition == right.beatPosition) {
            return left.microsecondsPerQuarterNote < right.microsecondsPerQuarterNote;
        }

        return left.beatPosition < right.beatPosition;
    });

    return tempoMap;
}

std::vector<core::MidiTimeSignatureEvent> parseTimeSignatureMap(
    const juce::MidiFile& midiFile,
    int ticksPerQuarterNote)
{
    std::vector<core::MidiTimeSignatureEvent> timeSignatures;

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex) {
        const auto* sequence = midiFile.getTrack(trackIndex);
        if (sequence == nullptr) {
            continue;
        }

        for (int eventIndex = 0; eventIndex < sequence->getNumEvents(); ++eventIndex) {
            const auto* event = sequence->getEventPointer(eventIndex);
            if (event == nullptr || !event->message.isTimeSignatureMetaEvent()) {
                continue;
            }

            int numerator = 4;
            int denominator = 4;
            event->message.getTimeSignatureInfo(numerator, denominator);

            core::MidiTimeSignatureEvent signature;
            signature.beatPosition = std::max(
                0.0,
                static_cast<double>(event->message.getTimeStamp()) / static_cast<double>(ticksPerQuarterNote));
            signature.numerator = std::max(1, numerator);
            signature.denominator = std::max(1, denominator);
            timeSignatures.push_back(signature);
        }
    }

    if (timeSignatures.empty()) {
        timeSignatures.push_back(core::MidiTimeSignatureEvent {});
    }

    std::sort(timeSignatures.begin(), timeSignatures.end(), [](const auto& left, const auto& right) {
        if (left.beatPosition == right.beatPosition) {
            if (left.numerator == right.numerator) {
                return left.denominator < right.denominator;
            }

            return left.numerator < right.numerator;
        }

        return left.beatPosition < right.beatPosition;
    });

    return timeSignatures;
}

}  // namespace

std::optional<MidiImportDocument> parseMidiImportDocument(
    const std::filesystem::path& filePath,
    std::string& error)
{
    error.clear();

    const juce::File midiFilePath(filePath.string());
    if (!midiFilePath.existsAsFile()) {
        error = "Selected MIDI file does not exist.";
        return std::nullopt;
    }

    juce::FileInputStream stream(midiFilePath);
    if (!stream.openedOk()) {
        error = "Unable to open selected MIDI file.";
        return std::nullopt;
    }

    juce::MidiFile midiFile;
    int midiFileType = 1;
    if (!midiFile.readFrom(stream, true, &midiFileType)) {
        error = "Selected file is not a readable MIDI file.";
        return std::nullopt;
    }

    const auto ticksPerQuarterNote = static_cast<int>(midiFile.getTimeFormat());
    if (ticksPerQuarterNote <= 0) {
        error = "SMPTE-based MIDI time formats are not supported for import.";
        return std::nullopt;
    }

    MidiImportDocument document;
    document.sourceFilePath = filePath.string();
    document.sourceFileName = midiFilePath.getFileName().toStdString();
    document.midiFileType = midiFileType;
    document.ticksPerQuarterNote = ticksPerQuarterNote;
    document.totalSourceTrackCount = midiFile.getNumTracks();
    document.tempoMap = parseTempoMap(midiFile, ticksPerQuarterNote);
    document.timeSignatureMap = parseTimeSignatureMap(midiFile, ticksPerQuarterNote);

    for (int trackIndex = 0; trackIndex < midiFile.getNumTracks(); ++trackIndex) {
        const auto* sequence = midiFile.getTrack(trackIndex);
        if (sequence == nullptr) {
            continue;
        }

        std::string trackName = defaultTrackName(trackIndex);
        std::map<std::pair<int, int>, std::deque<std::pair<double, int>>> activeNotes;
        std::vector<core::MidiNote> parsedNotes;
        std::set<int> channelsUsed;
        double lastEventTick = 0.0;

        for (int eventIndex = 0; eventIndex < sequence->getNumEvents(); ++eventIndex) {
            const auto* event = sequence->getEventPointer(eventIndex);
            if (event == nullptr) {
                continue;
            }

            const auto& message = event->message;
            const auto tickTime = static_cast<double>(message.getTimeStamp());
            lastEventTick = std::max(lastEventTick, tickTime);

            if (message.isTrackNameEvent()) {
                const auto importedName = message.getTextFromTextMetaEvent().trim().toStdString();
                if (!importedName.empty()) {
                    trackName = importedName;
                }
            }

            if (message.isNoteOn() && message.getVelocity() > 0) {
                const auto key = std::make_pair(message.getChannel(), message.getNoteNumber());
                activeNotes[key].push_back({ tickTime, std::clamp(static_cast<int>(message.getVelocity()), 1, 127) });
                channelsUsed.insert(std::clamp(message.getChannel(), 1, 16));
                continue;
            }

            if (!isNoteOffMessage(message)) {
                continue;
            }

            const auto key = std::make_pair(message.getChannel(), message.getNoteNumber());
            auto activeIt = activeNotes.find(key);
            if (activeIt == activeNotes.end() || activeIt->second.empty()) {
                continue;
            }

            const auto active = activeIt->second.front();
            activeIt->second.pop_front();

            const auto startTick = std::max(0.0, active.first);
            const auto endTick = std::max(startTick + 1.0, tickTime);

            core::MidiNote note;
            note.pitch = std::clamp(message.getNoteNumber(), 0, 127);
            note.velocity = std::clamp(active.second, 1, 127);
            note.channel = std::clamp(message.getChannel(), 1, 16);
            note.startBeat = startTick / static_cast<double>(ticksPerQuarterNote);
            note.durationBeats = std::max(
                1.0 / static_cast<double>(ticksPerQuarterNote),
                (endTick - startTick) / static_cast<double>(ticksPerQuarterNote));
            parsedNotes.push_back(note);
            channelsUsed.insert(note.channel);
        }

        for (auto& [noteKey, pendingNotes] : activeNotes) {
            juce::ignoreUnused(noteKey);
            while (!pendingNotes.empty()) {
                const auto active = pendingNotes.front();
                pendingNotes.pop_front();

                const auto startTick = std::max(0.0, active.first);
                const auto endTick = std::max(startTick + 1.0, lastEventTick + 1.0);

                core::MidiNote note;
                note.pitch = std::clamp(noteKey.second, 0, 127);
                note.velocity = std::clamp(active.second, 1, 127);
                note.channel = std::clamp(noteKey.first, 1, 16);
                note.startBeat = startTick / static_cast<double>(ticksPerQuarterNote);
                note.durationBeats = std::max(
                    1.0 / static_cast<double>(ticksPerQuarterNote),
                    (endTick - startTick) / static_cast<double>(ticksPerQuarterNote));
                parsedNotes.push_back(note);
                channelsUsed.insert(note.channel);
            }
        }

        if (parsedNotes.empty()) {
            continue;
        }

        std::sort(parsedNotes.begin(), parsedNotes.end(), [](const auto& left, const auto& right) {
            if (left.startBeat == right.startBeat) {
                if (left.pitch == right.pitch) {
                    return left.channel < right.channel;
                }

                return left.pitch < right.pitch;
            }

            return left.startBeat < right.startBeat;
        });

        MidiImportTrackCandidate candidate;
        candidate.sourceTrackIndex = trackIndex;
        candidate.sourceTrackName = trackName;
        candidate.notes = std::move(parsedNotes);
        candidate.channelsUsed.assign(channelsUsed.begin(), channelsUsed.end());

        if (!candidate.notes.empty()) {
            const auto& lastNote = candidate.notes.back();
            candidate.approximateDurationBeats = lastNote.startBeat + lastNote.durationBeats;
            document.approximateDurationBeats = std::max(document.approximateDurationBeats, candidate.approximateDurationBeats);
        }

        document.noteBearingTracks.push_back(std::move(candidate));
    }

    return document;
}

core::MidiSourceMetadata makeImportedMidiSourceMetadata(
    const MidiImportDocument& document,
    const MidiImportTrackCandidate& trackCandidate)
{
    core::MidiSourceMetadata metadata;
    metadata.sourceFilePath = document.sourceFilePath;
    metadata.sourceFileName = document.sourceFileName;
    metadata.sourceTrackIndex = trackCandidate.sourceTrackIndex;
    metadata.sourceTrackName = trackCandidate.sourceTrackName;
    metadata.midiFileType = document.midiFileType;
    metadata.ticksPerQuarterNote = std::max(1, document.ticksPerQuarterNote);
    metadata.tempoMap = document.tempoMap;
    metadata.timeSignatureMap = document.timeSignatureMap;
    metadata.channelsUsed = trackCandidate.channelsUsed;
    metadata.noteCount = trackCandidate.notes.size();
    metadata.approximateDurationBeats = trackCandidate.approximateDurationBeats;
    metadata.origin = core::MidiTrackOrigin::imported;
    return metadata;
}

std::optional<WavFileInspection> inspectWavFile(
    const std::filesystem::path& filePath,
    std::string& error)
{
    error.clear();

    const juce::File wavPath(filePath.string());
    if (!wavPath.existsAsFile()) {
        error = "WAV file does not exist.";
        return std::nullopt;
    }

    juce::AudioFormatManager formatManager;
    formatManager.registerBasicFormats();

    auto inputStream = wavPath.createInputStream();
    if (inputStream == nullptr) {
        error = "Unable to open WAV file.";
        return std::nullopt;
    }

    std::unique_ptr<juce::AudioFormatReader> reader(formatManager.createReaderFor(std::move(inputStream)));
    if (reader == nullptr) {
        error = "Unable to parse WAV file metadata.";
        return std::nullopt;
    }

    WavFileInspection metadata;
    metadata.sampleRate = reader->sampleRate;
    metadata.channelCount = static_cast<int>(reader->numChannels);
    metadata.bitsPerSample = reader->bitsPerSample;
    metadata.durationSeconds =
        reader->sampleRate > 0.0
            ? static_cast<double>(reader->lengthInSamples) / reader->sampleRate
            : 0.0;
    metadata.fileSizeBytes = static_cast<std::uint64_t>(wavPath.getSize());
    return metadata;
}

}  // namespace dawhermes::ui
