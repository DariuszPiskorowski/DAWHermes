#include "plugins/Vst3InstrumentHost.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace dawhermes::plugins {

namespace {

constexpr std::size_t kMaximumMidiEventsPerPluginBlock =
    8192;

int midiVelocity(float amplitude)
{
    return std::clamp(
        static_cast<int>(std::lround(
            std::clamp(amplitude, 0.0f, 1.0f) * 127.0f)),
        1,
        127);
}

}  // namespace

juce::AudioPlayHead::PositionInfo makePluginPositionInfo(
    const PluginTransportPosition& position)
{
    juce::AudioPlayHead::PositionInfo result;
    result.setTimeInSamples(position.samplePosition);
    result.setTimeInSeconds(position.seconds);
    result.setPpqPosition(position.ppqPosition);
    result.setBpm(std::max(1.0, position.bpm));
    juce::AudioPlayHead::TimeSignature timeSignature;
    timeSignature.numerator =
        std::max(1, position.timeSignatureNumerator);
    timeSignature.denominator =
        std::max(1, position.timeSignatureDenominator);
    result.setTimeSignature(timeSignature);
    result.setIsPlaying(position.playing);
    result.setIsRecording(false);
    result.setIsLooping(position.looping);
    if (position.looping
        && position.loopEndPpq > position.loopStartPpq) {
        juce::AudioPlayHead::LoopPoints loopPoints;
        loopPoints.ppqStart = position.loopStartPpq;
        loopPoints.ppqEnd = position.loopEndPpq;
        result.setLoopPoints(loopPoints);
    }
    return result;
}

class Vst3InstrumentHost::HostedPlayHead final
    : public juce::AudioPlayHead {
public:
    void setPosition(PositionInfo position) noexcept
    {
        position_ = std::move(position);
    }

    juce::Optional<PositionInfo> getPosition() const override
    {
        return position_;
    }

private:
    PositionInfo position_;
};

class Vst3InstrumentHost::Runtime final {
public:
    Runtime(
        std::uint64_t trackId,
        std::unique_ptr<juce::AudioPluginInstance> instance,
        juce::PluginDescription description,
        double sampleRate,
        int maximumBlockSize)
        : trackId_(trackId),
          instance_(std::move(instance)),
          description_(std::move(description)),
          outputChannels_(std::clamp(
              description_.numOutputChannels,
              1,
              2))
    {
        instance_->setPlayHead(&playHead_);
        prepare(sampleRate, maximumBlockSize);
    }

    void prepare(double sampleRate, int maximumBlockSize)
    {
        const auto blockSize = std::max(1, maximumBlockSize);
        if (prepared_) {
            instance_->releaseResources();
            prepared_ = false;
        }
        instance_->setPlayConfigDetails(
            0,
            outputChannels_,
            std::max(1.0, sampleRate),
            blockSize);
        scratch_.setSize(
            outputChannels_,
            blockSize,
            false,
            true,
            false);
        midi_.ensureSize(
            kMaximumMidiEventsPerPluginBlock * 16U);
        for (auto& delay : delay_) {
            delay.assign(
                static_cast<std::size_t>(
                    kMaximumHostedInstrumentLatencySamples
                    + blockSize + 1),
                0.0f);
        }
        delayWrite_ = 0;

        instance_->prepareToPlay(
            std::max(1.0, sampleRate),
            blockSize);
        prepared_ = true;
        const auto latency = instance_->getLatencySamples();
        if (latency > kMaximumHostedInstrumentLatencySamples) {
            instance_->releaseResources();
            prepared_ = false;
            throw std::runtime_error(
                "Hosted instrument latency exceeds the bounded limit.");
        }
        latencySamples_.store(
            std::max(0, latency),
            std::memory_order_release);
    }

    ~Runtime()
    {
        instance_->setPlayHead(nullptr);
        if (prepared_) {
            instance_->releaseResources();
        }
    }

    void beginBlock(
        int numSamples,
        const juce::AudioPlayHead::PositionInfo& position) noexcept
    {
        const auto samples =
            std::clamp(numSamples, 0, scratch_.getNumSamples());
        scratch_.clear(0, samples);
        midi_.clear();
        midiEventCount_ = 0;
        playHead_.setPosition(position);
    }

    void reset(int sampleOffset) noexcept
    {
        for (int channel = 1; channel <= 16; ++channel) {
            if (midiEventCount_ + 2
                > kMaximumMidiEventsPerPluginBlock) {
                break;
            }
            midi_.addEvent(
                juce::MidiMessage::allNotesOff(channel),
                sampleOffset);
            midi_.addEvent(
                juce::MidiMessage::allSoundOff(channel),
                sampleOffset);
            midiEventCount_ += 2;
        }
    }

