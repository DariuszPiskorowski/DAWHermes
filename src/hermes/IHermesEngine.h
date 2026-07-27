#pragma once

#include "hermes/HermesTypes.h"

namespace dawhermes::hermes {

class IHermesEngine {
public:
    virtual ~IHermesEngine() = default;

    virtual HermesOperationResult drumsMakeMidiFromWav(
        const HermesTrackContext& context,
        const HermesDrumsOptions& options) = 0;

    virtual HermesOperationResult bassMakeOrRepairMidiFromWav(
        const HermesAudioMidiPairContext& context,
        const HermesBassOptions& options) = 0;

    virtual HermesOperationResult synchronizeMidiWithWav(
        const HermesAudioMidiPairContext& context,
        const HermesSyncOptions& options) = 0;

    virtual HermesOperationResult setOrFixBpm(
        const HermesTrackContext& context,
        const HermesBpmOptions& options) = 0;
};

}  // namespace dawhermes::hermes
