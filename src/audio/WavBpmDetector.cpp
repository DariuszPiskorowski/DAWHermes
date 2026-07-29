#include "audio/WavBpmDetector.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <memory>
#include <numeric>
#include <utility>
#include <vector>

#include <juce_audio_formats/juce_audio_formats.h>

#include "core/Utf8Path.h"

namespace dawhermes::audio {

namespace {

constexpr double kMinimumBpm = 60.0;
constexpr double kMaximumBpm = 200.0;
constexpr double kEnvelopeRate = 200.0;
constexpr int kReadBlockFrames = 4096;
constexpr std::size_t kMaximumBeatGridPhaseCandidates = 64;

juce::File juceFileFromPath(const std::filesystem::path& sourcePath)
{
    const auto utf8 = core::pathToUtf8(sourcePath);
    return juce::File(juce::String::fromUTF8(
        utf8.data(),
        static_cast<int>(utf8.size())));
}

std::optional<WavFileFingerprint> fingerprintFile(
    const std::filesystem::path& sourcePath)
{
    std::error_code error;
    if (!std::filesystem::exists(sourcePath, error)
        || !std::filesystem::is_regular_file(sourcePath, error)) {
        return std::nullopt;
    }

    WavFileFingerprint fingerprint;
    fingerprint.sourcePath = core::pathToUtf8(
        std::filesystem::absolute(sourcePath, error));
    if (error) {
        fingerprint.sourcePath = core::pathToUtf8(sourcePath);
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

struct OnsetEvent {
    double position { 0.0 };
    double strength { 0.0 };
};

struct BeatGridMetrics {
    double phase { 0.0 };
    double coverage { 0.0 };
    double weightedCoverage { 0.0 };
    double eventCoverage { 0.0 };
    double intermediateCoverage { 0.0 };
    double intermediateStrength { 0.0 };
    double phaseScore { 0.0 };
};

struct TempoCandidate {
    double lag { 0.0 };
    double bpm { 0.0 };
    double correlation { 0.0 };
    BeatGridMetrics grid;
    double score { 0.0 };
};

double refinedAutocorrelationLag(
    int lag,
    int minimumLag,
    int maximumLag,
    const std::vector<double>& correlations)
{
    auto refinedLag = static_cast<double>(lag);
    if (lag <= minimumLag || lag >= maximumLag) {
        return refinedLag;
    }

    const auto left = correlations[static_cast<std::size_t>(lag - 1)];
    const auto center = correlations[static_cast<std::size_t>(lag)];
    const auto right = correlations[static_cast<std::size_t>(lag + 1)];
    const auto denominator = left - (2.0 * center) + right;
    if (std::abs(denominator) > 1.0e-9) {
        refinedLag += std::clamp(
            0.5 * (left - right) / denominator,
            -0.75,
            0.75);
    }
    return refinedLag;
}

std::vector<OnsetEvent> findOnsetEvents(
    const std::vector<double>& onset,
    double maximumOnset,
    double meanOnset,
    double standardDeviation)
{
    const auto eventThreshold = std::max(
        maximumOnset * 0.06,
        meanOnset + (standardDeviation * 0.35));
    std::vector<OnsetEvent> events;
    for (std::size_t index = 1; index + 1 < onset.size(); ++index) {
        const auto value = onset[index];
        if (value < eventThreshold
            || value < onset[index - 1]
            || value < onset[index + 1]) {
            continue;
        }

        const auto normalizedStrength = std::clamp(
            value / maximumOnset,
            0.0,
            1.0);
        if (!events.empty()
            && static_cast<double>(index) - events.back().position <= 3.0) {
            if (normalizedStrength > events.back().strength) {
                events.back() = OnsetEvent {
                    static_cast<double>(index),
                    normalizedStrength,
                };
            }
            continue;
        }
        events.push_back(OnsetEvent {
            static_cast<double>(index),
            normalizedStrength,
        });
    }
    return events;
}

double normalizedOnsetSupport(
    const std::vector<double>& onset,
    double maximumOnset,
    double position,
    int radius)
{
    const auto center = static_cast<long long>(std::llround(position));
    double support = 0.0;
    for (int offset = -radius; offset <= radius; ++offset) {
        const auto index = center + offset;
        if (index < 0
            || index >= static_cast<long long>(onset.size())) {
            continue;
        }
        support = std::max(
            support,
            onset[static_cast<std::size_t>(index)] / maximumOnset);
    }
    return std::clamp(support, 0.0, 1.0);
}

BeatGridMetrics evaluateBeatGrid(
    const std::vector<double>& onset,
    const std::vector<OnsetEvent>& events,
    double maximumOnset,
    double period)
{
    BeatGridMetrics best;
    if (events.empty() || period <= 1.0) {
        return best;
    }

    const auto tolerance = std::clamp(
        static_cast<int>(std::llround(period * 0.025)),
        2,
        5);
    double totalEventStrength = 0.0;
    for (const auto& event : events) {
        totalEventStrength += event.strength;
    }

    std::vector<std::size_t> phaseEventIndices(events.size());
    std::iota(
        phaseEventIndices.begin(),
        phaseEventIndices.end(),
        static_cast<std::size_t>(0));
    std::stable_sort(
        phaseEventIndices.begin(),
        phaseEventIndices.end(),
        [&events](std::size_t left, std::size_t right) {
            return events[left].strength > events[right].strength;
        });
    if (phaseEventIndices.size() > kMaximumBeatGridPhaseCandidates) {
        phaseEventIndices.resize(kMaximumBeatGridPhaseCandidates);
    }

    for (const auto phaseEventIndex : phaseEventIndices) {
        const auto& phaseEvent = events[phaseEventIndex];
        auto phase = std::fmod(phaseEvent.position, period);
        if (phase < 0.0) {
            phase += period;
        }

        std::size_t gridPointCount = 0;
        std::size_t supportedGridPointCount = 0;
        double weightedGridSupport = 0.0;
        for (auto position = phase;
             position < static_cast<double>(onset.size());
             position += period) {
            const auto support = normalizedOnsetSupport(
                onset,
                maximumOnset,
                position,
                tolerance);
            ++gridPointCount;
            if (support >= 0.08) {
                ++supportedGridPointCount;
            }
            weightedGridSupport += std::min(1.0, support / 0.35);
        }
        if (gridPointCount == 0) {
            continue;
        }

        double alignedEventStrength = 0.0;
        for (const auto& event : events) {
            const auto nearestGridIndex = std::round(
                (event.position - phase) / period);
            const auto nearestGridPosition =
                phase + (nearestGridIndex * period);
            if (std::abs(event.position - nearestGridPosition)
                <= static_cast<double>(tolerance)) {
                alignedEventStrength += event.strength;
            }
        }

        BeatGridMetrics metrics;
        metrics.phase = phase;
        metrics.coverage =
            static_cast<double>(supportedGridPointCount)
            / static_cast<double>(gridPointCount);
        metrics.weightedCoverage =
            weightedGridSupport / static_cast<double>(gridPointCount);
        metrics.eventCoverage = totalEventStrength > 0.0
            ? alignedEventStrength / totalEventStrength
            : 0.0;
        metrics.phaseScore =
            (metrics.coverage * 0.40)
            + (metrics.weightedCoverage * 0.25)
            + (metrics.eventCoverage * 0.35);

        std::size_t intermediateCount = 0;
        std::size_t supportedIntermediateCount = 0;
        double intermediateStrength = 0.0;
        for (auto position = phase + (period * 0.5);
             position < static_cast<double>(onset.size());
             position += period) {
            const auto support = normalizedOnsetSupport(
                onset,
                maximumOnset,
                position,
                tolerance);
            ++intermediateCount;
            if (support >= 0.08) {
                ++supportedIntermediateCount;
            }
            intermediateStrength += std::min(1.0, support / 0.35);
        }
        if (intermediateCount > 0) {
            metrics.intermediateCoverage =
                static_cast<double>(supportedIntermediateCount)
                / static_cast<double>(intermediateCount);
            metrics.intermediateStrength =
                intermediateStrength / static_cast<double>(intermediateCount);
        }

        if (metrics.phaseScore > best.phaseScore) {
            best = metrics;
        }
    }
    return best;
}

bool areOctaveRelated(double slowerBpm, double fasterBpm)
{
    if (slowerBpm <= 0.0 || fasterBpm <= 0.0) {
        return false;
    }
    return std::abs((fasterBpm / (slowerBpm * 2.0)) - 1.0) <= 0.035;
}

}  // namespace

bool WavBpmEstimate::isConfident() const noexcept
{
    return bpm.has_value() && confidence >= kMinimumWavBpmConfidence;
}

std::optional<WavFileFingerprint> fingerprintWavFile(
    const std::filesystem::path& sourcePath)
{
    return fingerprintFile(sourcePath);
}

WavBpmCandidateDecision evaluateWavBpmCandidates(
    const std::vector<double>& onsetEnvelope,
    double envelopeRate)
{
    WavBpmCandidateDecision decision;
    if (onsetEnvelope.size() < 4
        || !std::isfinite(envelopeRate)
        || envelopeRate <= 0.0) {
        return decision;
    }

    const auto maximumOnset = *std::max_element(
        onsetEnvelope.begin(),
        onsetEnvelope.end());
    if (maximumOnset <= std::numeric_limits<double>::epsilon()) {
        return decision;
    }
    const auto meanOnset = std::accumulate(
        onsetEnvelope.begin(),
        onsetEnvelope.end(),
        0.0) / static_cast<double>(onsetEnvelope.size());
    double variance = 0.0;
    for (const auto value : onsetEnvelope) {
        const auto delta = value - meanOnset;
        variance += delta * delta;
    }
    variance /= static_cast<double>(onsetEnvelope.size());
    const auto standardDeviation = std::sqrt(variance);
    const auto events = findOnsetEvents(
        onsetEnvelope,
        maximumOnset,
        meanOnset,
        standardDeviation);
    decision.onsetEventCount = events.size();
    if (events.size() < 4) {
        return decision;
    }

    const auto minimumLag = std::max(
        1,
        static_cast<int>(std::floor(
            (60.0 * envelopeRate) / kMaximumBpm)));
    const auto maximumLag = std::min(
        static_cast<int>(std::ceil(
            (60.0 * envelopeRate) / kMinimumBpm)),
        static_cast<int>(onsetEnvelope.size() / 2));
    if (maximumLag <= minimumLag) {
        return decision;
    }

    std::vector<double> correlations(
        static_cast<std::size_t>(maximumLag + 1),
        0.0);
    int strongestLag = 0;
    for (int lag = minimumLag; lag <= maximumLag; ++lag) {
        const auto correlation = normalizedAutocorrelation(
            onsetEnvelope,
            lag);
        correlations[static_cast<std::size_t>(lag)] = correlation;
        if (strongestLag == 0
            || correlation
                > correlations[static_cast<std::size_t>(strongestLag)]) {
            strongestLag = lag;
        }
    }
    if (strongestLag == 0) {
        return decision;
    }

    const auto strongestRefinedLag = refinedAutocorrelationLag(
        strongestLag,
        minimumLag,
        maximumLag,
        correlations);
    decision.strongestAutocorrelation =
        correlations[static_cast<std::size_t>(strongestLag)];
    decision.strongestAutocorrelationBpm =
        (60.0 * envelopeRate) / strongestRefinedLag;

    std::vector<int> peakLags;
    for (int lag = minimumLag; lag <= maximumLag; ++lag) {
        const auto correlation = correlations[static_cast<std::size_t>(lag)];
        const auto left = lag > minimumLag
            ? correlations[static_cast<std::size_t>(lag - 1)]
            : correlation;
        const auto right = lag < maximumLag
            ? correlations[static_cast<std::size_t>(lag + 1)]
            : correlation;
        if (correlation >= 0.18
            && correlation >= left
            && correlation >= right) {
            peakLags.push_back(lag);
        }
    }
    if (std::find(peakLags.begin(), peakLags.end(), strongestLag)
        == peakLags.end()) {
        peakLags.push_back(strongestLag);
    }
    std::sort(
        peakLags.begin(),
        peakLags.end(),
        [&correlations](int left, int right) {
            return correlations[static_cast<std::size_t>(left)]
                > correlations[static_cast<std::size_t>(right)];
        });
    if (peakLags.size() > 16) {
        peakLags.resize(16);
    }

    std::vector<TempoCandidate> candidates;
    const auto addCandidateNearLag = [&](
                                         double requestedLag,
                                         int searchRadius) {
        if (requestedLag < static_cast<double>(minimumLag)
            || requestedLag > static_cast<double>(maximumLag)) {
            return;
        }
        const auto roundedLag = static_cast<int>(std::llround(requestedLag));
        const auto searchStart = std::max(
            minimumLag,
            roundedLag - searchRadius);
        const auto searchEnd = std::min(
            maximumLag,
            roundedLag + searchRadius);
        auto selectedLag = searchStart;
        for (int lag = searchStart + 1; lag <= searchEnd; ++lag) {
            if (correlations[static_cast<std::size_t>(lag)]
                > correlations[static_cast<std::size_t>(selectedLag)]) {
                selectedLag = lag;
            }
        }

        const auto refinedLag = refinedAutocorrelationLag(
            selectedLag,
            minimumLag,
            maximumLag,
            correlations);
        const auto bpm = (60.0 * envelopeRate) / refinedLag;
        if (!std::isfinite(bpm)
            || bpm < kMinimumBpm
            || bpm > kMaximumBpm
            || std::any_of(
                candidates.begin(),
                candidates.end(),
                [bpm](const TempoCandidate& candidate) {
                    return std::abs(candidate.bpm - bpm) < 0.75;
                })) {
            return;
        }

        TempoCandidate candidate;
        candidate.lag = refinedLag;
        candidate.bpm = bpm;
        candidate.correlation =
            correlations[static_cast<std::size_t>(selectedLag)];
        candidate.grid = evaluateBeatGrid(
            onsetEnvelope,
            events,
            maximumOnset,
            refinedLag);
        candidate.score =
            (candidate.correlation * 0.50)
            + (candidate.grid.phaseScore * 0.50);
        candidates.push_back(std::move(candidate));
    };

    for (const auto lag : peakLags) {
        addCandidateNearLag(static_cast<double>(lag), 1);
        addCandidateNearLag(static_cast<double>(lag) * 0.5, 3);
        addCandidateNearLag(static_cast<double>(lag) * 2.0, 3);
    }
    if (candidates.empty()) {
        return decision;
    }

    for (auto& slower : candidates) {
        for (auto& faster : candidates) {
            if (!areOctaveRelated(slower.bpm, faster.bpm)) {
                continue;
            }

            const auto intermediateEvidence = std::clamp(
                (slower.grid.intermediateCoverage * 0.65)
                    + (slower.grid.intermediateStrength * 0.35),
                0.0,
                1.0);
            const auto fastGridEvidence = std::clamp(
                (faster.grid.coverage * 0.45)
                    + (faster.grid.weightedCoverage * 0.20)
                    + (faster.grid.eventCoverage * 0.35),
                0.0,
                1.0);
            const auto octaveEvidence =
                intermediateEvidence * fastGridEvidence;
            if (octaveEvidence >= 0.28) {
                const auto evidenceScale = std::clamp(
                    (octaveEvidence - 0.28) / 0.55,
                    0.0,
                    1.0);
                faster.score += 0.26 * evidenceScale;
                slower.score -= 0.10 * evidenceScale;
            } else if (octaveEvidence < 0.18) {
                faster.score -= 0.08 * std::clamp(
                    (0.18 - octaveEvidence) / 0.18,
                    0.0,
                    1.0);
            }
        }
    }

    const auto best = std::max_element(
        candidates.begin(),
        candidates.end(),
        [](const TempoCandidate& left, const TempoCandidate& right) {
            return left.score < right.score;
        });
    if (best == candidates.end()
        || best->correlation < 0.25
        || best->score < 0.45) {
        return decision;
    }

    double strongestOctaveAlternativeScore = -1.0;
    double strongestOtherScore = -1.0;
    for (const auto& candidate : candidates) {
        if (&candidate == &(*best)) {
            continue;
        }
        strongestOtherScore = std::max(
            strongestOtherScore,
            candidate.score);
        const auto slower = std::min(candidate.bpm, best->bpm);
        const auto faster = std::max(candidate.bpm, best->bpm);
        if (areOctaveRelated(slower, faster)) {
            strongestOctaveAlternativeScore = std::max(
                strongestOctaveAlternativeScore,
                candidate.score);
        }
    }

    const auto comparisonScore = strongestOctaveAlternativeScore >= 0.0
        ? strongestOctaveAlternativeScore
        : strongestOtherScore;
    const auto scoreMargin = comparisonScore >= 0.0
        ? std::max(0.0, best->score - comparisonScore)
        : best->score;
    const auto correlationQuality = std::clamp(
        (best->correlation - 0.22) / 0.70,
        0.0,
        1.0);
    const auto rhythmicCoverage = std::clamp(
        (best->grid.coverage * 0.45)
            + (best->grid.weightedCoverage * 0.20)
            + (best->grid.eventCoverage * 0.35),
        0.0,
        1.0);
    const auto analyzedSeconds =
        static_cast<double>(onsetEnvelope.size()) / envelopeRate;
    const auto durationQuality = std::clamp(
        (analyzedSeconds - 4.0) / 8.0,
        0.0,
        1.0);
    const auto eventQuality = std::clamp(
        static_cast<double>(events.size()) / 14.0,
        0.0,
        1.0);
    const auto marginQuality = comparisonScore >= 0.0
        ? std::clamp((scoreMargin - 0.025) / 0.18, 0.0, 1.0)
        : 1.0;
    const auto evidenceQuality =
        (correlationQuality * 0.35)
        + (rhythmicCoverage * 0.30)
        + (durationQuality * 0.15)
        + (eventQuality * 0.20);

    decision.bpm = best->bpm;
    decision.rhythmicCoverage = rhythmicCoverage;
    decision.octaveScoreMargin = scoreMargin;
    decision.confidence = std::clamp(
        evidenceQuality * (0.25 + (0.75 * marginQuality)),
        0.0,
        1.0);
    return decision;
}

WavBpmEstimate analyzeWavBpm(
    const std::filesystem::path& sourcePath,
    double maximumAnalysisSeconds)
{
    WavBpmEstimate estimate;
    const auto sourceFile = juceFileFromPath(sourcePath);
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

    const auto decision = evaluateWavBpmCandidates(onset, kEnvelopeRate);
    estimate.confidence = decision.confidence;
    if (decision.bpm.has_value()
        && estimate.confidence >= kMinimumWavBpmConfidence) {
        estimate.bpm = decision.bpm;
    }
    return estimate;
}

std::optional<WavBpmEstimate> WavBpmCache::find(
    const WavFileFingerprint& fingerprint)
{
    const auto it = entries_.find(fingerprint.sourcePath);
    if (it == entries_.end() || !(it->second.fingerprint == fingerprint)) {
        return std::nullopt;
    }
    it->second.lastUse = nextUse_++;
    return it->second.estimate;
}

void WavBpmCache::store(
    const WavFileFingerprint& fingerprint,
    WavBpmEstimate estimate)
{
    if (const auto existing = entries_.find(fingerprint.sourcePath);
        existing != entries_.end()) {
        existing->second = Entry {
            fingerprint,
            std::move(estimate),
            nextUse_++,
        };
        return;
    }

    if (entries_.size() >= kMaximumWavBpmCacheEntries) {
        const auto oldest = std::min_element(
            entries_.begin(),
            entries_.end(),
            [](const auto& left, const auto& right) {
                return left.second.lastUse < right.second.lastUse;
            });
        if (oldest != entries_.end()) {
            entries_.erase(oldest);
        }
    }

    entries_.emplace(
        fingerprint.sourcePath,
        Entry {
            fingerprint,
            std::move(estimate),
            nextUse_++,
        });
}

std::size_t WavBpmCache::size() const noexcept
{
    return entries_.size();
}

WavBpmAnalysisService::WavBpmAnalysisService()
    : WavBpmAnalysisService(
        [](const std::filesystem::path& sourcePath) {
            return fingerprintWavFile(sourcePath);
        },
        [](const std::filesystem::path& sourcePath) {
            return analyzeWavBpm(sourcePath);
        })
{
}

WavBpmAnalysisService::WavBpmAnalysisService(
    WavBpmFingerprintFunction fingerprintFunction,
    WavBpmAnalyzeFunction analyzeFunction)
    : fingerprintFunction_(std::move(fingerprintFunction)),
      analyzeFunction_(std::move(analyzeFunction)),
      worker_([this](std::stop_token stopToken) { workerLoop(stopToken); })
{
}

WavBpmAnalysisService::~WavBpmAnalysisService()
{
    stop();
}

std::uint64_t WavBpmAnalysisService::request(
    const std::filesystem::path& sourcePath)
{
    const auto fingerprint = fingerprintFunction_(sourcePath);
    std::scoped_lock lock(mutex_);
    const auto generation = nextGeneration_++;
    latestRequestedGeneration_ = generation;
    completed_.reset();

    if (!fingerprint.has_value()) {
        pending_.reset();
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
        pending_.reset();
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
    std::scoped_lock lock(mutex_);
    pending_.reset();
    analyzing_ = false;
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

        auto estimate = analyzeFunction_(
            core::pathFromUtf8(request.fingerprint.sourcePath));
        const auto currentFingerprint = fingerprintFunction_(
            core::pathFromUtf8(request.fingerprint.sourcePath));
        std::scoped_lock lock(mutex_);
        if (!currentFingerprint.has_value()) {
            if (request.generation == latestRequestedGeneration_) {
                completed_ = WavBpmAnalysisResult {
                    request.fingerprint,
                    WavBpmEstimate {},
                    request.generation,
                    false,
                };
            }
            analyzing_ = pending_.has_value();
            continue;
        }
        if (currentFingerprint.has_value()
            && !(currentFingerprint.value() == request.fingerprint)) {
            const auto mayRetry = request.generation == latestRequestedGeneration_
                && !pending_.has_value();
            if (mayRetry) {
                pending_ = PendingRequest {
                    currentFingerprint.value(),
                    request.generation,
                };
                condition_.notify_one();
            }
            analyzing_ = pending_.has_value();
            continue;
        }
        cache_.store(request.fingerprint, estimate);
        if (request.generation == latestRequestedGeneration_) {
            completed_ = WavBpmAnalysisResult {
                request.fingerprint,
                std::move(estimate),
                request.generation,
                false,
            };
        }
        analyzing_ = pending_.has_value();
    }
}

}  // namespace dawhermes::audio