    void addMidi(
        bool noteOn,
        int channel,
        int pitch,
        float amplitude,
        int sampleOffset) noexcept
    {
        if (midiEventCount_
            >= kMaximumMidiEventsPerPluginBlock) {
            return;
        }
        const auto safeChannel = std::clamp(channel, 1, 16);
        const auto safePitch = std::clamp(pitch, 0, 127);
        midi_.addEvent(
            noteOn
                ? juce::MidiMessage::noteOn(
                      safeChannel,
                      safePitch,
                      static_cast<juce::uint8>(
                          midiVelocity(amplitude)))
                : juce::MidiMessage::noteOff(
                      safeChannel,
                      safePitch),
            sampleOffset);
        ++midiEventCount_;
    }

    void process(
        float* const* outputs,
        int numOutputChannels,
        int numSamples,
        int maximumLatency,
        float masterGain,
        bool addToOutput) noexcept
    {
        const auto samples =
            std::clamp(numSamples, 0, scratch_.getNumSamples());
        instance_->processBlock(scratch_, midi_);
        if (!addToOutput) {
            return;
        }
        const auto compensation = std::clamp(
            maximumLatency
                - latencySamples_.load(
                    std::memory_order_relaxed),
            0,
            kMaximumHostedInstrumentLatencySamples);
        const auto channels =
            std::min(2, numOutputChannels);
        const auto size = delay_[0].size();
        for (int sample = 0; sample < samples; ++sample) {
            const auto read =
                (delayWrite_ + size
                 - static_cast<std::size_t>(compensation))
                % size;
            for (int channel = 0; channel < channels; ++channel) {
                auto& delay =
                    delay_[static_cast<std::size_t>(channel)];
                delay[delayWrite_] =
                    scratch_.getSample(
                        std::min(
                            channel,
                            outputChannels_ - 1),
                        sample);
                if (outputs[channel] != nullptr) {
                    outputs[channel][sample] +=
                        delay[read] * masterGain;
                }
            }
            delayWrite_ = (delayWrite_ + 1U) % size;
        }
    }

    std::uint64_t trackId() const noexcept { return trackId_; }
    int latencySamples() const noexcept
    {
        return latencySamples_.load(
            std::memory_order_acquire);
    }
    bool refreshLatency() noexcept
    {
        const auto next = std::clamp(
            instance_->getLatencySamples(),
            0,
            kMaximumHostedInstrumentLatencySamples);
        return next
            != latencySamples_.exchange(
                next,
                std::memory_order_acq_rel);
    }
    juce::String name() const { return description_.name; }
    juce::AudioPluginInstance& instance() noexcept { return *instance_; }

private:
    std::uint64_t trackId_ { 0 };
    std::unique_ptr<juce::AudioPluginInstance> instance_;
    juce::PluginDescription description_;
    HostedPlayHead playHead_;
    juce::AudioBuffer<float> scratch_;
    juce::MidiBuffer midi_;
    std::size_t midiEventCount_ { 0 };
    std::array<std::vector<float>, 2> delay_;
    std::size_t delayWrite_ { 0 };
    std::atomic<int> latencySamples_ { 0 };
    int outputChannels_ { 2 };
    bool prepared_ { false };
};

class Vst3InstrumentHost::EditorWindow final
    : public juce::DocumentWindow {
public:
    EditorWindow(
        std::shared_ptr<Runtime> runtime)
        : juce::DocumentWindow(
              runtime->name(),
              juce::Colour(0xff252b33),
              juce::DocumentWindow::closeButton,
              true),
          runtime_(std::move(runtime))
    {
        std::unique_ptr<juce::AudioProcessorEditor> editor(
            runtime_->instance().createEditorAndMakeActive());
        if (editor == nullptr) {
            editor = std::make_unique<
                juce::GenericAudioProcessorEditor>(
                runtime_->instance());
        }
        setUsingNativeTitleBar(true);
        setContentOwned(editor.release(), true);
        centreWithSize(getWidth(), getHeight());
        setResizable(true, false);
        setVisible(true);
    }

    void closeButtonPressed() override
    {
        setVisible(false);
    }

private:
    std::shared_ptr<Runtime> runtime_;
};

