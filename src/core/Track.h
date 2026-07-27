#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace dawhermes::core {

enum class TrackType {
    audio,
    midi,
    group
};

struct MidiNote {
    int pitch { 60 };
    int velocity { 100 };
    double startBeat { 0.0 };
    double durationBeats { 0.25 };
    int channel { 1 };
};

enum class MidiTrackOrigin {
    unknown,
    imported,
    generated
};

struct MidiTempoEvent {
    double beatPosition { 0.0 };
    int microsecondsPerQuarterNote { 500000 };
};

struct MidiSourceMetadata {
    std::string sourceFilePath;
    std::string sourceFileName;
    std::optional<int> sourceTrackIndex;
    std::string sourceTrackName;
    int midiFileType { 1 };
    int ticksPerQuarterNote { 960 };
    std::vector<MidiTempoEvent> tempoMap;
    std::vector<int> channelsUsed;
    std::size_t noteCount { 0 };
    double approximateDurationBeats { 0.0 };
    MidiTrackOrigin origin { MidiTrackOrigin::unknown };
};

struct Track {
    std::uint64_t id{};
    std::string name;
    TrackType type { TrackType::audio };
    std::uint64_t parentTrackId { 0 };
    std::string audioSourcePath;
    std::vector<MidiNote> midiNotes;
    std::optional<MidiSourceMetadata> midiSourceMetadata;
    std::string generatedGroupId;
};

}  // namespace dawhermes::core
