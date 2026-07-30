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
    std::uint64_t id { 0 };

    bool operator==(const MidiNote& other) const
    {
        return pitch == other.pitch
            && velocity == other.velocity
            && startBeat == other.startBeat
            && durationBeats == other.durationBeats
            && channel == other.channel
            && id == other.id;
    }
};

enum class MidiTrackOrigin {
    unknown,
    imported,
    generated
};

struct MidiTempoEvent {
    double beatPosition { 0.0 };
    int microsecondsPerQuarterNote { 500000 };

    bool operator==(const MidiTempoEvent& other) const = default;
};

struct MidiTimeSignatureEvent {
    double beatPosition { 0.0 };
    int numerator { 4 };
    int denominator { 4 };
};

struct MidiSourceMetadata {
    std::string sourceFilePath;
    std::string sourceFileName;
    std::optional<int> sourceTrackIndex;
    std::string sourceTrackName;
    int midiFileType { 1 };
    int ticksPerQuarterNote { 960 };
    std::vector<MidiTempoEvent> tempoMap;
    std::optional<bool> containsExplicitTempoEvents;
    std::vector<MidiTimeSignatureEvent> timeSignatureMap;
    std::vector<int> channelsUsed;
    std::size_t noteCount { 0 };
    double approximateDurationBeats { 0.0 };
    MidiTrackOrigin origin { MidiTrackOrigin::unknown };
};

struct AudioSourceMetadata {
    double sampleRate { 0.0 };
    int channelCount { 0 };
    double durationSeconds { 0.0 };
    std::uint64_t frameCount { 0 };
    int bitsPerSample { 0 };
    std::uint64_t fileSizeBytes { 0 };

    bool operator==(const AudioSourceMetadata& other) const = default;
};

enum class InstrumentKind {
    internalSynth,
    vst3
};

struct InstrumentAssignment {
    InstrumentKind kind { InstrumentKind::internalSynth };
    std::string pluginIdentifier;
    std::string pluginName;
    std::string pluginManufacturer;
    std::string pluginFileOrIdentifier;

    bool operator==(const InstrumentAssignment& other) const = default;
};

struct Track {
    std::uint64_t id{};
    std::string name;
    TrackType type { TrackType::audio };
    std::uint64_t parentTrackId { 0 };
    bool muted { false };
    bool soloed { false };
    std::string audioSourcePath;
    std::optional<AudioSourceMetadata> audioSourceMetadata;
    std::vector<MidiNote> midiNotes;
    std::optional<MidiSourceMetadata> midiSourceMetadata;
    InstrumentAssignment instrument;
    std::string generatedGroupId;
};

}  // namespace dawhermes::core
