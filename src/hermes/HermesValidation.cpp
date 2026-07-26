#include "hermes/HermesValidation.h"

#include <cmath>

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

}  // namespace dawhermes::hermes
