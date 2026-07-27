#pragma once

#include <string>

#include "hermes/HermesTypes.h"

namespace dawhermes::hermes {

struct ValidationResult {
    bool ok { true };
    std::string message;

    static ValidationResult pass() { return ValidationResult { true, {} }; }
    static ValidationResult fail(std::string message) { return ValidationResult { false, std::move(message) }; }
};

bool isValidResultLayout(HermesResultLayout value);
bool isValidDrumsProfile(HermesDrumsProfile value);
bool isValidDetectionMode(HermesDetectionMode value);
bool isValidTargetMapping(HermesTargetMapping value);

ValidationResult validateDrumsOptions(const HermesDrumsOptions& options);
ValidationResult validateBassOptions(const HermesBassOptions& options);
ValidationResult validateSyncOptions(const HermesSyncOptions& options);
ValidationResult validateBpmOptions(const HermesBpmOptions& options);
ValidationResult validateTrackContextForDrums(const HermesTrackContext& context);
ValidationResult validateTrackContextForAudioMidiPair(const HermesAudioMidiPairContext& context);

}  // namespace dawhermes::hermes
