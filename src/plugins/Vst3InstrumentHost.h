#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "plugins/Vst3PluginCatalog.h"

namespace dawhermes::plugins {

constexpr int kMaximumHostedInstrumentLatencySamples = 131072;

struct PluginTransportPosition {
    std::int64_t samplePosition { 0 };
    double seconds { 0.0 };
    double ppqPosition { 0.0 };
    double bpm { 120.0 };
    int timeSignatureNumerator { 4 };
    int timeSignatureDenominator { 4 };
    bool playing { false };
    bool looping { false };
    double loopStartPpq { 0.0 };
    double loopEndPpq { 0.0 };
};

struct InstrumentRuntimeFailure {
    std::uint64_t trackId { 0 };
    juce::String instrumentName;
    juce::String reason;
};

juce::AudioPlayHead::PositionInfo makePluginPositionInfo(
    const PluginTransportPosition& position);

class Vst3InstrumentHost final {
public:
    using AssignmentCallback = std::function<void(
        bool,
        juce::String,
        juce::PluginDescription)>;

    explicit Vst3InstrumentHost(
        juce::PropertiesFile* settings);
    ~Vst3InstrumentHost();

    Vst3InstrumentHost(const Vst3InstrumentHost&) = delete;
    Vst3InstrumentHost& operator=(const Vst3InstrumentHost&) = delete;

    Vst3PluginCatalog& catalog() noexcept;
    const Vst3PluginCatalog& catalog() const noexcept;

    void prepareDevice(double sampleRate, int maximumBlockSize);
    void releaseDevice();
    double sampleRate() const noexcept;
    int maximumBlockSize() const noexcept;

    void assignAsync(
        std::uint64_t trackId,
        const juce::PluginDescription& description,
        AssignmentCallback callback);
    void useInternalSynth(std::uint64_t trackId);
    bool hasInstrument(std::uint64_t trackId) const noexcept;
    juce::String instrumentName(std::uint64_t trackId) const;
    bool openEditor(std::uint64_t trackId, juce::String& error);
    void closeEditor(std::uint64_t trackId);
    void closeAllEditors();

    void beginAudioBlock(
        int numSamples,
        const PluginTransportPosition& position) noexcept;
    void resetAllFromAudioThread(int sampleOffset = 0) noexcept;
    bool addMidiEventFromAudioThread(
        std::uint64_t trackId,
        bool noteOn,
        int channel,
        int pitch,
        float amplitude,
        int sampleOffset) noexcept;
    void processAudioBlock(
        float* const* outputs,
        int numOutputChannels,
        int numSamples,
        float masterGain,
        bool addToOutput = true) noexcept;

    int maximumLatencySamples() const noexcept;
    bool refreshLatencyLayoutIfNeeded();
    std::size_t activeInstanceCount() const noexcept;
    void collectRetiredRuntimes();
    std::vector<InstrumentRuntimeFailure>
    takeRuntimeFailures();

    bool installPreparedInstanceForTesting(
        std::uint64_t trackId,
        std::unique_ptr<juce::AudioPluginInstance> instance,
        const juce::PluginDescription& description,
        juce::String& error);

private:
    class HostedPlayHead;
    class Runtime;
    class EditorWindow;

    struct Registry {
        std::vector<std::shared_ptr<Runtime>> runtimes;
        int maximumLatencySamples { 0 };
    };

    std::shared_ptr<Runtime> runtimeForTrack(
        const std::shared_ptr<const Registry>& registry,
        std::uint64_t trackId) const noexcept;
    bool publishInstance(
        std::uint64_t trackId,
        std::unique_ptr<juce::AudioPluginInstance> instance,
        const juce::PluginDescription& description,
        juce::String& error);
    void publishWithoutTrack(std::uint64_t trackId);
    void publishRegistry(
        std::shared_ptr<const Registry> registry);

    Vst3PluginCatalog catalog_;
    std::atomic<double> sampleRate_ { 44100.0 };
    std::atomic<int> maximumBlockSize_ { 512 };
    std::atomic<int> requestedMaximumLatency_ { 0 };
    std::atomic<std::shared_ptr<const Registry>> requestedRegistry_;
    std::shared_ptr<const Registry> activeRegistry_;
    std::uint64_t requestedGeneration_ { 0 };
    std::uint64_t activeGeneration_ { 0 };
    mutable std::mutex messageMutex_;
    std::vector<std::shared_ptr<const Registry>> retainedRegistries_;
    std::map<std::uint64_t, std::unique_ptr<EditorWindow>> editorWindows_;
    std::vector<InstrumentRuntimeFailure> runtimeFailures_;
    std::uint64_t nextAssignmentRequestId_ { 0 };
    std::map<std::uint64_t, std::uint64_t> pendingAssignmentRequests_;
    std::shared_ptr<std::atomic<bool>> lifetimeState_ {
        std::make_shared<std::atomic<bool>>(true)
    };
};

}  // namespace dawhermes::plugins
