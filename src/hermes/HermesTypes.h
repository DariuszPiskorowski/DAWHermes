#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

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
    std::string audioSourcePath;
};

struct HermesAudioMidiPairContext {
    HermesTrackContext audioTrack;
    HermesTrackContext midiTrack;
    std::vector<core::MidiNote> midiNotes;
    core::MidiSourceMetadata midiSourceMetadata;
};

struct HermesGeneratedMidiTrack {
    std::string trackName;
    std::string semanticLayer;
    bool enabledLayer { true };
    bool emptyLayer { false };
    std::vector<core::MidiNote> notes;
    std::optional<core::MidiSourceMetadata> midiSourceMetadata;
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
    std::string resultTrackName;
};

enum class HermesSyncRole {
    bass,
    drums,
    synth,
    guitar,
    other
};

struct HermesSyncOptions {
    HermesSyncRole role { HermesSyncRole::other };
    bool preserveTempoMap { true };
    std::optional<double> bpmOverride;
    std::string resultTrackName;
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

enum class HermesOperationKind {
    drumsExtraction,
    bassRepair,
    midiWavSynchronization
};

struct HermesOperationStatistics {
    std::size_t inputNoteCount { 0 };
    std::size_t outputNoteCount { 0 };
    std::size_t mergedCount { 0 };
    std::size_t insertedCount { 0 };
    std::size_t splitCount { 0 };
    std::size_t removedOrMutedCount { 0 };
    std::size_t alignedCount { 0 };
    std::size_t keepOriginalCount { 0 };
    std::size_t reviewTimingCount { 0 };
    std::size_t noAudioEvidenceCount { 0 };
};

struct HermesOperationResult {
    HermesOperationStatus status { HermesOperationStatus::notImplemented };
    std::string message;
    HermesOperationKind operationKind { HermesOperationKind::drumsExtraction };
    std::uint64_t sourceAudioTrackId { 0 };
    std::uint64_t sourceMidiTrackId { 0 };
    std::string resultName;
    HermesResultLayout resultLayout { HermesResultLayout::separateMidiTracks };
    std::vector<HermesGeneratedMidiTrack> generatedMidiTracks;
    int ticksPerQuarterNote { 960 };
    std::vector<core::MidiTempoEvent> tempoMap;
    HermesOperationStatistics statistics;
    double bpmUsed { 0.0 };
    double durationMs { 0.0 };
    std::vector<std::string> warnings;

    static HermesOperationResult success(
        std::string message = {},
        HermesResultLayout resultLayout = HermesResultLayout::separateMidiTracks,
        std::vector<HermesGeneratedMidiTrack> generatedMidiTracks = {},
        double bpmUsed = 0.0,
        std::vector<std::string> warnings = {},
        HermesOperationKind operationKind = HermesOperationKind::drumsExtraction);
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
    requiresAudioFile,
    requiresMidiTrack,
    requiresAudioAndMidi
};

}  // namespace dawhermes::hermes
