#pragma once

#include <cstddef>
#include <filesystem>
#include <optional>
#include <string>

#include <juce_audio_basics/juce_audio_basics.h>

#include "core/Track.h"

namespace dawhermes::midi {

struct MidiTrackExportOptions {
    int fallbackTicksPerQuarterNote { 960 };
    int fallbackMidiFileType { 1 };
};

struct MidiTrackExportResult {
    bool ok { false };
    std::string message;
    std::size_t exportedNoteCount { 0 };
    int ticksPerQuarterNote { 960 };
    int midiFileType { 1 };
};

bool canExportMidiTrack(const core::Track& track);

std::optional<juce::MidiFile> createMidiFileForTrack(
    const core::Track& track,
    const MidiTrackExportOptions& options,
    MidiTrackExportResult& result);

MidiTrackExportResult exportMidiTrackToFile(
    const core::Track& track,
    const std::filesystem::path& outputPath,
    const MidiTrackExportOptions& options = {});

}  // namespace dawhermes::midi
