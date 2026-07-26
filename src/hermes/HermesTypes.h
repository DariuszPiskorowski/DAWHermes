#pragma once

#include <cstdint>
#include <string>

#include "core/Track.h"

namespace dawhermes::hermes {

enum class HermesResultLayout {
    separateMidiTracks,
    groupedMultitrack,
    singleDrumTrack
};

enum class HermesDrumsProfile {
    conservative,
    balanced,
    sensitive
};

enum class HermesDetectionMode {
    multiDetector,
    global
};

enum class HermesTargetMapping {
    ujamKandy,
    generalMidi,
    sitala,
    custom
};

struct HermesTrackContext {
    std::uint64_t trackId{};
    std::string trackName;
    core::TrackType trackType { core::TrackType::audio };
};

struct HermesDrumsOptions {
    HermesResultLayout resultLayout { HermesResultLayout::separateMidiTracks };
    HermesDrumsProfile profile { HermesDrumsProfile::conservative };
    HermesDetectionMode detectionMode { HermesDetectionMode::multiDetector };
    HermesTargetMapping targetMapping { HermesTargetMapping::ujamKandy };
    int c1MidiNote { 36 };
    bool createEmptyEnabledLayers { false };
};

struct HermesBassOptions {
};

struct HermesSyncOptions {
};

struct HermesBpmOptions {
    double bpm { 120.0 };
};

enum class HermesOperationStatus {
    success,
    notImplemented,
    invalidInput,
    unavailable
};

struct HermesOperationResult {
    HermesOperationStatus status { HermesOperationStatus::notImplemented };
    std::string message;

    static HermesOperationResult success(std::string message = {});
    static HermesOperationResult notImplemented(std::string message = {});
    static HermesOperationResult invalidInput(std::string message = {});
    static HermesOperationResult unavailable(std::string message = {});

    bool isSuccess() const { return status == HermesOperationStatus::success; }
};

enum class HermesCommand {
    drumsMakeMidiFromWav,
    drumsMapping,
    bassMakeRepairMidiFromWav,
    synchronizeMidiWithWav,
    setFixBpm
};

enum class HermesCommandAvailability {
    enabled,
    requiresAudioTrack,
    requiresMidiTrack,
    requiresAudioAndMidi
};

}  // namespace dawhermes::hermes
