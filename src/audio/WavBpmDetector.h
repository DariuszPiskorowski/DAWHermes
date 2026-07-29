#pragma once

#include <condition_variable>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <map>
#include <mutex>
#include <optional>
#include <stop_token>
#include <string>
#include <thread>
#include <vector>

namespace dawhermes::audio {

constexpr std::size_t kMaximumWavBpmCacheEntries = 128;
constexpr double kMinimumWavBpmConfidence = 0.45;

struct WavFileFingerprint {
    std::string sourcePath;
    std::uintmax_t fileSize { 0 };
    std::filesystem::file_time_type lastWriteTime {};

    bool operator==(const WavFileFingerprint& other) const = default;
};

struct WavBpmEstimate {
    std::optional<double> bpm;
    double confidence { 0.0 };
    double analyzedSeconds { 0.0 };

    bool isConfident() const noexcept;
};

struct WavBpmAnalysisResult {
    WavFileFingerprint fingerprint;
    WavBpmEstimate estimate;
    std::uint64_t requestGeneration { 0 };
    bool reusedCache { false };
};

struct WavBpmCandidateDecision {
    std::optional<double> bpm;
    double confidence { 0.0 };
    double strongestAutocorrelationBpm { 0.0 };
    double strongestAutocorrelation { 0.0 };
    double rhythmicCoverage { 0.0 };
    double octaveScoreMargin { 0.0 };
    std::size_t onsetEventCount { 0 };
};

std::optional<WavFileFingerprint> fingerprintWavFile(
    const std::filesystem::path& sourcePath);

WavBpmCandidateDecision evaluateWavBpmCandidates(
    const std::vector<double>& onsetEnvelope,
    double envelopeRate = 200.0);

WavBpmEstimate analyzeWavBpm(
    const std::filesystem::path& sourcePath,
    double maximumAnalysisSeconds = 90.0);

using WavBpmFingerprintFunction = std::function<
    std::optional<WavFileFingerprint>(const std::filesystem::path&)>;
using WavBpmAnalyzeFunction = std::function<
    WavBpmEstimate(const std::filesystem::path&)>;

class WavBpmCache {
public:
    std::optional<WavBpmEstimate> find(
        const WavFileFingerprint& fingerprint);
    void store(
        const WavFileFingerprint& fingerprint,
        WavBpmEstimate estimate);
    std::size_t size() const noexcept;

private:
    struct Entry {
        WavFileFingerprint fingerprint;
        WavBpmEstimate estimate;
        std::uint64_t lastUse { 0 };
    };

    std::map<std::string, Entry> entries_;
    std::uint64_t nextUse_ { 1 };
};

class WavBpmAnalysisService {
public:
    WavBpmAnalysisService();
    WavBpmAnalysisService(
        WavBpmFingerprintFunction fingerprintFunction,
        WavBpmAnalyzeFunction analyzeFunction);
    ~WavBpmAnalysisService();

    WavBpmAnalysisService(const WavBpmAnalysisService&) = delete;
    WavBpmAnalysisService& operator=(const WavBpmAnalysisService&) = delete;

    std::uint64_t request(const std::filesystem::path& sourcePath);
    std::optional<WavBpmAnalysisResult> pollCompleted();
    bool isAnalyzing() const;
    void stop();

private:
    struct PendingRequest {
        WavFileFingerprint fingerprint;
        std::uint64_t generation { 0 };
    };

    void workerLoop(std::stop_token stopToken);

    mutable std::mutex mutex_;
    std::condition_variable_any condition_;
    WavBpmFingerprintFunction fingerprintFunction_;
    WavBpmAnalyzeFunction analyzeFunction_;
    WavBpmCache cache_;
    std::optional<PendingRequest> pending_;
    std::optional<WavBpmAnalysisResult> completed_;
    std::uint64_t nextGeneration_ { 1 };
    std::uint64_t latestRequestedGeneration_ { 0 };
    bool analyzing_ { false };
    std::jthread worker_;
};

}  // namespace dawhermes::audio
