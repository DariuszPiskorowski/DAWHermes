#pragma once

#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <condition_variable>

#include "hermes/IHermesEngine.h"

namespace dawhermes::hermes {

struct HermesJobRequest {
    HermesOperationKind kind { HermesOperationKind::drumsExtraction };
    HermesTrackContext trackContext;
    HermesAudioMidiPairContext pairContext;
    HermesDrumsOptions drumsOptions;
    HermesBassOptions bassOptions;
    HermesSyncOptions syncOptions;
};

struct HermesJobResult {
    HermesOperationKind kind { HermesOperationKind::drumsExtraction };
    HermesOperationResult operationResult;
    double durationMs { 0.0 };
};

class HermesJobRunner final {
public:
    using CompletionCallback = std::function<void(HermesJobResult)>;
    using EngineFactory = std::function<std::unique_ptr<IHermesEngine>()>;

    HermesJobRunner();
    explicit HermesJobRunner(EngineFactory engineFactory);
    ~HermesJobRunner();

    HermesJobRunner(const HermesJobRunner&) = delete;
    HermesJobRunner& operator=(const HermesJobRunner&) = delete;

    bool submit(const HermesJobRequest& request, CompletionCallback completion, std::string& error);
    bool isBusy() const;
    void stop();

private:
    struct PendingJob {
        HermesJobRequest request;
        CompletionCallback completion;
    };

    void workerMain();

    mutable std::mutex mutex_;
    std::condition_variable conditionVariable_;
    bool stopping_ { false };
    bool workerBusy_ { false };
    std::optional<PendingJob> pendingJob_;
    EngineFactory engineFactory_;
    std::thread workerThread_;
};

}  // namespace dawhermes::hermes
