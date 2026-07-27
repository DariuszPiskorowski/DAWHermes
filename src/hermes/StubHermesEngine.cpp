#include "hermes/StubHermesEngine.h"

#include <utility>

namespace dawhermes::hermes {

namespace {
constexpr auto kNotIntegratedMessage = "Hermes processing is not integrated for this command.";
}

HermesOperationResult HermesOperationResult::success(
    std::string message,
    HermesResultLayout resultLayout,
    std::vector<HermesGeneratedMidiTrack> generatedMidiTracks,
    double bpmUsed,
    std::vector<std::string> warnings,
    HermesOperationKind operationKind)
{
    HermesOperationResult result;
    result.status = HermesOperationStatus::success;
    result.message = std::move(message);
    result.operationKind = operationKind;
    result.resultLayout = resultLayout;
    result.generatedMidiTracks = std::move(generatedMidiTracks);
    result.bpmUsed = bpmUsed;
    result.warnings = std::move(warnings);
    return result;
}

HermesOperationResult HermesOperationResult::notImplemented(std::string message)
{
    if (message.empty()) {
        message = kNotIntegratedMessage;
    }

    return HermesOperationResult { HermesOperationStatus::notImplemented, std::move(message) };
}

HermesOperationResult HermesOperationResult::invalidInput(std::string message)
{
    return HermesOperationResult { HermesOperationStatus::invalidInput, std::move(message) };
}

HermesOperationResult HermesOperationResult::unavailable(std::string message)
{
    return HermesOperationResult { HermesOperationStatus::unavailable, std::move(message) };
}

HermesOperationResult StubHermesEngine::drumsMakeMidiFromWav(
    const HermesTrackContext&,
    const HermesDrumsOptions&)
{
    return HermesOperationResult::notImplemented();
}

HermesOperationResult StubHermesEngine::bassMakeOrRepairMidiFromWav(
    const HermesAudioMidiPairContext&,
    const HermesBassOptions&)
{
    return HermesOperationResult::notImplemented();
}

HermesOperationResult StubHermesEngine::synchronizeMidiWithWav(
    const HermesAudioMidiPairContext&,
    const HermesSyncOptions&)
{
    return HermesOperationResult::notImplemented();
}

HermesOperationResult StubHermesEngine::setOrFixBpm(
    const HermesTrackContext&,
    const HermesBpmOptions&)
{
    return HermesOperationResult::notImplemented();
}

}  // namespace dawhermes::hermes