Vst3InstrumentHost::Vst3InstrumentHost(
    juce::PropertiesFile* settings)
    : catalog_(settings)
{
    auto empty = std::make_shared<const Registry>();
    requestedRegistry_.store(empty, std::memory_order_release);
    activeRegistry_ = empty;
    retainedRegistries_.push_back(empty);
}

Vst3InstrumentHost::~Vst3InstrumentHost()
{
    lifetimeState_->store(false, std::memory_order_release);
    closeAllEditors();
    releaseDevice();
}

Vst3PluginCatalog& Vst3InstrumentHost::catalog() noexcept
{
    return catalog_;
}

const Vst3PluginCatalog&
Vst3InstrumentHost::catalog() const noexcept
{
    return catalog_;
}

void Vst3InstrumentHost::prepareDevice(
    double sampleRate,
    int maximumBlockSize)
{
    sampleRate_.store(
        std::max(1.0, sampleRate),
        std::memory_order_release);
    maximumBlockSize_.store(
        std::max(1, maximumBlockSize),
        std::memory_order_release);
    const auto registry =
        requestedRegistry_.load(std::memory_order_acquire);
    if (registry == nullptr) {
        return;
    }

    auto next = std::make_shared<Registry>();
    std::vector<InstrumentRuntimeFailure> failures;
    for (const auto& runtime : registry->runtimes) {
        try {
            runtime->prepare(sampleRate, maximumBlockSize);
            next->runtimes.push_back(runtime);
            next->maximumLatencySamples = std::max(
                next->maximumLatencySamples,
                runtime->latencySamples());
        } catch (...) {
            failures.push_back({
                runtime->trackId(),
                runtime->name(),
                "The instrument could not be prepared for the current audio device."
            });
        }
    }
    const auto layoutChanged =
        next->maximumLatencySamples
        != registry->maximumLatencySamples;
    if (failures.empty() && !layoutChanged) {
        return;
    }

    for (const auto& failure : failures) {
        closeEditor(failure.trackId);
    }
    publishRegistry(std::move(next));
    if (!failures.empty()) {
        const std::scoped_lock lock(messageMutex_);
        runtimeFailures_.insert(
            runtimeFailures_.end(),
            failures.begin(),
            failures.end());
    }
}

void Vst3InstrumentHost::releaseDevice()
{
    publishRegistry(std::make_shared<const Registry>());
}

double Vst3InstrumentHost::sampleRate() const noexcept
{
    return sampleRate_.load(std::memory_order_acquire);
}

int Vst3InstrumentHost::maximumBlockSize() const noexcept
{
    return maximumBlockSize_.load(std::memory_order_acquire);
}

void Vst3InstrumentHost::assignAsync(
    std::uint64_t trackId,
    const juce::PluginDescription& description,
    AssignmentCallback callback)
{
    std::uint64_t requestId = 0;
    {
        const std::scoped_lock lock(messageMutex_);
        requestId = ++nextAssignmentRequestId_;
        pendingAssignmentRequests_[trackId] = requestId;
    }
    const std::weak_ptr<std::atomic<bool>> lifetime =
        lifetimeState_;
    catalog_.formatManager().createPluginInstanceAsync(
        description,
        sampleRate(),
        maximumBlockSize(),
        [this,
         lifetime,
         trackId,
         requestId,
         description,
         callback = std::move(callback)](
            std::unique_ptr<juce::AudioPluginInstance> instance,
            const juce::String& creationError) mutable {
            const auto alive = lifetime.lock();
            if (alive == nullptr
                || !alive->load(std::memory_order_acquire)) {
                return;
            }
            {
                const std::scoped_lock lock(messageMutex_);
                const auto pending =
                    pendingAssignmentRequests_.find(trackId);
                if (pending == pendingAssignmentRequests_.end()
                    || pending->second != requestId) {
                    return;
                }
            }

            juce::String error = creationError;
            const auto ok = instance != nullptr
                && publishInstance(
                    trackId,
                    std::move(instance),
                    description,
                    error);
            {
                const std::scoped_lock lock(messageMutex_);
                const auto pending =
                    pendingAssignmentRequests_.find(trackId);
                if (pending != pendingAssignmentRequests_.end()
                    && pending->second == requestId) {
                    pendingAssignmentRequests_.erase(pending);
                }
            }
            if (callback) {
                callback(
                    ok,
                    ok ? juce::String {} : error,
                    description);
            }
        });
}

void Vst3InstrumentHost::useInternalSynth(
    std::uint64_t trackId)
{
    {
        const std::scoped_lock lock(messageMutex_);
        pendingAssignmentRequests_.erase(trackId);
    }
    closeEditor(trackId);
    publishWithoutTrack(trackId);
}

