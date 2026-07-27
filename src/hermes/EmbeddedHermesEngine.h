#pragma once

#include <memory>
#include <mutex>
#include <string>

#include "hermes/IHermesEngine.h"

namespace dawhermes::hermes {

class EmbeddedHermesEngine final : public IHermesEngine {
public:
    EmbeddedHermesEngine();
    ~EmbeddedHermesEngine() override;

    HermesOperationResult drumsMakeMidiFromWav(
        const HermesTrackContext& context,
        const HermesDrumsOptions& options) override;

    HermesOperationResult bassMakeOrRepairMidiFromWav(
        const HermesAudioMidiPairContext& context,
        const HermesBassOptions& options) override;

    HermesOperationResult synchronizeMidiWithWav(
        const HermesAudioMidiPairContext& context,
        const HermesSyncOptions& options) override;

    HermesOperationResult setOrFixBpm(
        const HermesTrackContext& context,
        const HermesBpmOptions& options) override;

private:
    struct PythonState;

    bool ensureRuntimeReady();
    std::string resolveMidiCleanerRoot() const;
    HermesOperationResult unavailableResult(const std::string& commandName) const;

    mutable std::mutex mutex_;
    bool runtimeReady_ { false };
    std::string unavailableReason_;
    std::unique_ptr<PythonState> pythonState_;
};

}  // namespace dawhermes::hermes
