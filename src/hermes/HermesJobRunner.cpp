#include "hermes/HermesJobRunner.h"

#include <chrono>
#include <exception>
#include <utility>

#include "hermes/EmbeddedHermesEngine.h"

namespace dawhermes::hermes {

namespace {

std::unique_ptr<IHermesEngine> createDefaultEmbeddedEngine()
{
    return std::make_unique<EmbeddedHermesEngine>();
}

HermesOperationResult makeUnavailableResult(const std::string& message)
{
    return HermesOperationResult::unavailable(message);
}

}  // namespace

HermesJobRunner::HermesJobRunner()
    : HermesJobRunner(createDefaultEmbeddedEngine)
{
}

HermesJobRunner::HermesJobRunner(EngineFactory engineFactory)
    : engineFactory_(std::move(engineFactory))
{
    workerThread_ = std::thread([this]() { workerMain(); });
}

HermesJobRunner::~HermesJobRunner()
{
    stop();
}

bool HermesJobRunner::submit(const HermesJobRequest& request, CompletionCallback completion, std::string& error)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (stopping_) {
        error = "Hermes worker is shutting down.";
        return false;
    }

    if (workerBusy_ || pendingJob_.has_value()) {
        error = "Another Hermes operation is already running.";
        return false;
    }

    pendingJob_ = PendingJob { request, std::move(completion) };
    conditionVariable_.notify_one();
    return true;
}

bool HermesJobRunner::isBusy() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return workerBusy_ || pendingJob_.has_value();
}

void HermesJobRunner::stop()
{
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) {
            // Already stopping.
        } else {
            stopping_ = true;
            pendingJob_.reset();
        }
    }

    conditionVariable_.notify_one();

    if (workerThread_.joinable()) {
        workerThread_.join();
    }
}

void HermesJobRunner::workerMain()
{
    std::unique_ptr<IHermesEngine> engine;
    std::string engineInitError;

    try {
        engine = engineFactory_();
        if (engine == nullptr) {
            engineInitError = "Hermes engine factory returned null.";
        }
    } catch (const std::exception& ex) {
        engineInitError = std::string("Failed to initialize embedded Hermes engine: ") + ex.what();
    } catch (...) {
        engineInitError = "Failed to initialize embedded Hermes engine: unknown exception.";
    }

    while (true) {
        PendingJob job;

        {
            std::unique_lock<std::mutex> lock(mutex_);
            conditionVariable_.wait(lock, [this]() { return stopping_ || pendingJob_.has_value(); });

            if (stopping_ && !pendingJob_.has_value()) {
                break;
            }

            if (!pendingJob_.has_value()) {
                continue;
            }

            job = std::move(pendingJob_.value());
            pendingJob_.reset();
            workerBusy_ = true;
        }

        HermesJobResult result;
        result.kind = job.request.kind;

        const auto start = std::chrono::steady_clock::now();
        if (!engineInitError.empty() || engine == nullptr) {
            result.operationResult = makeUnavailableResult(engineInitError.empty()
                                                               ? "Embedded Hermes engine is unavailable."
                                                               : engineInitError);
        } else {
            try {
                switch (job.request.kind) {
                case HermesOperationKind::drumsExtraction:
                    result.operationResult = engine->drumsMakeMidiFromWav(job.request.trackContext, job.request.drumsOptions);
                    break;
                case HermesOperationKind::bassRepair:
                    result.operationResult = engine->bassMakeOrRepairMidiFromWav(job.request.pairContext, job.request.bassOptions);
                    break;
                case HermesOperationKind::midiWavSynchronization:
                    result.operationResult = engine->synchronizeMidiWithWav(job.request.pairContext, job.request.syncOptions);
                    break;
                default:
                    result.operationResult = makeUnavailableResult("Unknown Hermes operation requested.");
                    break;
                }
            } catch (const std::exception& ex) {
                result.operationResult = makeUnavailableResult(
                    std::string("Hermes operation failed: ") + ex.what());
            } catch (...) {
                result.operationResult = makeUnavailableResult("Hermes operation failed: unknown exception.");
            }
        }

        const auto end = std::chrono::steady_clock::now();
        result.durationMs = std::chrono::duration<double, std::milli>(end - start).count();
        result.operationResult.durationMs = result.durationMs;

        if (job.completion) {
            job.completion(std::move(result));
        }

        {
            std::lock_guard<std::mutex> lock(mutex_);
            workerBusy_ = false;
        }
    }
}

}  // namespace dawhermes::hermes