bool Vst3InstrumentHost::hasInstrument(
    std::uint64_t trackId) const noexcept
{
    return runtimeForTrack(
               requestedRegistry_.load(std::memory_order_acquire),
               trackId)
        != nullptr;
}

juce::String Vst3InstrumentHost::instrumentName(
    std::uint64_t trackId) const
{
    const auto runtime = runtimeForTrack(
        requestedRegistry_.load(std::memory_order_acquire),
        trackId);
    return runtime == nullptr ? juce::String {} : runtime->name();
}

bool Vst3InstrumentHost::openEditor(
    std::uint64_t trackId,
    juce::String& error)
{
    const std::scoped_lock lock(messageMutex_);
    if (const auto found = editorWindows_.find(trackId);
        found != editorWindows_.end()) {
        found->second->setVisible(true);
        found->second->toFront(true);
        return true;
    }
    auto runtime = runtimeForTrack(
        requestedRegistry_.load(std::memory_order_acquire),
        trackId);
    if (runtime == nullptr) {
        error = "This track has no active VST3 instrument.";
        return false;
    }
    editorWindows_[trackId] =
        std::make_unique<EditorWindow>(
            std::move(runtime));
    return true;
}

void Vst3InstrumentHost::closeEditor(
    std::uint64_t trackId)
{
    const std::scoped_lock lock(messageMutex_);
    editorWindows_.erase(trackId);
}

void Vst3InstrumentHost::closeAllEditors()
{
    const std::scoped_lock lock(messageMutex_);
    editorWindows_.clear();
}

void Vst3InstrumentHost::beginAudioBlock(
    int numSamples,
    const PluginTransportPosition& position) noexcept
{
    const auto requested =
        requestedRegistry_.load(std::memory_order_acquire);
    if (requested != activeRegistry_) {
        activeRegistry_ = requested;
    }
    const auto info = makePluginPositionInfo(position);
    for (const auto& runtime : activeRegistry_->runtimes) {
        runtime->beginBlock(numSamples, info);
    }
}

void Vst3InstrumentHost::resetAllFromAudioThread(
    int sampleOffset) noexcept
{
    for (const auto& runtime : activeRegistry_->runtimes) {
        runtime->reset(sampleOffset);
    }
}

bool Vst3InstrumentHost::addMidiEventFromAudioThread(
    std::uint64_t trackId,
    bool noteOn,
    int channel,
    int pitch,
    float amplitude,
    int sampleOffset) noexcept
{
    const auto runtime =
        runtimeForTrack(activeRegistry_, trackId);
    if (runtime == nullptr) {
        return false;
    }
    runtime->addMidi(
        noteOn,
        channel,
        pitch,
        amplitude,
        sampleOffset);
    return true;
}

void Vst3InstrumentHost::processAudioBlock(
    float* const* outputs,
    int numOutputChannels,
    int numSamples,
    float masterGain,
    bool addToOutput) noexcept
{
    const auto maximumLatency =
        activeRegistry_->maximumLatencySamples;
    for (const auto& runtime : activeRegistry_->runtimes) {
        runtime->process(
            outputs,
            numOutputChannels,
            numSamples,
            maximumLatency,
            masterGain,
            addToOutput);
    }
}

int Vst3InstrumentHost::maximumLatencySamples() const noexcept
{
    return requestedMaximumLatency_.load(
        std::memory_order_acquire);
}

bool Vst3InstrumentHost::refreshLatencyLayoutIfNeeded()
{
    const auto current =
        requestedRegistry_.load(std::memory_order_acquire);
    if (current == nullptr) {
        return false;
    }
    bool changed = false;
    for (const auto& runtime : current->runtimes) {
        changed = runtime->refreshLatency() || changed;
    }
    if (!changed) {
        return false;
    }
    auto next = std::make_shared<Registry>();
    next->runtimes = current->runtimes;
    for (const auto& runtime : next->runtimes) {
        next->maximumLatencySamples = std::max(
            next->maximumLatencySamples,
            runtime->latencySamples());
    }
    publishRegistry(std::move(next));
    return true;
}

std::size_t
Vst3InstrumentHost::activeInstanceCount() const noexcept
{
    const auto registry =
        requestedRegistry_.load(std::memory_order_acquire);
    return registry == nullptr ? 0 : registry->runtimes.size();
}

