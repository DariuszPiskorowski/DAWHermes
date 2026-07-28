#include "audio/WavBpmDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

namespace dawhermes::audio {

namespace {

constexpr double kMinimumBpm = 60.0;
constexpr double kMaximumBpm = 200.0;
constexpr double kEnvelopeRate = 200.0;
constexpr double kMinimumConfidence = 0.45;
constexpr int kReadBlockFrames = 4096;

std::optional<WavFileFingerprint> fingerprintFile(
    const std::filesystem::path& sourcePath)
{
    std::error_code error;
    if (!std::filesystem::exists(sourcePath, error)
        || !std::filesystem::is_regular_file(sourcePath, error)) {
        return std::nullopt;
    }

    WavFileFingerprint fingerprint;
    fingerprint.sourcePath = std::filesystem::absolute(sourcePath, error).string();
    if (error) {
        fingerprint.sourcePath = sourcePath.string();
        error.clear();
    }
    fingerprint.fileSize = std::filesystem::file_size(sourcePath, error);
    if (error) {
        return std::nullopt;
    }
    fingerprint.lastWriteTime = std::filesystem::last_write_time(sourcePath, error);
    if (error) {
        return std::nullopt;
    }
    return fingerprint;
}

std::vector<double> buildOnsetEnvelope(
    juce::AudioFormatReader& reader,
    std::uint64_t maximumFrames,
    double& analyzedSeconds)
{
    const auto channelCount = static_cast<int>(std::clamp<unsigned int>(
        reader.numChannels,
        1U,
        2U));
    const auto hopFrames = std::max(
        1,
        static_cast<int>(std::llround(reader.sampleRate / kEnvelopeRate)));
    juce::AudioBuffer<float> block(channelCount, kReadBlockFrames);
    std::vector<double> energy;
    energy.reserve(static_cast<std::size_t>(
        std::ceil(static_cast<double>(maximumFrames) / static_cast<double>(hopFrames))));

    double hopEnergy = 0.0;
    int hopSampleCount = 0;
    std::uint64_t frameOffset = 0;
    while (frameOffset < maximumFrames) {
        const auto remaining = maximumFrames - frameOffset;
        const auto framesToRead = static_cast<int>(std::min<std::uint64_t>(
            remaining,
            kReadBlockFrames));
        block.clear();
        if (!reader.read(
                &block,
                0,
                framesToRead,
                static_cast<juce::int64>(frameOffset),
                true,
                channelCount > 1)) {
            break;
        }

        for (int frame = 0; frame < framesToRead; ++frame) {
            double sampleEnergy = 0.0;
            for (int channel = 0; channel < channelCount; ++channel) {
                const auto sample = static_cast<double>(block.getSample(channel, frame));
                sampleEnergy += sample * sample;
            }
            hopEnergy += sampleEnergy / static_cast<double>(channelCount);
            ++hopSampleCount;

            if (hopSampleCount >= hopFrames) {
                energy.push_back(std::sqrt(hopEnergy / static_cast<double>(hopSampleCount)));
                hopEnergy = 0.0;
                hopSampleCount = 0;
            }
        }
        frameOffset += static_cast<std::uint64_t>(framesToRead);
    }

    if (hopSampleCount > 0) {
        energy.push_back(std::sqrt(hopEnergy / static_cast<double>(hopSampleCount)));
    }
    analyzedSeconds = static_cast<double>(frameOffset) / reader.sampleRate;

    if (energy.size() < 4) {
        return {};
    }

    std::vector<double> onset(energy.size(), 0.0);
    double previousSmoothed = energy.front();
    for (std::size_t index = 1; index < energy.size(); ++index) {
        const auto difference = energy[index] - previousSmoothed;
        onset[index] = std::max(0.0, difference);
        previousSmoothed = (previousSmoothed * 0.75) + (energy[index] * 0.25);
    }
    return onset;
}

double normalizedAutocorrelation(
    const std::vector<double>& onset,
    int lag)
{
    double numerator = 0.0;
    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    for (std::size_t index = static_cast<std::size_t>(lag);
         index < onset.size();
         ++index) {
        const auto left = onset[index];
        const auto right = onset[index - static_cast<std::size_t>(lag)];
        numerator += left * right;
        leftEnergy += left * left;
        rightEnergy += right * right;
    }

    const auto denominator = std::sqrt(leftEnergy * rightEnergy);
    return denominator <= std::numeric_limits<double>::epsilon()
        ? 0.0
        : numerator / denominator;
}

}  // namespace

bool WavBpmEstimate::isConfident() const noexcept
{
    return bpm.has_value() && confidence >= kMinimumConfidence;
}

std::optional<WavFileFingerprint> fingerprintWavFile(
    const std::filesystem::path& sourcePath)
{
    return fingerprintFile(sourcePath);
}

WavBpmEstimate analyzeWavBpm(
    const std::filesystem::path& sourcePath,
    double maximumAnalysisSeconds)
{
    WavBpmEstimate estimate;
    const juce::File sourceFile(sourcePath.string());
    if (!sourceFile.existsAsFile()) {
        return estimate;
    }

    juce::WavAudioFormat wavFormat;
    auto inputStream = sourceFile.createInputStream();
    if (inputStream == nullptr) {
        return estimate;
    }
    std::unique_ptr<juce::AudioFormatReader> reader(
        wavFormat.createReaderFor(inputStream.release(), true));
    if (reader == nullptr
        || reader->sampleRate <= 0.0
        || reader->lengthInSamples <= 0
        || reader->numChannels < 1
        || reader->numChannels > 2) {
        return estimate;
    }

    const auto boundedSeconds = std::clamp(
        std::isfinite(maximumAnalysisSeconds) ? maximumAnalysisSeconds : 90.0,
        5.0,
        180.0);
    const auto maximumFrames = static_cast<std::uint64_t>(std::min<double>(
        static_cast<double>(reader->lengthInSamples),
        reader->sampleRate * boundedSeconds));
    auto onset = buildOnsetEnvelope(*reader, maximumFrames, estimate.analyzedSeconds);
    if (onset.size() < static_cast<std::size_t>(kEnvelopeRate * 4.0)) {
        return estimate;
    }

    const auto maximumOnset = *std::max_element(onset.begin(), onset.end());
    const auto meanOnset = std::accumulate(onset.begin(), onset.end(), 0.0)
        / static_cast<double>(onset.size());
    double variance = 0.0;
    for (const auto value : onset) {
        const auto delta = value - meanOnset;
        variance += delta * delta;
    }
    variance /= static_cast<double>(onset.size());
    const auto standardDeviation = std::sqrt(variance);
    const auto peakThreshold = meanOnset + (standardDeviation * 2.0);
    const auto significantPeakCount = static_cast<std::size_t>(std::count_if(
        onset.begin(),
        onset.end(),
        [peakThreshold](double value) { return value >= peakThreshold; }));

    if (maximumOnset < 1.0e-5
        || significantPeakCount < 6
        || maximumOnset < meanOnset * 2.5) {
        return estimate;
    }

    const auto minimumLag = std::max(
        1,
        static_cast<int>(std::floor((60.0 * kEnvelopeRate) / kMaximumBpm)));
    const auto maximumLag = static_cast<int>(
        std::ceil((60.0 * kEnvelopeRate) / kMinimumBpm));
    std::vector<double> correlations(
        static_cast<std::size_t>(maximumLag + 1),
        0.0);

    int bestLag = 0;
    double bestScore = 0.0;
    double bestCorrelation = 0.0;
    for (int lag = minimumLag; lag <= maximumLag; ++lag) {
        const auto correlation = normalizedAutocorrelation(onset, lag);
        correlations[static_cast<std::size_t>(lag)] = correlation;
        const auto bpm = (60.0 * kEnvelopeRate) / static_cast<double>(lag);
        const auto practicalTempoBias = bpm >= 80.0 && bpm <= 160.0 ? 1.04 : 1.0;
        const auto score = correlation * practicalTempoBias;
        if (score > bestScore) {
            bestScore = score;
            bestCorrelation = correlation;
            bestLag = lag;
        }
    }

    if (bestLag == 0 || bestCorrelation < 0.32) {
        return estimate;
    }

    double refinedLag = static_cast<double>(bestLag);
    if (bestLag > minimumLag && bestLag < maximumLag) {
        const auto left = correlations[static_cast<std::size_t>(bestLag - 1)];
        const auto center = correlations[static_cast<std::size_t>(bestLag)];
        const auto right = correlations[static_cast<std::size_t>(bestLag + 1)];
        const auto denominator = left - (2.0 * center) + right;
        if (std::abs(denominator) > 1.0e-9) {
            refinedLag += 0.5 * (left - right) / denominator;
        }
    }

    const auto bpm = (60.0 * kEnvelopeRate) / refinedLag;
    if (!std::isfinite(bpm) || bpm < kMinimumBpm || bpm > kMaximumBpm) {
        return estimate;
    }

    const auto peakCoverage = std::clamp(
        static_cast<double>(significantPeakCount) / 16.0,
        0.0,
        1.0);
    estimate.confidence = std::clamp(
        ((bestCorrelation - 0.25) / 0.65) * (0.65 + (0.35 * peakCoverage)),
        0.0,
        1.0);
    if (estimate.confidence >= kMinimumConfidence) {
        estimate.bpm = bpm;
    }
    return estimate;
}

std::optional<WavBpmEstimate> WavBpmCache::find(
    const WavFileFingerprint& fingerprint) const
{
    const auto it = entries_.find(fingerprint.sourcePath);
    if (it == entries_.end() || !(it->second.fingerprint == fingerprint)) {
        return std::nullopt;
    }
    return it->second.estimate;
}

void WavBpmCache::store(
    const WavFileFingerprint& fingerprint,
    WavBpmEstimate estimate)
{
    entries_[fingerprint.sourcePath] = Entry {
        fingerprint,
        std::move(estimate),
    };
}

std::size_t WavBpmCache::size() const noexcept
{
    return entries_.size();
}

WavBpmAnalysisService::WavBpmAnalysisService()
    : worker_([this](std::stop_token stopToken) { workerLoop(stopToken); })
{
}

WavBpmAnalysisService::~WavBpmAnalysisService()
{
    stop();
}

std::uint64_t WavBpmAnalysisService::request(
    const std::filesystem::path& sourcePath)
{
    const auto fingerprint = fingerprintWavFile(sourcePath);
    std::scoped_lock lock(mutex_);
    const auto generation = nextGeneration_++;
    completed_.reset();

    if (!fingerprint.has_value()) {
        analyzing_ = false;
        return generation;
    }

    if (const auto cached = cache_.find(fingerprint.value()); cached.has_value()) {
        completed_ = WavBpmAnalysisResult {
            fingerprint.value(),
            cached.value(),
            generation,
            true,
        };
        analyzing_ = false;
        return generation;
    }

    pending_ = PendingRequest { fingerprint.value(), generation };
    analyzing_ = true;
    condition_.notify_one();
    return generation;
}

std::optional<WavBpmAnalysisResult> WavBpmAnalysisService::pollCompleted()
{
    std::scoped_lock lock(mutex_);
    auto result = std::move(completed_);
    completed_.reset();
    return result;
}

bool WavBpmAnalysisService::isAnalyzing() const
{
    std::scoped_lock lock(mutex_);
    return analyzing_;
}

void WavBpmAnalysisService::stop()
{
    worker_.request_stop();
    condition_.notify_all();
    if (worker_.joinable()) {
        worker_.join();
    }
}

void WavBpmAnalysisService::workerLoop(std::stop_token stopToken)
{
    while (!stopToken.stop_requested()) {
        PendingRequest request;
        {
            std::unique_lock lock(mutex_);
            condition_.wait(lock, stopToken, [this]() { return pending_.has_value(); });
            if (stopToken.stop_requested()) {
                return;
            }
            request = pending_.value();
            pending_.reset();
        }

        auto estimate = analyzeWavBpm(request.fingerprint.sourcePath);
        const auto currentFingerprint = fingerprintWavFile(
            request.fingerprint.sourcePath);
        std::scoped_lock lock(mutex_);
        if (!currentFingerprint.has_value()) {
            completed_ = WavBpmAnalysisResult {
                request.fingerprint,
                WavBpmEstimate {},
                request.generation,
                false,
            };
            analyzing_ = pending_.has_value();
            continue;
        }
        if (currentFingerprint.has_value()
            && !(currentFingerprint.value() == request.fingerprint)) {
            pending_ = PendingRequest {
                currentFingerprint.value(),
                request.generation,
            };
            analyzing_ = true;
            condition_.notify_one();
            continue;
        }
        cache_.store(request.fingerprint, estimate);
        completed_ = WavBpmAnalysisResult {
            request.fingerprint,
            std::move(estimate),
            request.generation,
            false,
        };
        analyzing_ = pending_.has_value();
    }
}

}  // namespace dawhermes::audio
