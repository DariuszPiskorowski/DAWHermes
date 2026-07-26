#include "hermes/StubHermesEngine.h"

#include <utility>

namespace dawhermes::hermes {

namespace {
constexpr auto kNotIntegratedMessage = "Hermes processing is not integrated in Milestone 0.";
}

HermesOperationResult HermesOperationResult::success(std::string message)
{
    return HermesOperationResult { HermesOperationStatus::success, std::move(message) };
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
    const HermesTrackContext&,
    const HermesBassOptions&)
{
    return HermesOperationResult::notImplemented();
}

HermesOperationResult StubHermesEngine::synchronizeMidiWithWav(
    const HermesTrackContext&,
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
