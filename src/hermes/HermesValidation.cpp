#include "hermes/HermesValidation.h"

#include <cmath>
#include <filesystem>

namespace dawhermes::hermes {

bool isValidResultLayout(HermesResultLayout value)
{
    switch (value) {
    case HermesResultLayout::separateMidiTracks:
    case HermesResultLayout::groupedMultitrack:
    case HermesResultLayout::singleDrumTrack:
        return true;
    default:
        return false;
    }
}

bool isValidDrumsProfile(HermesDrumsProfile value)
{
    switch (value) {
    case HermesDrumsProfile::conservative:
    case HermesDrumsProfile::balanced:
    case HermesDrumsProfile::sensitive:
        return true;
    default:
        return false;
    }
}

bool isValidDetectionMode(HermesDetectionMode value)
{
    switch (value) {
    case HermesDetectionMode::multiDetector:
    case HermesDetectionMode::global:
        return true;
    default:
        return false;
    }
}

bool isValidTargetMapping(HermesTargetMapping value)
{
    switch (value) {
    case HermesTargetMapping::ujamKandy:
    case HermesTargetMapping::generalMidi:
    case HermesTargetMapping::sitala:
    case HermesTargetMapping::custom:
        return true;
    default:
        return false;
    }
}

ValidationResult validateDrumsOptions(const HermesDrumsOptions& options)
{
    if (!isValidResultLayout(options.resultLayout)) {
        return ValidationResult::fail("Invalid result layout.");
    }

    if (!isValidDrumsProfile(options.profile)) {
        return ValidationResult::fail("Invalid drums profile.");
    }

    if (!isValidDetectionMode(options.detectionMode)) {
        return ValidationResult::fail("Invalid detection mode.");
    }

    if (!isValidTargetMapping(options.targetMapping)) {
        return ValidationResult::fail("Invalid target mapping.");
    }

    if (options.c1MidiNote < 0 || options.c1MidiNote > 127) {
        return ValidationResult::fail("C1 MIDI note must be in range 0..127.");
    }

    return ValidationResult::pass();
}

ValidationResult validateBassOptions(const HermesBassOptions&)
{
    return ValidationResult::pass();
}

ValidationResult validateSyncOptions(const HermesSyncOptions& options)
{
    if (!options.preserveTempoMap) {
        if (!options.bpmOverride.has_value()) {
            return ValidationResult::fail("A BPM override is required when tempo preservation is disabled.");
        }

        if (!std::isfinite(options.bpmOverride.value()) || options.bpmOverride.value() <= 0.0) {
            return ValidationResult::fail("BPM override must be a finite value greater than zero.");
        }
    }

    return ValidationResult::pass();
}

ValidationResult validateBpmOptions(const HermesBpmOptions& options)
{
    if (!std::isfinite(options.bpm)) {
        return ValidationResult::fail("BPM must be a finite number.");
    }

    if (options.bpm <= 0.0) {
        return ValidationResult::fail("BPM must be greater than zero.");
    }

    return ValidationResult::pass();
}

ValidationResult validateTrackContextForDrums(const HermesTrackContext& context)
{
    if (context.trackType != core::TrackType::audio) {
        return ValidationResult::fail("Drums extraction requires an audio track.");
    }

    if (context.audioSourcePath.empty()) {
        return ValidationResult::fail("Selected audio track has no assigned WAV source file.");
    }

    std::error_code ec;
    const auto sourcePath = std::filesystem::path(context.audioSourcePath);
    if (!std::filesystem::exists(sourcePath, ec) || !std::filesystem::is_regular_file(sourcePath, ec)) {
        return ValidationResult::fail("Selected WAV source file does not exist.");
    }

    return ValidationResult::pass();
}

ValidationResult validateTrackContextForAudioMidiPair(const HermesAudioMidiPairContext& context)
{
    const auto audioValidation = validateTrackContextForDrums(context.audioTrack);
    if (!audioValidation.ok) {
        return audioValidation;
    }

    if (context.midiTrack.trackType != core::TrackType::midi) {
        return ValidationResult::fail("Selected MIDI source is not a MIDI track.");
    }

    if (context.midiNotes.empty()) {
        return ValidationResult::fail("Selected MIDI source track does not contain note data.");
    }

    if (context.midiSourceMetadata.ticksPerQuarterNote <= 0) {
        return ValidationResult::fail("MIDI source metadata is missing a valid ticks-per-quarter-note value.");
    }

    return ValidationResult::pass();
}

}  // namespace dawhermes::hermes
