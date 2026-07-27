#pragma once

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "core/Track.h"

namespace dawhermes::ui {

struct MidiImportTrackCandidate {
    int sourceTrackIndex { 0 };
    std::string sourceTrackName;
    std::vector<core::MidiNote> notes;
    std::vector<int> channelsUsed;
    double approximateDurationBeats { 0.0 };
};

struct MidiImportDocument {
    std::string sourceFilePath;
    std::string sourceFileName;
    int midiFileType { 1 };
    int ticksPerQuarterNote { 960 };
    int totalSourceTrackCount { 0 };
    std::vector<core::MidiTempoEvent> tempoMap;
    std::vector<core::MidiTimeSignatureEvent> timeSignatureMap;
    std::vector<MidiImportTrackCandidate> noteBearingTracks;
    double approximateDurationBeats { 0.0 };
};

struct WavFileInspection {
    double sampleRate { 0.0 };
    int channelCount { 0 };
    int bitsPerSample { 0 };
    double durationSeconds { 0.0 };
    std::uint64_t fileSizeBytes { 0 };
};

std::optional<MidiImportDocument> parseMidiImportDocument(
    const std::filesystem::path& filePath,
    std::string& error);

core::MidiSourceMetadata makeImportedMidiSourceMetadata(
    const MidiImportDocument& document,
    const MidiImportTrackCandidate& trackCandidate);

std::optional<WavFileInspection> inspectWavFile(
    const std::filesystem::path& filePath,
    std::string& error);

}  // namespace dawhermes::ui
