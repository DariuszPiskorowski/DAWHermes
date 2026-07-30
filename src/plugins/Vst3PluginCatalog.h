#pragma once

#include <atomic>
#include <mutex>
#include <optional>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_core/juce_core.h>

namespace dawhermes::plugins {

struct Vst3ScanStatus {
    bool running { false };
    bool cancelRequested { false };
    bool completed { false };
    bool catalogReplaced { false };
    float progress { 0.0f };
    int instrumentCount { 0 };
    int failedCount { 0 };
    int recoverySkippedCount { 0 };
    int staleRecoveryCount { 0 };
    juce::String currentCandidate;
    juce::String summary;
};

struct Vst3ScanRecoveryPlan {
    juce::StringArray candidatesToScan;
    int recoveredFailureCount { 0 };
    int staleRecoveryCount { 0 };
};

juce::String stableVst3Identifier(
    const juce::PluginDescription& description);

std::vector<juce::PluginDescription> filterAndSortVst3Instruments(
    const juce::Array<juce::PluginDescription>& descriptions);

Vst3ScanRecoveryPlan prepareVst3ScanRecoveryPlan(
    const juce::StringArray& candidates,
    const juce::StringArray& deadMansPedalEntries,
    bool retryRecoveredFailures);

class Vst3PluginCatalog final : private juce::Thread {
public:
    explicit Vst3PluginCatalog(juce::PropertiesFile* settings);
    ~Vst3PluginCatalog() override;

    Vst3PluginCatalog(const Vst3PluginCatalog&) = delete;
    Vst3PluginCatalog& operator=(const Vst3PluginCatalog&) = delete;

    std::vector<juce::PluginDescription> instruments() const;
    std::optional<juce::PluginDescription> findByIdentifier(
        const juce::String& identifier) const;
    juce::AudioPluginFormatManager& formatManager() noexcept;
    juce::FileSearchPath defaultSearchPath() const;
    juce::File catalogFile() const;
    juce::File deadMansPedalFile() const;

    bool startScan(bool rescanExisting);
    void requestCancel() noexcept;
    Vst3ScanStatus scanStatus() const;

private:
    void run() override;
    void load();
    bool save(const juce::KnownPluginList& list);
    juce::File settingsSibling(const juce::String& name) const;

    juce::PropertiesFile* settings_ { nullptr };
    juce::AudioPluginFormatManager formatManager_;
    mutable std::mutex mutex_;
    std::vector<juce::PluginDescription> instruments_;
    Vst3ScanStatus status_;
    bool rescanExisting_ { false };
    std::atomic<bool> cancelRequested_ { false };
};

}  // namespace dawhermes::plugins