void Vst3InstrumentHost::collectRetiredRuntimes()
{
    const std::scoped_lock lock(messageMutex_);
    retainedRegistries_.erase(
        std::remove_if(
            retainedRegistries_.begin(),
            retainedRegistries_.end(),
            [](const auto& registry) {
                return registry.use_count() == 1;
            }),
        retainedRegistries_.end());
}

std::vector<InstrumentRuntimeFailure>
Vst3InstrumentHost::takeRuntimeFailures()
{
    const std::scoped_lock lock(messageMutex_);
    std::vector<InstrumentRuntimeFailure> result;
    result.swap(runtimeFailures_);
    return result;
}

bool Vst3InstrumentHost::installPreparedInstanceForTesting(
    std::uint64_t trackId,
    std::unique_ptr<juce::AudioPluginInstance> instance,
    const juce::PluginDescription& description,
    juce::String& error)
{
    return publishInstance(
        trackId,
        std::move(instance),
        description,
        error);
}

std::shared_ptr<Vst3InstrumentHost::Runtime>
Vst3InstrumentHost::runtimeForTrack(
    const std::shared_ptr<const Registry>& registry,
    std::uint64_t trackId) const noexcept
{
    if (registry == nullptr) {
        return {};
    }
    const auto found = std::find_if(
        registry->runtimes.begin(),
        registry->runtimes.end(),
        [trackId](const auto& runtime) {
            return runtime->trackId() == trackId;
        });
    return found == registry->runtimes.end()
        ? std::shared_ptr<Runtime> {}
        : *found;
}

bool Vst3InstrumentHost::publishInstance(
    std::uint64_t trackId,
    std::unique_ptr<juce::AudioPluginInstance> instance,
    const juce::PluginDescription& description,
    juce::String& error)
{
    if (instance == nullptr) {
        if (error.isEmpty()) {
            error = "The VST3 instrument could not be created.";
        }
        return false;
    }
    if (!description.isInstrument
        || description.numOutputChannels <= 0) {
        error = "The selected VST3 is not an audio-output instrument.";
        return false;
    }
    if (instance->getLatencySamples()
        > kMaximumHostedInstrumentLatencySamples) {
        error = "The VST3 reports latency above DAWHermes' bounded compensation limit.";
        return false;
    }

    std::shared_ptr<Runtime> runtime;
    try {
        runtime = std::make_shared<Runtime>(
            trackId,
            std::move(instance),
            description,
            sampleRate(),
            maximumBlockSize());
    } catch (...) {
        error = "The VST3 instrument failed while being prepared.";
        return false;
    }

    const auto previous =
        requestedRegistry_.load(std::memory_order_acquire);
    auto next = std::make_shared<Registry>();
    if (previous != nullptr) {
        next->runtimes = previous->runtimes;
    }
    next->runtimes.erase(
        std::remove_if(
            next->runtimes.begin(),
            next->runtimes.end(),
            [trackId](const auto& candidate) {
                return candidate->trackId() == trackId;
            }),
        next->runtimes.end());
    next->runtimes.push_back(std::move(runtime));
    std::sort(
        next->runtimes.begin(),
        next->runtimes.end(),
        [](const auto& left, const auto& right) {
            return left->trackId() < right->trackId();
        });
    for (const auto& candidate : next->runtimes) {
        next->maximumLatencySamples = std::max(
            next->maximumLatencySamples,
            candidate->latencySamples());
    }
    closeEditor(trackId);
    publishRegistry(std::move(next));
    return true;
}

void Vst3InstrumentHost::publishWithoutTrack(
    std::uint64_t trackId)
{
    const auto previous =
        requestedRegistry_.load(std::memory_order_acquire);
    auto next = std::make_shared<Registry>();
    if (previous != nullptr) {
        for (const auto& runtime : previous->runtimes) {
            if (runtime->trackId() != trackId) {
                next->runtimes.push_back(runtime);
                next->maximumLatencySamples = std::max(
                    next->maximumLatencySamples,
                    runtime->latencySamples());
            }
        }
    }
    publishRegistry(std::move(next));
}

void Vst3InstrumentHost::publishRegistry(
    std::shared_ptr<const Registry> registry)
{
    const std::scoped_lock lock(messageMutex_);
    retainedRegistries_.push_back(registry);
    requestedMaximumLatency_.store(
        registry == nullptr
            ? 0
            : registry->maximumLatencySamples,
        std::memory_order_release);
    requestedRegistry_.store(
        std::move(registry),
        std::memory_order_release);
    ++requestedGeneration_;
}

}  // namespace dawhermes::plugins
