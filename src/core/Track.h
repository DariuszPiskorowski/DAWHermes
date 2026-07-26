#pragma once

#include <cstdint>
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

struct Track {
    std::uint64_t id{};
    std::string name;
    TrackType type { TrackType::audio };
    std::uint64_t parentTrackId { 0 };
    std::string audioSourcePath;
    std::vector<MidiNote> midiNotes;
    std::string generatedGroupId;
};

}  // namespace dawhermes::core
