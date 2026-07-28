#include "audio/TransportModel.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#include "audio/SelectionPlaybackModel.h"

namespace dawhermes::audio {

namespace {

double sanitizedDuration(double durationSeconds) noexcept
{
    return std::isfinite(durationSeconds)
        ? std::max(0.0, durationSeconds)
        : 0.0;
}

std::string formatUnderOneHourCurrent(double seconds)
{
    const auto totalSeconds = static_cast<long long>(
        std::floor(std::max(0.0, seconds) + 1.0e-6));
    const auto minutes = totalSeconds / 60;
    const auto wholeSeconds = totalSeconds % 60;

    std::ostringstream text;
    text << std::setfill('0')
         << std::setw(2) << minutes << ":"
         << std::setw(2) << wholeSeconds;
    return text.str();
}

std::string formatUnderOneHourTotal(double seconds)
{
    const auto roundedSeconds = static_cast<long long>(
        std::ceil(std::max(0.0, seconds) - 1.0e-9));
    const auto minutes = roundedSeconds / 60;
    const auto remainingSeconds = roundedSeconds % 60;

    std::ostringstream text;
    text << std::setfill('0')
         << std::setw(2) << minutes << ":"
         << std::setw(2) << remainingSeconds;
    return text.str();
}

std::string formatOneHourOrLonger(double seconds)
{
    const auto roundedSeconds = static_cast<long long>(
        std::floor(std::max(0.0, seconds) + 1.0e-6));
    const auto hours = roundedSeconds / 3600;
    const auto minutes = (roundedSeconds / 60) % 60;
    const auto remainingSeconds = roundedSeconds % 60;

    std::ostringstream text;
    text << hours << ":"
         << std::setfill('0') << std::setw(2) << minutes << ":"
         << std::setw(2) << remainingSeconds;
    return text.str();
}

}  // namespace

void SharedTransportState::setPreviewDuration(double durationSeconds) noexcept
{
    if (mode() != TransportMode::stopped) {
        return;
    }

    const auto duration = sanitizedDuration(durationSeconds);
    snapshot_.store(nullptr, std::memory_order_release);
    totalSeconds_.store(duration, std::memory_order_release);
    currentSeconds_.store(
        clampTransportSeconds(currentSeconds(), duration),
        std::memory_order_release);
}

void SharedTransportState::prepare(
    std::shared_ptr<const SelectionPlaybackSnapshot> snapshot,
    double startSeconds) noexcept
{
    const auto playable = snapshot != nullptr && snapshot->isPlayable();
    const auto duration = playable ? sanitizedDuration(snapshot->durationSeconds) : 0.0;
    snapshot_.store(playable ? std::move(snapshot) : nullptr, std::memory_order_release);
    totalSeconds_.store(duration, std::memory_order_release);
    currentSeconds_.store(
        clampTransportSeconds(startSeconds, duration),
        std::memory_order_release);
    mode_.store(TransportMode::stopped, std::memory_order_release);
}

void SharedTransportState::play() noexcept
{
    if (hasPreparedPlayback() && totalSeconds() > 0.0) {
        mode_.store(TransportMode::playing, std::memory_order_release);
    }
}

void SharedTransportState::pause() noexcept
{
    if (mode() == TransportMode::playing) {
        mode_.store(TransportMode::paused, std::memory_order_release);
    }
}

void SharedTransportState::stop() noexcept
{
    mode_.store(TransportMode::stopped, std::memory_order_release);
    currentSeconds_.store(0.0, std::memory_order_release);
}

void SharedTransportState::panic() noexcept
{
    snapshot_.store(nullptr, std::memory_order_release);
    totalSeconds_.store(0.0, std::memory_order_release);
    stop();
}

void SharedTransportState::complete() noexcept
{
    currentSeconds_.store(totalSeconds(), std::memory_order_release);
    mode_.store(TransportMode::stopped, std::memory_order_release);
}

void SharedTransportState::seek(double targetSeconds) noexcept
{
    currentSeconds_.store(
        clampTransportSeconds(targetSeconds, totalSeconds()),
        std::memory_order_release);
}

void SharedTransportState::updatePositionFromAudio(double currentSeconds) noexcept
{
    if (!isPlaying()) {
        return;
    }
    currentSeconds_.store(
        clampTransportSeconds(currentSeconds, totalSeconds()),
        std::memory_order_release);
}

TransportMode SharedTransportState::mode() const noexcept
{
    return mode_.load(std::memory_order_acquire);
}

bool SharedTransportState::isPlaying() const noexcept
{
    return mode() == TransportMode::playing;
}

bool SharedTransportState::isPaused() const noexcept
{
    return mode() == TransportMode::paused;
}

double SharedTransportState::currentSeconds() const noexcept
{
    return currentSeconds_.load(std::memory_order_acquire);
}

double SharedTransportState::totalSeconds() const noexcept
{
    return totalSeconds_.load(std::memory_order_acquire);
}

bool SharedTransportState::hasPreparedPlayback() const noexcept
{
    return snapshot_.load(std::memory_order_acquire) != nullptr;
}

std::shared_ptr<const SelectionPlaybackSnapshot> SharedTransportState::snapshot() const noexcept
{
    return snapshot_.load(std::memory_order_acquire);
}

double clampTransportSeconds(double seconds, double totalSeconds) noexcept
{
    const auto total = sanitizedDuration(totalSeconds);
    const auto current = std::isfinite(seconds) ? seconds : 0.0;
    return std::clamp(current, 0.0, total);
}

double seekTransportSeconds(
    double currentSeconds,
    double deltaSeconds,
    double totalSeconds) noexcept
{
    const auto delta = std::isfinite(deltaSeconds) ? deltaSeconds : 0.0;
    return clampTransportSeconds(currentSeconds + delta, totalSeconds);
}

bool shouldAutomaticallyFollowPlayhead(TransportMode mode) noexcept
{
    return mode == TransportMode::playing;
}

std::string formatTransportCounter(
    double currentSeconds,
    double totalSeconds)
{
    const auto total = sanitizedDuration(totalSeconds);
    if (total <= 0.0) {
        return "00:00 / 00:00";
    }

    const auto current = clampTransportSeconds(currentSeconds, total);
    if (total >= 3600.0) {
        return formatOneHourOrLonger(current)
            + " / "
            + formatOneHourOrLonger(total);
    }

    return formatUnderOneHourCurrent(current)
        + " / "
        + formatUnderOneHourTotal(total);
}

}  // namespace dawhermes::audio
