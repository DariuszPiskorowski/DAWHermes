#pragma once

#include "hermes/IHermesEngine.h"

namespace dawhermes::hermes {

class StubHermesEngine final : public IHermesEngine {
public:
    HermesOperationResult drumsMakeMidiFromWav(
        const HermesTrackContext& context,
        const HermesDrumsOptions& options) override;

    HermesOperationResult bassMakeOrRepairMidiFromWav(
        const HermesTrackContext& context,
        const HermesBassOptions& options) override;

    HermesOperationResult synchronizeMidiWithWav(
        const HermesTrackContext& context,
        const HermesSyncOptions& options) override;

    HermesOperationResult setOrFixBpm(
        const HermesTrackContext& context,
        const HermesBpmOptions& options) override;
};

}  // namespace dawhermes::hermes
