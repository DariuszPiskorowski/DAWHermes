#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <iterator>
#include <limits>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/MainLayoutGeometry.h"
#include "core/AudioTrackImport.h"
#include "core/MidiComparisonModel.h"
#include "core/MidiNoteEditing.h"
#include "core/MidiNoteSelectionState.h"
#include "core/MidiTimeMap.h"
#include "core/ProjectController.h"
#include "core/ProjectHistory.h"
#include "core/ProjectModel.h"
#include "core/SelectionState.h"
#include "core/TimelineGeometry.h"
#include "core/TimelineLoop.h"
#include "core/TimelineViewport.h"
#include "core/Track.h"
#include "core/TrackRouting.h"
#include "core/Utf8Path.h"
#include "audio/AudioDeviceService.h"
#include "audio/MidiAuditionEngine.h"
#include "audio/AudioTrackImporter.h"
#include "audio/MidiPlaybackModel.h"
#include "audio/SelectionPlaybackModel.h"
#include "audio/TransportModel.h"
#include "audio/WavBpmDetector.h"
#include "hermes/HermesCommandAvailability.h"
#include "hermes/ComposerAssistantConnector.h"
#include "hermes/EmbeddedHermesEngine.h"
#include "hermes/HermesCache.h"
#include "hermes/HermesJobRunner.h"
#include "hermes/HermesProjectResult.h"
#include "hermes/HermesTypes.h"
#include "hermes/HermesValidation.h"
#include "hermes/StubHermesEngine.h"
#include "midi/MidiTrackExporter.h"
#include "plugins/Vst3InstrumentHost.h"
#include "plugins/Vst3PluginCatalog.h"
#include "ui/MidiImportParser.h"
#include "ui/CommandLabels.h"

namespace {

#define EXPECT_TRUE(condition)                                                                     \
    do {                                                                                            \
        if (!(condition)) {                                                                         \
            std::cerr << "EXPECT_TRUE failed at line " << __LINE__ << ": " #condition "\n";     \
            return false;                                                                           \
        }                                                                                           \
    } while (false)

#define EXPECT_EQ(actual, expected)                                                                 \
    do {                                                                                            \
        if (!((actual) == (expected))) {                                                            \
            std::cerr << "EXPECT_EQ failed at line " << __LINE__ << ": " #actual " != " #expected \
                      << "\n";                                                                      \
            return false;                                                                           \
        }                                                                                           \
    } while (false)

using dawhermes::core::ProjectController;
using dawhermes::core::MidiNote;
using dawhermes::core::ProjectModel;
using dawhermes::core::SelectionState;
using dawhermes::core::TrackType;
using dawhermes::hermes::HermesCommand;
using dawhermes::hermes::HermesCommandAvailability;
using dawhermes::hermes::HermesDrumsOptions;
using dawhermes::hermes::HermesDrumsProfile;
using dawhermes::hermes::HermesResultLayout;
using dawhermes::hermes::HermesDetectionMode;
using dawhermes::hermes::HermesBpmOptions;
using dawhermes::hermes::HermesBassOptions;
using dawhermes::hermes::HermesGeneratedMidiTrack;
using dawhermes::hermes::HermesOperationResult;
using dawhermes::hermes::HermesOperationStatus;
using dawhermes::hermes::HermesSyncOptions;
using dawhermes::hermes::HermesSyncRole;
using dawhermes::hermes::StubHermesEngine;

struct FakePluginState {
    int prepareCount { 0 };
    int releaseCount { 0 };
    int processCount { 0 };
    int noteOnCount { 0 };
    int noteOffCount { 0 };
    int resetCount { 0 };
    int lastChannel { 0 };
    int lastVelocity { 0 };
    bool active { false };
    bool sawPlayHead { false };
    double playHeadBpm { 0.0 };
    double playHeadPpq { 0.0 };
    bool playHeadLooping { false };
    float outputLevel { 0.2f };
    int throwOnPrepareCall { 0 };
    bool outputWhenInactive { false };
    std::vector<int> processBlockSizes;
    std::vector<std::int64_t> playHeadSamplePositions;
    std::vector<double> playHeadPpqs;
    std::vector<int> noteOnOffsets;
};

struct TemporaryPluginSettings {
    TemporaryPluginSettings()
        : directory(
              juce::File::getSpecialLocation(
                  juce::File::tempDirectory)
                  .getNonexistentChildFile(
                      "dawhermes-vst3-test",
                      {},
                      false))
    {
        if (!directory.createDirectory()) {
            throw std::runtime_error(
                "Could not create temporary VST3 test settings directory.");
        }
        juce::PropertiesFile::Options options;
        options.applicationName = "DAWHermesTests";
        options.filenameSuffix = "settings";
        options.storageFormat =
            juce::PropertiesFile::storeAsXML;
        settings = std::make_unique<juce::PropertiesFile>(
            directory.getChildFile(
                "DAWHermesTests.settings"),
            options);
    }

    ~TemporaryPluginSettings()
    {
        settings.reset();
        const auto tempRoot =
            juce::File::getSpecialLocation(
                juce::File::tempDirectory);
        if (directory.getParentDirectory() == tempRoot
            && directory.getFileName().startsWith(
                "dawhermes-vst3-test")) {
            directory.deleteRecursively();
        }
    }

    juce::File directory;
    std::unique_ptr<juce::PropertiesFile> settings;
};

class FakeInstrumentInstance final
    : public juce::AudioPluginInstance {
public:
    FakeInstrumentInstance(
        FakePluginState& state,
        int latencySamples,
        juce::String name = "Fake Instrument")
        : juce::AudioPluginInstance(
              juce::AudioProcessor::BusesProperties()
                  .withOutput(
                      "Output",
                      juce::AudioChannelSet::stereo(),
                      true)),
          state_(state),
          name_(std::move(name))
    {
        setLatencySamples(latencySamples);
    }

    const juce::String getName() const override { return name_; }
    void prepareToPlay(double, int) override
    {
        ++state_.prepareCount;
        if (state_.throwOnPrepareCall
            == state_.prepareCount) {
            throw std::runtime_error(
                "Synthetic prepare failure");
        }
    }
    void releaseResources() override { ++state_.releaseCount; }
    bool isBusesLayoutSupported(
        const BusesLayout& layouts) const override
    {
        return layouts.getMainOutputChannelSet()
            == juce::AudioChannelSet::stereo();
    }
    void processBlock(
        juce::AudioBuffer<float>& buffer,
        juce::MidiBuffer& midi) override
    {
        ++state_.processCount;
        state_.processBlockSizes.push_back(
            buffer.getNumSamples());
        if (const auto* currentPlayHead = getPlayHead();
            currentPlayHead != nullptr) {
            const auto position =
                currentPlayHead->getPosition();
            if (position.hasValue()) {
                state_.sawPlayHead = true;
                state_.playHeadBpm =
                    position->getBpm().orFallback(0.0);
                state_.playHeadPpq =
                    position->getPpqPosition().orFallback(0.0);
                state_.playHeadLooping =
                    position->getIsLooping();
                state_.playHeadSamplePositions.push_back(
                    position->getTimeInSamples()
                        .orFallback(0));
                state_.playHeadPpqs.push_back(
                    state_.playHeadPpq);
            }
        }
        for (const auto metadata : midi) {
            const auto message = metadata.getMessage();
            if (message.isNoteOn()) {
                ++state_.noteOnCount;
                state_.lastChannel = message.getChannel();
                state_.lastVelocity =
                    message.getVelocity();
                state_.active = true;
                state_.noteOnOffsets.push_back(
                    metadata.samplePosition);
            } else if (message.isNoteOff()) {
                ++state_.noteOffCount;
                state_.active = false;
            } else if (message.isAllNotesOff()
                       || message.isAllSoundOff()) {
                ++state_.resetCount;
                state_.active = false;
            }
        }
        buffer.clear();
        if (state_.active
            || state_.outputWhenInactive) {
            for (int channel = 0;
                 channel < buffer.getNumChannels();
                 ++channel) {
                juce::FloatVectorOperations::fill(
                    buffer.getWritePointer(channel),
                    state_.outputLevel,
                    buffer.getNumSamples());
            }
        }
    }
    double getTailLengthSeconds() const override { return 0.0; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override {}
    void setStateInformation(const void*, int) override {}
    void fillInPluginDescription(
        juce::PluginDescription& description) const override
    {
        description.name = name_;
        description.pluginFormatName = "VST3";
        description.isInstrument = true;
        description.numOutputChannels = 2;
    }

private:
    FakePluginState& state_;
    juce::String name_;
};

MidiNote makeMidiNote(int pitch, int velocity, double startBeat, double durationBeats, int channel = 10);
HermesGeneratedMidiTrack makeGeneratedTrack(
    std::string name,
    std::vector<MidiNote> notes,
    std::string semanticLayer = {},
    bool enabledLayer = true,
    bool emptyLayer = false);

class FakeHermesEngine final : public dawhermes::hermes::IHermesEngine {
public:
    HermesOperationResult drumsMakeMidiFromWav(
        const dawhermes::hermes::HermesTrackContext&,
        const dawhermes::hermes::HermesDrumsOptions&) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        return HermesOperationResult::success(
            "fake drums",
            HermesResultLayout::singleDrumTrack,
            std::vector<HermesGeneratedMidiTrack> {
                makeGeneratedTrack("Fake", { makeMidiNote(36, 100, 0.0, 0.25) }, "drums")
            },
            120.0,
            {},
            dawhermes::hermes::HermesOperationKind::drumsExtraction);
    }

    HermesOperationResult bassMakeOrRepairMidiFromWav(
        const dawhermes::hermes::HermesAudioMidiPairContext&,
        const dawhermes::hermes::HermesBassOptions&) override
    {
        return HermesOperationResult::notImplemented();
    }

    HermesOperationResult synchronizeMidiWithWav(
        const dawhermes::hermes::HermesAudioMidiPairContext&,
        const dawhermes::hermes::HermesSyncOptions&) override
    {
        return HermesOperationResult::notImplemented();
    }

    HermesOperationResult setOrFixBpm(
        const dawhermes::hermes::HermesTrackContext&,
        const dawhermes::hermes::HermesBpmOptions&) override
    {
        return HermesOperationResult::notImplemented();
    }
};

std::filesystem::path createTempWavFixture(const std::string& stem)
{
    const auto base = std::filesystem::temp_directory_path() / ("dawhermes-" + stem + ".wav");
    std::ofstream out(base, std::ios::binary | std::ios::trunc);
    out << "RIFF";
    out.close();
    return base;
}

void writeLe16(std::ofstream& out, std::uint16_t value)
{
    const char bytes[2] {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff)
    };
    out.write(bytes, sizeof(bytes));
}

void writeLe32(std::ofstream& out, std::uint32_t value)
{
    const char bytes[4] {
        static_cast<char>(value & 0xff),
        static_cast<char>((value >> 8) & 0xff),
        static_cast<char>((value >> 16) & 0xff),
        static_cast<char>((value >> 24) & 0xff)
    };
    out.write(bytes, sizeof(bytes));
}

std::filesystem::path createSyntheticPlaybackWavFixture(
    const std::string& stem,
    int sampleRate,
    int channelCount,
    int frameCount)
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("dawhermes-playback-" + stem + "-" + std::to_string(unique) + ".wav");
    std::vector<std::int16_t> samples(
        static_cast<std::size_t>(frameCount * channelCount),
        0);
    for (int frame = 0; frame < frameCount; ++frame) {
        for (int channel = 0; channel < channelCount; ++channel) {
            const auto value = ((frame + channel) % 32) * 512 - 8192;
            samples[static_cast<std::size_t>((frame * channelCount) + channel)]
                = static_cast<std::int16_t>(value);
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const int bitsPerSample = 16;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(
        samples.size() * sizeof(std::int16_t));
    out.write("RIFF", 4);
    writeLe32(out, 36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16u);
    writeLe16(out, 1u);
    writeLe16(out, static_cast<std::uint16_t>(channelCount));
    writeLe32(out, static_cast<std::uint32_t>(sampleRate));
    writeLe32(
        out,
        static_cast<std::uint32_t>(
            sampleRate * channelCount * bitsPerSample / 8));
    writeLe16(
        out,
        static_cast<std::uint16_t>(channelCount * bitsPerSample / 8));
    writeLe16(out, static_cast<std::uint16_t>(bitsPerSample));
    out.write("data", 4);
    writeLe32(out, dataSize);
    out.write(
        reinterpret_cast<const char*>(samples.data()),
        static_cast<std::streamsize>(dataSize));
    out.close();
    return path;
}

std::filesystem::path createSyntheticClickTrackWavFixture(
    const std::string& stem,
    double bpm,
    int sampleRate,
    int channelCount,
    double durationSeconds,
    int clickAmplitude = 26000)
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("dawhermes-click-" + stem + "-" + std::to_string(unique) + ".wav");
    const auto frameCount = static_cast<int>(
        std::llround(durationSeconds * static_cast<double>(sampleRate)));
    std::vector<std::int16_t> samples(
        static_cast<std::size_t>(frameCount * channelCount),
        0);

    if (bpm > 0.0 && clickAmplitude > 0) {
        const auto framesPerBeat = (60.0 / bpm) * static_cast<double>(sampleRate);
        const auto clickFrames = std::max(1, static_cast<int>(0.018 * sampleRate));
        for (double beatFrame = 0.0;
             beatFrame < static_cast<double>(frameCount);
             beatFrame += framesPerBeat) {
            const auto start = static_cast<int>(std::llround(beatFrame));
            for (int offset = 0; offset < clickFrames && start + offset < frameCount; ++offset) {
                const auto seconds = static_cast<double>(offset)
                    / static_cast<double>(sampleRate);
                const auto envelope = std::exp(-seconds * 95.0);
                const auto wave = std::sin(
                    2.0 * 3.14159265358979323846 * 1800.0 * seconds);
                const auto value = static_cast<int>(
                    envelope * wave * static_cast<double>(clickAmplitude));
                for (int channel = 0; channel < channelCount; ++channel) {
                    const auto channelScale = channel == 0 ? 1.0 : 0.82;
                    samples[static_cast<std::size_t>(
                        ((start + offset) * channelCount) + channel)]
                        = static_cast<std::int16_t>(std::clamp(
                            static_cast<int>(value * channelScale),
                            -32767,
                            32767));
                }
            }
        }
    } else if (clickAmplitude > 0) {
        for (int frame = 0; frame < frameCount; ++frame) {
            const auto value = static_cast<std::int16_t>(
                std::sin(
                    2.0 * 3.14159265358979323846
                    * 311.0
                    * static_cast<double>(frame)
                    / static_cast<double>(sampleRate))
                * static_cast<double>(clickAmplitude));
            for (int channel = 0; channel < channelCount; ++channel) {
                samples[static_cast<std::size_t>((frame * channelCount) + channel)] = value;
            }
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const int bitsPerSample = 16;
    const std::uint32_t dataSize = static_cast<std::uint32_t>(
        samples.size() * sizeof(std::int16_t));
    out.write("RIFF", 4);
    writeLe32(out, 36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16u);
    writeLe16(out, 1u);
    writeLe16(out, static_cast<std::uint16_t>(channelCount));
    writeLe32(out, static_cast<std::uint32_t>(sampleRate));
    writeLe32(
        out,
        static_cast<std::uint32_t>(
            sampleRate * channelCount * bitsPerSample / 8));
    writeLe16(
        out,
        static_cast<std::uint16_t>(channelCount * bitsPerSample / 8));
    writeLe16(out, static_cast<std::uint16_t>(bitsPerSample));
    out.write("data", 4);
    writeLe32(out, dataSize);
    out.write(
        reinterpret_cast<const char*>(samples.data()),
        static_cast<std::streamsize>(dataSize));
    out.close();
    return path;
}

struct SyntheticRhythmEvent {
    double beatOffset { 0.0 };
    double amplitude { 1.0 };
    double frequencyHz { 1000.0 };
    double durationSeconds { 0.03 };
    double decayPerSecond { 80.0 };
};

std::filesystem::path createSyntheticRhythmWavFixture(
    const std::string& stem,
    double bpm,
    int sampleRate,
    int channelCount,
    double durationSeconds,
    double patternBeats,
    const std::vector<SyntheticRhythmEvent>& events)
{
    const auto unique = std::chrono::steady_clock::now().time_since_epoch().count();
    const auto path = std::filesystem::temp_directory_path()
        / ("dawhermes-rhythm-" + stem + "-" + std::to_string(unique) + ".wav");
    const auto frameCount = static_cast<int>(
        std::llround(durationSeconds * static_cast<double>(sampleRate)));
    std::vector<double> mixed(
        static_cast<std::size_t>(frameCount * channelCount),
        0.0);
    const auto framesPerBeat =
        (60.0 / bpm) * static_cast<double>(sampleRate);

    for (double cycleBeat = 0.0;
         cycleBeat * framesPerBeat < static_cast<double>(frameCount);
         cycleBeat += patternBeats) {
        for (const auto& event : events) {
            const auto start = static_cast<int>(std::llround(
                (cycleBeat + event.beatOffset) * framesPerBeat));
            const auto eventFrames = std::max(
                1,
                static_cast<int>(std::llround(
                    event.durationSeconds * static_cast<double>(sampleRate))));
            for (int offset = 0;
                 offset < eventFrames
                 && start + offset < frameCount;
                 ++offset) {
                if (start + offset < 0) {
                    continue;
                }
                const auto seconds = static_cast<double>(offset)
                    / static_cast<double>(sampleRate);
                const auto envelope = std::exp(
                    -seconds * event.decayPerSecond);
                const auto wave = std::sin(
                    2.0 * 3.14159265358979323846
                    * event.frequencyHz
                    * seconds);
                for (int channel = 0; channel < channelCount; ++channel) {
                    const auto channelScale = channel == 0 ? 1.0 : 0.82;
                    mixed[static_cast<std::size_t>(
                        ((start + offset) * channelCount) + channel)]
                        += wave
                        * envelope
                        * event.amplitude
                        * channelScale
                        * 26000.0;
                }
            }
        }
    }

    std::vector<std::int16_t> samples(mixed.size(), 0);
    std::transform(
        mixed.begin(),
        mixed.end(),
        samples.begin(),
        [](double sample) {
            return static_cast<std::int16_t>(std::clamp(
                static_cast<int>(std::llround(sample)),
                -32767,
                32767));
        });

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const int bitsPerSample = 16;
    const auto dataSize = static_cast<std::uint32_t>(
        samples.size() * sizeof(std::int16_t));
    out.write("RIFF", 4);
    writeLe32(out, 36u + dataSize);
    out.write("WAVE", 4);
    out.write("fmt ", 4);
    writeLe32(out, 16u);
    writeLe16(out, 1u);
    writeLe16(out, static_cast<std::uint16_t>(channelCount));
    writeLe32(out, static_cast<std::uint32_t>(sampleRate));
    writeLe32(
        out,
        static_cast<std::uint32_t>(
            sampleRate * channelCount * bitsPerSample / 8));
    writeLe16(
        out,
        static_cast<std::uint16_t>(
            channelCount * bitsPerSample / 8));
    writeLe16(out, static_cast<std::uint16_t>(bitsPerSample));
    out.write("data", 4);
    writeLe32(out, dataSize);
    out.write(
        reinterpret_cast<const char*>(samples.data()),
        static_cast<std::streamsize>(dataSize));
    out.close();
    return path;
}

std::vector<double> createSyntheticOnsetEnvelope(
    double bpm,
    double durationSeconds,
    const std::vector<double>& repeatingBeatStrengths)
{
    constexpr double envelopeRate = 200.0;
    std::vector<double> onset(
        static_cast<std::size_t>(
            std::llround(durationSeconds * envelopeRate)),
        0.0);
    const auto period = (60.0 * envelopeRate) / bpm;
    std::size_t beatIndex = 0;
    for (double position = 8.0;
         position < static_cast<double>(onset.size() - 3);
         position += period, ++beatIndex) {
        const auto strength = repeatingBeatStrengths[
            beatIndex % repeatingBeatStrengths.size()];
        const auto center = static_cast<std::size_t>(std::llround(position));
        onset[center] = std::max(onset[center], strength);
        onset[center + 1] = std::max(onset[center + 1], strength * 0.45);
        onset[center + 2] = std::max(onset[center + 2], strength * 0.15);
    }
    return onset;
}

std::filesystem::path createSyntheticDrumWavFixture(const std::string& stem)
{
    const int sampleRate = 44100;
    const int channelCount = 1;
    const int bitsPerSample = 16;
    const double seconds = 4.0;
    const int totalFrames = static_cast<int>(seconds * static_cast<double>(sampleRate));

    std::vector<std::int16_t> samples(static_cast<std::size_t>(totalFrames), 0);
    const std::vector<double> hitTimes { 0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5 };

    for (const double hitTime : hitTimes) {
        const int start = static_cast<int>(hitTime * static_cast<double>(sampleRate));
        const int length = static_cast<int>(0.06 * static_cast<double>(sampleRate));
        for (int i = 0; i < length; ++i) {
            const int index = start + i;
            if (index < 0 || index >= totalFrames) {
                break;
            }

            const double t = static_cast<double>(i) / static_cast<double>(sampleRate);
            const double envelope = std::exp(-t * 55.0);
            const double low = std::sin(2.0 * 3.14159265358979323846 * 85.0 * t);
            const double click = std::sin(2.0 * 3.14159265358979323846 * 2400.0 * t);
            const double value = envelope * (0.85 * low + 0.15 * click);
            const auto scaled = static_cast<int>(value * 27000.0);
            const auto clamped = std::clamp(scaled, -32767, 32767);
            samples[static_cast<std::size_t>(index)] = static_cast<std::int16_t>(clamped);
        }
    }

    const auto path = std::filesystem::temp_directory_path() / ("dawhermes-synth-" + stem + ".wav");
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    const std::uint32_t dataSize = static_cast<std::uint32_t>(samples.size() * sizeof(std::int16_t));
    const std::uint32_t riffSize = 36u + dataSize;

    out.write("RIFF", 4);
    writeLe32(out, riffSize);
    out.write("WAVE", 4);

    out.write("fmt ", 4);
    writeLe32(out, 16u);
    writeLe16(out, 1u);
    writeLe16(out, static_cast<std::uint16_t>(channelCount));
    writeLe32(out, static_cast<std::uint32_t>(sampleRate));
    writeLe32(out, static_cast<std::uint32_t>(sampleRate * channelCount * bitsPerSample / 8));
    writeLe16(out, static_cast<std::uint16_t>(channelCount * bitsPerSample / 8));
    writeLe16(out, static_cast<std::uint16_t>(bitsPerSample));

    out.write("data", 4);
    writeLe32(out, dataSize);
    out.write(reinterpret_cast<const char*>(samples.data()), static_cast<std::streamsize>(dataSize));
    out.close();
    return path;
}

std::string getEnvironment(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }

    std::string result(value);
    free(value);
    return result;
#else
    const char* value = std::getenv(name);
    return value != nullptr ? std::string(value) : std::string {};
#endif
}

void setEnvironment(const char* name, const std::string& value)
{
#ifdef _WIN32
    _putenv_s(name, value.c_str());
#else
    setenv(name, value.c_str(), 1);
#endif
}

struct EnvironmentGuard {
    explicit EnvironmentGuard(const char* variableName)
        : name(variableName), originalValue(getEnvironment(variableName))
    {
    }

    ~EnvironmentGuard()
    {
        setEnvironment(name.c_str(), originalValue);
    }

    std::string name;
    std::string originalValue;
};

struct CurrentDirectoryGuard {
    CurrentDirectoryGuard()
        : originalPath(std::filesystem::current_path())
    {
    }

    ~CurrentDirectoryGuard()
    {
        std::error_code ec;
        std::filesystem::current_path(originalPath, ec);
    }

    std::filesystem::path originalPath;
};

MidiNote makeMidiNote(int pitch, int velocity, double startBeat, double durationBeats, int channel)
{
    return MidiNote { pitch, velocity, startBeat, durationBeats, channel };
}

HermesGeneratedMidiTrack makeGeneratedTrack(
    std::string name,
    std::vector<MidiNote> notes,
    std::string semanticLayer,
    bool enabledLayer,
    bool emptyLayer)
{
    HermesGeneratedMidiTrack track;
    track.trackName = std::move(name);
    track.semanticLayer = std::move(semanticLayer);
    track.enabledLayer = enabledLayer;
    track.emptyLayer = emptyLayer;
    track.notes = std::move(notes);
    return track;
}

std::size_t countNotesOnTrack(const dawhermes::core::Track& track)
{
    return track.midiNotes.size();
}

std::vector<std::uint64_t> noteIdsFor(const std::vector<MidiNote>& notes)
{
    std::vector<std::uint64_t> ids;
    ids.reserve(notes.size());
    for (const auto& note : notes) {
        ids.push_back(note.id);
    }
    return ids;
}

bool containsNoteId(const std::vector<std::uint64_t>& ids, std::uint64_t noteId)
{
    return std::find(ids.begin(), ids.end(), noteId) != ids.end();
}

bool approxEqual(double left, double right, double tolerance = 0.0001)
{
    return std::abs(left - right) <= tolerance;
}

std::filesystem::path createTempDirectory(const std::string& stem)
{
    const auto now = std::chrono::steady_clock::now().time_since_epoch().count();
    auto path = std::filesystem::temp_directory_path()
        / ("dawhermes-" + stem + "-" + std::to_string(now));
    std::error_code ec;
    std::filesystem::create_directories(path, ec);
    return path;
}

std::string readBinaryFile(const std::filesystem::path& path)
{
    std::ifstream in(path, std::ios::binary);
    return std::string(
        std::istreambuf_iterator<char>(in),
        std::istreambuf_iterator<char>());
}

const MidiNote* findNoteByPitch(const std::vector<MidiNote>& notes, int pitch)
{
    const auto it = std::find_if(notes.begin(), notes.end(), [pitch](const MidiNote& note) {
        return note.pitch == pitch;
    });

    return it != notes.end() ? &(*it) : nullptr;
}

class TestCreateMidiNoteCommand final : public dawhermes::core::ProjectEditCommand {
public:
    TestCreateMidiNoteCommand(ProjectModel& project, std::uint64_t trackId, MidiNote note)
        : project_(project), trackId_(trackId), note_(note)
    {
    }

    std::string label() const override { return "Create MIDI Note"; }

    bool undo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr) {
            return false;
        }

        return dawhermes::core::deleteSelectedNotes(track->midiNotes, { note_.id }) == 1;
    }

    bool redo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr) {
            return false;
        }

        if (containsNoteId(noteIdsFor(track->midiNotes), note_.id)) {
            return false;
        }

        track->midiNotes.push_back(note_);
        dawhermes::core::sortMidiNotesByStart(track->midiNotes);
        return true;
    }

private:
    ProjectModel& project_;
    std::uint64_t trackId_ { 0 };
    MidiNote note_;
};

class TestDeleteMidiNotesCommand final : public dawhermes::core::ProjectEditCommand {
public:
    TestDeleteMidiNotesCommand(
        ProjectModel& project,
        std::uint64_t trackId,
        std::vector<std::uint64_t> selectedIds,
        std::vector<MidiNote> deletedNotes)
        : project_(project),
          trackId_(trackId),
          selectedIds_(std::move(selectedIds)),
          deletedNotes_(std::move(deletedNotes))
    {
    }

    std::string label() const override { return "Delete MIDI Notes"; }

    bool undo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr) {
            return false;
        }

        track->midiNotes.insert(track->midiNotes.end(), deletedNotes_.begin(), deletedNotes_.end());
        dawhermes::core::sortMidiNotesByStart(track->midiNotes);
        return true;
    }

    bool redo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr) {
            return false;
        }

        return dawhermes::core::deleteSelectedNotes(track->midiNotes, selectedIds_) == deletedNotes_.size();
    }

private:
    ProjectModel& project_;
    std::uint64_t trackId_ { 0 };
    std::vector<std::uint64_t> selectedIds_;
    std::vector<MidiNote> deletedNotes_;
};

class TestReplaceMidiNotesCommand final : public dawhermes::core::ProjectEditCommand {
public:
    TestReplaceMidiNotesCommand(
        ProjectModel& project,
        std::uint64_t trackId,
        std::string label,
        std::vector<MidiNote> beforeNotes,
        std::vector<MidiNote> afterNotes)
        : project_(project),
          trackId_(trackId),
          label_(std::move(label)),
          beforeNotes_(std::move(beforeNotes)),
          afterNotes_(std::move(afterNotes))
    {
    }

    std::string label() const override { return label_; }

    bool undo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr) {
            return false;
        }

        track->midiNotes = beforeNotes_;
        return true;
    }

    bool redo() override
    {
        auto* track = project_.findTrackById(trackId_);
        if (track == nullptr) {
            return false;
        }

        track->midiNotes = afterNotes_;
        return true;
    }

private:
    ProjectModel& project_;
    std::uint64_t trackId_ { 0 };
    std::string label_;
    std::vector<MidiNote> beforeNotes_;
    std::vector<MidiNote> afterNotes_;
};

bool testTrackCreationAudioAndMidi()
{
    ProjectModel project;
    const auto audio = project.addTrack(TrackType::audio);
    const auto midi = project.addTrack(TrackType::midi);

    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(2));
    EXPECT_EQ(audio.type, TrackType::audio);
    EXPECT_EQ(midi.type, TrackType::midi);
    return true;
}

bool testAudioSourceAssignment()
{
    ProjectModel project;
    const auto audioId = project.addTrack(TrackType::audio).id;
    const auto midiId = project.addTrack(TrackType::midi).id;

    EXPECT_TRUE(project.setAudioSourcePath(audioId, "C:/tmp/drums.wav"));
    EXPECT_TRUE(!project.setAudioSourcePath(midiId, "C:/tmp/not-allowed.wav"));

    const auto* audioTrack = project.findTrackById(audioId);
    EXPECT_TRUE(audioTrack != nullptr);
    EXPECT_EQ(audioTrack->audioSourcePath, std::string("C:/tmp/drums.wav"));
    return true;
}

bool testDirectAudioImportCommandPolicy()
{
    const auto& fileMenu = dawhermes::ui::command_labels::fileMenu;
    const auto& trackMenu = dawhermes::ui::command_labels::trackMenu;
    const auto& audioMenu = dawhermes::ui::command_labels::audioMenu;
    EXPECT_TRUE(std::find(
        fileMenu.begin(),
        fileMenu.end(),
        dawhermes::ui::command_labels::importAudioAsTrack) != fileMenu.end());
    EXPECT_TRUE(std::find(
        fileMenu.begin(),
        fileMenu.end(),
        dawhermes::ui::command_labels::importMidiAsTrack) != fileMenu.end());
    EXPECT_EQ(audioMenu.size(), static_cast<std::size_t>(4));
    EXPECT_TRUE(std::find(
        audioMenu.begin(),
        audioMenu.end(),
        dawhermes::ui::command_labels::audioSettings)
        != audioMenu.end());
    EXPECT_TRUE(std::find(
        audioMenu.begin(),
        audioMenu.end(),
        dawhermes::ui::command_labels::audioDeviceStatus)
        != audioMenu.end());

    constexpr std::array obsoleteAudioCommands {
        std::string_view { "Assign Audio Source" },
        std::string_view { "Assign WAV" },
        std::string_view { "Assign WAV to selected audio track" },
        std::string_view { "Choose Audio Source" },
        std::string_view { "Replace Audio Source" },
        std::string_view { "Add Audio Track" },
    };
    for (const auto obsolete : obsoleteAudioCommands) {
        EXPECT_TRUE(std::find(fileMenu.begin(), fileMenu.end(), obsolete) == fileMenu.end());
        EXPECT_TRUE(std::find(trackMenu.begin(), trackMenu.end(), obsolete) == trackMenu.end());
    }
    return true;
}

bool testSingleAudioTrackImportMetadataPlaybackAndHistory()
{
    const auto sourcePath = createSyntheticPlaybackWavFixture(
        "import-single",
        48000,
        2,
        96000);
    const auto sourceBefore = readBinaryFile(sourcePath);
    const auto preparation = dawhermes::audio::prepareAudioTrackImports(
        { sourcePath });
    EXPECT_EQ(preparation.validTracks.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(preparation.skippedFiles.empty());

    ProjectModel project;
    SelectionState selection;
    dawhermes::core::ProjectHistory history;
    auto command = std::make_unique<dawhermes::core::ImportAudioTracksCommand>(
        project,
        selection,
        preparation.validTracks);
    EXPECT_TRUE(command->redo());
    EXPECT_EQ(command->createdTrackIds().size(), static_cast<std::size_t>(1));
    const auto createdId = command->createdTrackIds().front();
    history.pushExecuted(std::move(command));

    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(1));
    EXPECT_TRUE(selection.isSelected(createdId));

    const auto* imported = project.findTrackById(createdId);
    EXPECT_TRUE(imported != nullptr);
    EXPECT_EQ(imported->type, TrackType::audio);
    EXPECT_EQ(imported->name, sourcePath.stem().string());
    EXPECT_EQ(
        std::filesystem::path(imported->audioSourcePath).lexically_normal(),
        std::filesystem::absolute(sourcePath).lexically_normal());
    EXPECT_TRUE(imported->audioSourceMetadata.has_value());
    EXPECT_TRUE(approxEqual(imported->audioSourceMetadata->sampleRate, 48000.0));
    EXPECT_EQ(imported->audioSourceMetadata->channelCount, 2);
    EXPECT_TRUE(approxEqual(imported->audioSourceMetadata->durationSeconds, 2.0));
    EXPECT_EQ(
        imported->audioSourceMetadata->frameCount,
        static_cast<std::uint64_t>(96000));
    EXPECT_EQ(imported->audioSourceMetadata->bitsPerSample, 16);
    EXPECT_TRUE(std::filesystem::exists(imported->audioSourcePath));

    const auto summary = dawhermes::audio::createSelectionPlaybackSummary(
        project,
        selection);
    EXPECT_TRUE(summary.playable);
    EXPECT_EQ(summary.audioTrackCount, static_cast<std::size_t>(1));
    EXPECT_TRUE(approxEqual(summary.durationSeconds, 2.0, 0.001));
    EXPECT_TRUE(summary.firstReadableAudioPath.has_value());
    EXPECT_TRUE(dawhermes::audio::fingerprintWavFile(
        summary.firstReadableAudioPath.value()).has_value());
    EXPECT_EQ(readBinaryFile(sourcePath), sourceBefore);

    EXPECT_EQ(history.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(history.undo());
    EXPECT_TRUE(project.empty());
    EXPECT_TRUE(!selection.hasSelection());
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(project.tracks().front().audioSourceMetadata.has_value());
    EXPECT_EQ(project.tracks().front().name, sourcePath.stem().string());
    EXPECT_EQ(
        std::filesystem::path(project.tracks().front().audioSourcePath).lexically_normal(),
        std::filesystem::absolute(sourcePath).lexically_normal());
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(readBinaryFile(sourcePath), sourceBefore);

    std::error_code error;
    std::filesystem::remove(sourcePath, error);
    return true;
}

bool testBatchAudioTrackImportSkipsInvalidAndRestoresSelection()
{
    const auto firstPath = createSyntheticPlaybackWavFixture(
        "import-batch-first",
        44100,
        1,
        44100);
    const auto secondPath = createSyntheticPlaybackWavFixture(
        "import-batch-second",
        48000,
        2,
        96000);
    const auto invalidPath = createTempWavFixture("import-batch-invalid");
    const auto firstBefore = readBinaryFile(firstPath);
    const auto secondBefore = readBinaryFile(secondPath);
    const auto invalidBefore = readBinaryFile(invalidPath);

    const auto preparation = dawhermes::audio::prepareAudioTrackImports(
        { firstPath, invalidPath, secondPath });
    EXPECT_EQ(preparation.validTracks.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(preparation.skippedFiles.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(
        std::filesystem::path(preparation.validTracks.at(0).sourcePath).lexically_normal(),
        std::filesystem::absolute(firstPath).lexically_normal());
    EXPECT_EQ(
        std::filesystem::path(preparation.validTracks.at(1).sourcePath).lexically_normal(),
        std::filesystem::absolute(secondPath).lexically_normal());

    ProjectModel project;
    const auto existingMidiId = project.addTrack(TrackType::midi, "Existing MIDI").id;
    SelectionState selection;
    selection.selectTrack(existingMidiId);
    dawhermes::core::ProjectHistory history;
    auto command = std::make_unique<dawhermes::core::ImportAudioTracksCommand>(
        project,
        selection,
        preparation.validTracks);
    EXPECT_TRUE(command->redo());
    EXPECT_EQ(command->createdTrackIds().size(), static_cast<std::size_t>(2));
    const auto importedIds = command->createdTrackIds();
    history.pushExecuted(std::move(command));

    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(3));
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(2));
    EXPECT_TRUE(selection.isSelected(importedIds.at(0)));
    EXPECT_TRUE(selection.isSelected(importedIds.at(1)));
    EXPECT_EQ(
        project.findTrackById(importedIds.at(0))->name,
        firstPath.stem().string());
    EXPECT_EQ(
        project.findTrackById(importedIds.at(1))->name,
        secondPath.stem().string());

    const auto summary = dawhermes::audio::createSelectionPlaybackSummary(
        project,
        selection);
    EXPECT_TRUE(summary.playable);
    EXPECT_EQ(summary.audioTrackCount, static_cast<std::size_t>(2));
    EXPECT_TRUE(approxEqual(summary.durationSeconds, 2.0, 0.001));
    EXPECT_EQ(
        std::filesystem::path(summary.firstReadableAudioPath.value()).lexically_normal(),
        std::filesystem::absolute(firstPath).lexically_normal());

    EXPECT_TRUE(history.undo());
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(selection.isSelected(existingMidiId));
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(3));
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(2));
    EXPECT_EQ(readBinaryFile(firstPath), firstBefore);
    EXPECT_EQ(readBinaryFile(secondPath), secondBefore);
    EXPECT_EQ(readBinaryFile(invalidPath), invalidBefore);

    const auto invalidOnly = dawhermes::audio::prepareAudioTrackImports(
        { invalidPath });
    EXPECT_TRUE(invalidOnly.validTracks.empty());
    EXPECT_EQ(invalidOnly.skippedFiles.size(), static_cast<std::size_t>(1));

    std::error_code error;
    std::filesystem::remove(firstPath, error);
    std::filesystem::remove(secondPath, error);
    std::filesystem::remove(invalidPath, error);
    return true;
}

bool testUnicodeSafeAudioImportPlaybackAndHistory()
{
    const auto tempDirectory = createTempDirectory("unicode-audio-path");
    const auto unicodeDirectory = tempDirectory
        / std::filesystem::path(L"Za\u017C\u00F3\u0142\u0107-g\u0119\u015Bl\u0105");
    std::error_code error;
    std::filesystem::create_directories(unicodeDirectory, error);
    EXPECT_TRUE(!error);

    const auto asciiFixture = createSyntheticPlaybackWavFixture(
        "unicode-source",
        44100,
        2,
        4410);
    const auto unicodePath = unicodeDirectory
        / std::filesystem::path(
            L"\u015Acie\u017Cka-\u017C\u00F3\u0142\u0107.wav");
    std::filesystem::rename(asciiFixture, unicodePath, error);
    EXPECT_TRUE(!error);
    const auto sourceBefore = readBinaryFile(unicodePath);

    const auto preparation = dawhermes::audio::prepareAudioTrackImports(
        { unicodePath });
    EXPECT_EQ(preparation.validTracks.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(preparation.skippedFiles.empty());
    EXPECT_EQ(
        preparation.validTracks.front().trackName,
        dawhermes::core::pathToUtf8(unicodePath.stem()));
    EXPECT_EQ(
        dawhermes::core::pathFromUtf8(
            preparation.validTracks.front().sourcePath).lexically_normal(),
        std::filesystem::absolute(unicodePath).lexically_normal());

    ProjectModel project;
    SelectionState selection;
    dawhermes::core::ProjectHistory history;
    auto command = std::make_unique<dawhermes::core::ImportAudioTracksCommand>(
        project,
        selection,
        preparation.validTracks);
    EXPECT_TRUE(command->redo());
    const auto trackId = command->createdTrackIds().front();
    history.pushExecuted(std::move(command));

    const auto* imported = project.findTrackById(trackId);
    EXPECT_TRUE(imported != nullptr);
    EXPECT_EQ(
        imported->name,
        dawhermes::core::pathToUtf8(unicodePath.stem()));
    EXPECT_EQ(
        dawhermes::core::pathFromUtf8(imported->audioSourcePath)
            .lexically_normal(),
        std::filesystem::absolute(unicodePath).lexically_normal());

    std::string inspectionError;
    const auto inspection = dawhermes::ui::inspectWavFile(
        dawhermes::core::pathFromUtf8(imported->audioSourcePath),
        inspectionError);
    EXPECT_TRUE(inspection.has_value());
    EXPECT_TRUE(inspectionError.empty());
    EXPECT_EQ(inspection->channelCount, 2);

    const auto summary = dawhermes::audio::createSelectionPlaybackSummary(
        project,
        selection);
    EXPECT_TRUE(summary.playable);
    EXPECT_EQ(summary.audioTrackCount, static_cast<std::size_t>(1));
    const auto snapshot = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        selection);
    EXPECT_TRUE(snapshot.ok);
    EXPECT_EQ(snapshot.snapshot.audioTrackCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(
        dawhermes::core::pathFromUtf8(
            snapshot.snapshot.audioStems.front().sourcePath)
            .lexically_normal(),
        std::filesystem::absolute(unicodePath).lexically_normal());

    const auto fingerprint = dawhermes::audio::fingerprintWavFile(
        dawhermes::core::pathFromUtf8(imported->audioSourcePath));
    EXPECT_TRUE(fingerprint.has_value());
    EXPECT_EQ(
        dawhermes::core::pathFromUtf8(fingerprint->sourcePath)
            .lexically_normal(),
        std::filesystem::absolute(unicodePath).lexically_normal());
    const auto estimate = dawhermes::audio::analyzeWavBpm(
        dawhermes::core::pathFromUtf8(imported->audioSourcePath),
        5.0);
    EXPECT_TRUE(estimate.analyzedSeconds > 0.0);

    dawhermes::hermes::HermesTrackContext context {
        imported->id,
        imported->name,
        imported->type,
        imported->audioSourcePath,
    };
    EXPECT_TRUE(
        dawhermes::hermes::validateTrackContextForDrums(context).ok);
    EXPECT_EQ(readBinaryFile(unicodePath), sourceBefore);

    EXPECT_TRUE(history.undo());
    EXPECT_TRUE(project.empty());
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1));
    EXPECT_EQ(
        dawhermes::core::pathFromUtf8(
            project.tracks().front().audioSourcePath)
            .lexically_normal(),
        std::filesystem::absolute(unicodePath).lexically_normal());
    EXPECT_EQ(readBinaryFile(unicodePath), sourceBefore);

    std::filesystem::remove_all(tempDirectory, error);
    return true;
}

bool testMidiNoteReplacement()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi).id;
    const auto audioId = project.addTrack(TrackType::audio).id;

    std::vector<MidiNote> notes;
    notes.push_back(MidiNote { 36, 110, 1.0, 0.5, 10 });
    notes.push_back(MidiNote { 38, 105, 2.0, 0.5, 10 });

    EXPECT_TRUE(project.replaceMidiNotes(midiId, notes));
    EXPECT_TRUE(!project.replaceMidiNotes(audioId, notes));

    const auto* midiTrack = project.findTrackById(midiId);
    EXPECT_TRUE(midiTrack != nullptr);
    EXPECT_EQ(midiTrack->midiNotes.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(midiTrack->midiNotes.at(0).pitch, 36);
    EXPECT_EQ(midiTrack->midiNotes.at(1).pitch, 38);
    return true;
}

bool testStableTrackIds()
{
    ProjectModel project;
    const auto firstId = project.addTrack(TrackType::audio).id;
    const auto secondId = project.addTrack(TrackType::audio).id;

    EXPECT_TRUE(project.removeTrackById(firstId));

    const auto thirdId = project.addTrack(TrackType::midi).id;
    EXPECT_TRUE(secondId > firstId);
    EXPECT_TRUE(thirdId > secondId);
    return true;
}

bool testSelectionState()
{
    SelectionState selection;
    EXPECT_TRUE(!selection.hasSelection());

    selection.selectTrack(42);
    EXPECT_TRUE(selection.hasSelection());
    EXPECT_TRUE(selection.isSelected(42));

    selection.clear();
    EXPECT_TRUE(!selection.hasSelection());
    return true;
}

bool testSelectionStateMultiToggle()
{
    SelectionState selection;

    selection.selectTrack(100);
    selection.toggleTrack(200);
    EXPECT_TRUE(selection.hasSelection());
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(2));
    EXPECT_TRUE(selection.isSelected(100));
    EXPECT_TRUE(selection.isSelected(200));
    EXPECT_EQ(selection.selectedTrackId().value_or(0), static_cast<std::uint64_t>(200));

    selection.toggleTrack(200);
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(1));
    EXPECT_TRUE(selection.isSelected(100));
    EXPECT_EQ(selection.selectedTrackId().value_or(0), static_cast<std::uint64_t>(100));

    selection.toggleTrack(100);
    EXPECT_TRUE(!selection.hasSelection());
    return true;
}

bool testDeleteSelectedTrack()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto& track = controller.addTrack(TrackType::audio);
    controller.selectTrack(track.id);

    EXPECT_TRUE(controller.canDeleteSelectedTrack());
    EXPECT_TRUE(controller.deleteSelectedTrack());
    EXPECT_TRUE(project.empty());
    EXPECT_TRUE(!selection.hasSelection());
    return true;
}

bool testDeleteUnselectedTrack()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto firstId = controller.addTrack(TrackType::audio).id;
    const auto secondId = controller.addTrack(TrackType::midi).id;

    controller.selectTrack(firstId);
    EXPECT_TRUE(controller.deleteTrackById(secondId));
    EXPECT_TRUE(selection.hasSelection());
    EXPECT_TRUE(selection.isSelected(firstId));
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1));
    return true;
}

bool testEmptyProjectSafety()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    EXPECT_TRUE(project.empty());
    EXPECT_TRUE(!controller.deleteSelectedTrack());
    EXPECT_TRUE(!project.removeTrackById(123));
    return true;
}

bool testC1RangeValidation()
{
    HermesDrumsOptions options;
    options.c1MidiNote = 0;
    EXPECT_TRUE(dawhermes::hermes::validateDrumsOptions(options).ok);

    options.c1MidiNote = 127;
    EXPECT_TRUE(dawhermes::hermes::validateDrumsOptions(options).ok);

    options.c1MidiNote = -1;
    EXPECT_TRUE(!dawhermes::hermes::validateDrumsOptions(options).ok);

    options.c1MidiNote = 128;
    EXPECT_TRUE(!dawhermes::hermes::validateDrumsOptions(options).ok);
    return true;
}

bool testBpmValidation()
{
    HermesBpmOptions options;
    options.bpm = 120.0;
    EXPECT_TRUE(dawhermes::hermes::validateBpmOptions(options).ok);

    options.bpm = 0.0;
    EXPECT_TRUE(!dawhermes::hermes::validateBpmOptions(options).ok);

    options.bpm = -10.0;
    EXPECT_TRUE(!dawhermes::hermes::validateBpmOptions(options).ok);

    options.bpm = std::numeric_limits<double>::infinity();
    EXPECT_TRUE(!dawhermes::hermes::validateBpmOptions(options).ok);
    return true;
}

bool testDrumsEnumsValidation()
{
    EXPECT_TRUE(dawhermes::hermes::isValidDrumsProfile(HermesDrumsProfile::conservative));
    EXPECT_TRUE(dawhermes::hermes::isValidDrumsProfile(HermesDrumsProfile::balanced));
    EXPECT_TRUE(dawhermes::hermes::isValidDrumsProfile(HermesDrumsProfile::sensitive));
    EXPECT_TRUE(!dawhermes::hermes::isValidDrumsProfile(static_cast<HermesDrumsProfile>(99)));

    EXPECT_TRUE(dawhermes::hermes::isValidDetectionMode(HermesDetectionMode::multiDetector));
    EXPECT_TRUE(dawhermes::hermes::isValidDetectionMode(HermesDetectionMode::global));
    EXPECT_TRUE(!dawhermes::hermes::isValidDetectionMode(static_cast<HermesDetectionMode>(99)));

    EXPECT_TRUE(dawhermes::hermes::isValidResultLayout(HermesResultLayout::separateMidiTracks));
    EXPECT_TRUE(dawhermes::hermes::isValidResultLayout(HermesResultLayout::groupedMultitrack));
    EXPECT_TRUE(dawhermes::hermes::isValidResultLayout(HermesResultLayout::singleDrumTrack));
    EXPECT_TRUE(!dawhermes::hermes::isValidResultLayout(static_cast<HermesResultLayout>(99)));
    return true;
}

bool testDrumsTrackContextValidation()
{
    dawhermes::hermes::HermesTrackContext missingPath {
        1,
        "Audio Track 1",
        TrackType::audio,
        {}
    };
    EXPECT_TRUE(!dawhermes::hermes::validateTrackContextForDrums(missingPath).ok);

    const auto missingFixturePath = (std::filesystem::temp_directory_path() / "dawhermes-missing.wav").string();
    dawhermes::hermes::HermesTrackContext missingFile {
        1,
        "Audio Track 1",
        TrackType::audio,
        missingFixturePath
    };
    EXPECT_TRUE(!dawhermes::hermes::validateTrackContextForDrums(missingFile).ok);

    const auto wavFixture = createTempWavFixture("track-context");

    dawhermes::hermes::HermesTrackContext validAudio {
        1,
        "Audio Track 1",
        TrackType::audio,
        wavFixture.string()
    };
    EXPECT_TRUE(dawhermes::hermes::validateTrackContextForDrums(validAudio).ok);

    dawhermes::hermes::HermesTrackContext midiTrack {
        2,
        "MIDI Track 1",
        TrackType::midi,
        wavFixture.string()
    };
    EXPECT_TRUE(!dawhermes::hermes::validateTrackContextForDrums(midiTrack).ok);

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testMainLayoutGeometry()
{
    const auto layout = dawhermes::core::computeMainLayoutGeometry(1600, 900);

    EXPECT_TRUE(layout.trackList.width > 0);
    EXPECT_TRUE(layout.timeline.width > 0);
    EXPECT_TRUE(layout.aiAssistant.width > 0);
    EXPECT_TRUE(layout.timeline.height > 0);
    EXPECT_TRUE(layout.aiAssistant.height > 0);

    const auto topRowBottom = std::max(
        layout.trackList.y + layout.trackList.height,
        std::max(layout.timeline.y + layout.timeline.height, layout.aiAssistant.y + layout.aiAssistant.height));
    EXPECT_TRUE(layout.midiEditor.y >= topRowBottom);

    const auto topLeft = std::min(layout.trackList.x, std::min(layout.timeline.x, layout.aiAssistant.x));
    const auto topRight = std::max(
        layout.trackList.x + layout.trackList.width,
        std::max(layout.timeline.x + layout.timeline.width, layout.aiAssistant.x + layout.aiAssistant.width));
    const auto midiLeft = layout.midiEditor.x;
    const auto midiRight = layout.midiEditor.x + layout.midiEditor.width;

    EXPECT_TRUE(midiLeft <= topLeft);
    EXPECT_TRUE(midiRight >= topRight);
    EXPECT_TRUE(layout.aiAssistant.y + layout.aiAssistant.height <= layout.midiEditor.y);
    return true;
}

bool testPanelLayoutStateRoundTripAndClamping()
{
    dawhermes::core::MainPanelLayoutState state;
    state.leftColumnRatio = 0.95;
    state.rightColumnRatio = 0.90;
    state.topRowRatio = -1.0;

    const auto sanitized = dawhermes::core::sanitizeMainPanelLayoutState(state);
    EXPECT_TRUE(sanitized.leftColumnRatio >= 0.12);
    EXPECT_TRUE(sanitized.rightColumnRatio >= 0.12);
    EXPECT_TRUE((sanitized.leftColumnRatio + sanitized.rightColumnRatio) <= 0.80 + 1e-9);
    EXPECT_TRUE(sanitized.topRowRatio >= 0.35);

    const auto serialized = dawhermes::core::serializeMainPanelLayoutState(state);
    dawhermes::core::MainPanelLayoutState restored;
    EXPECT_TRUE(dawhermes::core::deserializeMainPanelLayoutState(serialized, restored));
    EXPECT_TRUE(std::abs(restored.leftColumnRatio - sanitized.leftColumnRatio) < 0.000001);
    EXPECT_TRUE(std::abs(restored.rightColumnRatio - sanitized.rightColumnRatio) < 0.000001);
    EXPECT_TRUE(std::abs(restored.topRowRatio - sanitized.topRowRatio) < 0.000001);

    dawhermes::core::MainPanelLayoutState invalid;
    EXPECT_TRUE(!dawhermes::core::deserializeMainPanelLayoutState("invalid-state", invalid));
    return true;
}

bool testTimelineViewportMappingAndZoom()
{
    dawhermes::core::TimelineViewportState viewport;
    viewport.startBeat = 4.0;
    viewport.visibleBeats = 8.0;
    viewport = dawhermes::core::sanitizeTimelineViewportState(viewport);

    EXPECT_TRUE(std::abs(dawhermes::core::timelineBeatToX(4.0, 800, viewport) - 0.0) < 0.0001);
    EXPECT_TRUE(std::abs(dawhermes::core::timelineBeatToX(12.0, 800, viewport) - 800.0) < 0.0001);
    EXPECT_TRUE(std::abs(dawhermes::core::timelineXToBeat(400.0, 800, viewport) - 8.0) < 0.0001);

    const auto zoomed = dawhermes::core::zoomTimelineViewport(viewport, 8.0, 2.0, 20.0);
    EXPECT_TRUE(std::abs(zoomed.visibleBeats - 4.0) < 0.0001);
    EXPECT_TRUE(std::abs(zoomed.startBeat - 6.0) < 0.0001);

    const auto scrolled = dawhermes::core::scrollTimelineViewport(zoomed, 3.0, 20.0);
    EXPECT_TRUE(std::abs(scrolled.startBeat - 9.0) < 0.0001);
    return true;
}

bool testMidiTimeMapBarAndGridResolution()
{
    std::vector<dawhermes::core::MidiTimeSignatureEvent> signatures {
        dawhermes::core::MidiTimeSignatureEvent { 0.0, 4, 4 },
        dawhermes::core::MidiTimeSignatureEvent { 16.0, 3, 4 },
    };

    signatures = dawhermes::core::sanitizeTimeSignatureMap(signatures);

    EXPECT_TRUE(std::abs(dawhermes::core::beatsPerBarAt(2.0, signatures) - 4.0) < 0.0001);
    EXPECT_TRUE(std::abs(dawhermes::core::beatsPerBarAt(18.0, signatures) - 3.0) < 0.0001);
    EXPECT_EQ(dawhermes::core::barNumberAt(0.0, signatures), 1);
    EXPECT_EQ(dawhermes::core::barNumberAt(15.9, signatures), 4);
    EXPECT_EQ(dawhermes::core::barNumberAt(16.0, signatures), 5);

    const auto bars = dawhermes::core::buildBarStartBeats(0.0, 22.0, signatures, 32);
    EXPECT_TRUE(!bars.empty());
    EXPECT_TRUE(std::find(bars.begin(), bars.end(), 16.0) != bars.end());
    EXPECT_TRUE(std::find(bars.begin(), bars.end(), 19.0) != bars.end());

    EXPECT_TRUE(std::abs(dawhermes::core::gridStepBeats(16) - 0.25) < 0.0001);
    const auto grid = dawhermes::core::buildGridBeatPositions(0.0, 1.0, 16, 32);
    EXPECT_TRUE(grid.size() >= 5);
    EXPECT_TRUE(std::abs(grid.front() - 0.0) < 0.0001);
    return true;
}

bool testTimelineLaneGeometryAndVisibleNoteCulling()
{
    ProjectModel project;
    const auto audio = project.addTrack(TrackType::audio, "Audio 1");
    const auto midi = project.addTrack(TrackType::midi, "MIDI 1");
    EXPECT_TRUE(audio.id > 0);

    auto* midiTrack = project.findTrackById(midi.id);
    EXPECT_TRUE(midiTrack != nullptr);
    midiTrack->midiNotes = {
        makeMidiNote(60, 100, 1.0, 1.0),
        makeMidiNote(72, 100, 12.0, 1.0),
    };

    const auto lanes = dawhermes::core::buildTimelineLaneGeometry(project.tracks(), 30, 10);
    EXPECT_EQ(lanes.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(lanes[0].trackId, audio.id);
    EXPECT_EQ(lanes[1].trackId, midi.id);
    EXPECT_EQ(lanes[0].y, 10);
    EXPECT_EQ(lanes[1].y, 40);

    dawhermes::core::TimelineViewportState horizontal;
    horizontal.startBeat = 0.0;
    horizontal.visibleBeats = 8.0;

    dawhermes::core::PitchViewportState vertical;
    vertical.highestVisiblePitch = 84.0;
    vertical.visiblePitchSpan = 24.0;

    const auto geometry = dawhermes::core::computeVisibleNoteGeometry(
        midiTrack->midiNotes,
        800,
        240,
        horizontal,
        vertical,
        64);

    EXPECT_EQ(geometry.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(geometry.front().noteIndex, static_cast<std::size_t>(0));

    const auto y = dawhermes::core::pitchToY(72.0, 240, vertical);
    const auto pitchRoundTrip = dawhermes::core::yToPitch(y, 240, vertical);
    EXPECT_TRUE(std::abs(pitchRoundTrip - 72.0) < 0.001);
    return true;
}

bool testMidiComparisonToleranceClassification()
{
    const std::vector<MidiNote> source {
        makeMidiNote(60, 100, 0.0, 1.0),
        makeMidiNote(64, 90, 2.0, 1.0),
        makeMidiNote(70, 80, 6.0, 1.0),
    };

    const std::vector<MidiNote> target {
        makeMidiNote(60, 102, 0.01, 1.0),
        makeMidiNote(64, 90, 2.07, 0.90),
        makeMidiNote(67, 100, 4.0, 1.0),
    };

    const auto result = dawhermes::core::compareMidiNotes(source, target);
    const auto summary = dawhermes::core::summarizeMidiComparison(result);

    EXPECT_EQ(summary.unchangedCount, static_cast<std::size_t>(1));
    EXPECT_EQ(summary.timingAdjustedCount, static_cast<std::size_t>(1));
    EXPECT_EQ(summary.pitchChangedCount, static_cast<std::size_t>(0));
    EXPECT_EQ(summary.addedCount, static_cast<std::size_t>(1));
    EXPECT_EQ(summary.removedCount, static_cast<std::size_t>(1));
    return true;
}

bool testMidiNoteSelectionStateUsesStableIds()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Editable").id;
    const auto audioId = project.addTrack(TrackType::audio, "Audio").id;
    const auto groupId = project.addTrack(TrackType::group, "Group").id;

    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        {
            makeMidiNote(67, 100, 2.0, 0.5, 2),
            makeMidiNote(60, 100, 0.0, 0.5, 2),
            makeMidiNote(64, 100, 1.0, 0.5, 2),
        }));

    auto* track = project.findTrackById(midiId);
    EXPECT_TRUE(track != nullptr);
    const auto firstId = track->midiNotes.at(0).id;
    const auto secondId = track->midiNotes.at(1).id;

    dawhermes::core::MidiNoteSelectionState selection;
    selection.selectSingle(midiId, firstId);
    EXPECT_TRUE(selection.hasSelection());
    EXPECT_TRUE(selection.isSelected(firstId));
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(1));

    selection.toggleNote(midiId, secondId);
    EXPECT_TRUE(selection.isSelected(firstId));
    EXPECT_TRUE(selection.isSelected(secondId));
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(2));

    selection.toggleNote(midiId, secondId);
    EXPECT_TRUE(selection.isSelected(firstId));
    EXPECT_TRUE(!selection.isSelected(secondId));
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(1));

    selection.clearSelection();
    EXPECT_TRUE(!selection.hasSelection());

    selection.selectSingle(midiId, firstId);
    dawhermes::core::sortMidiNotesByStart(track->midiNotes);
    EXPECT_TRUE(selection.isSelected(firstId));
    EXPECT_TRUE(dawhermes::core::findNoteIndexById(track->midiNotes, firstId).has_value());

    selection.setActiveTrack(audioId);
    EXPECT_TRUE(!selection.hasSelection());

    selection.selectSingle(midiId, firstId);
    selection.setActiveTrack(groupId);
    EXPECT_TRUE(!selection.hasSelection());
    return true;
}

bool testMidiNoteMarqueeSelectionGeometry()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;

    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(60, 100, 4.25, 0.50, 1),
            makeMidiNote(64, 100, 5.00, 0.50, 1),
            makeMidiNote(72, 100, 10.00, 0.50, 1),
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { makeMidiNote(60, 100, 4.25, 0.50, 1) }));

    auto* primary = project.findTrackById(primaryId);
    const auto* comparison = project.findTrackById(comparisonId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_TRUE(comparison != nullptr);

    dawhermes::core::TimelineViewportState horizontal;
    horizontal.startBeat = 4.0;
    horizontal.visibleBeats = 4.0;

    dawhermes::core::PitchViewportState vertical;
    vertical.highestVisiblePitch = 72.0;
    vertical.visiblePitchSpan = 24.0;

    const auto beatA = dawhermes::core::timelineXToBeat(40.0, 800, horizontal);
    const auto beatB = dawhermes::core::timelineXToBeat(260.0, 800, horizontal);
    const auto pitchA = dawhermes::core::yToPitch(120.0, 240, vertical);
    const auto pitchB = dawhermes::core::yToPitch(70.0, 240, vertical);

    dawhermes::core::MidiNoteMarqueeSelectionRequest request;
    request.startBeat = beatA;
    request.endBeat = beatB;
    request.lowPitch = std::floor(std::min(pitchA, pitchB));
    request.highPitch = std::ceil(std::max(pitchA, pitchB));

    const auto ids = dawhermes::core::findMidiNotesIntersectingRange(primary->midiNotes, request);
    EXPECT_EQ(ids.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(containsNoteId(ids, primary->midiNotes.at(0).id));
    EXPECT_TRUE(containsNoteId(ids, primary->midiNotes.at(1).id));

    dawhermes::core::MidiNoteSelectionState selection;
    selection.selectSingle(primaryId, primary->midiNotes.at(2).id);
    selection.setSelection(primaryId, ids, std::nullopt);
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(2));
    EXPECT_TRUE(!selection.isSelected(primary->midiNotes.at(2).id));

    auto additiveIds = selection.selectedNoteIds();
    additiveIds.push_back(primary->midiNotes.at(2).id);
    selection.setSelection(primaryId, additiveIds, std::nullopt);
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(3));

    dawhermes::core::MidiNoteMarqueeSelectionRequest partial;
    partial.startBeat = 4.70;
    partial.endBeat = 4.80;
    partial.lowPitch = 60.0;
    partial.highPitch = 61.0;
    const auto partialIds = dawhermes::core::findMidiNotesIntersectingRange(primary->midiNotes, partial);
    EXPECT_EQ(partialIds.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(partialIds.front(), primary->midiNotes.at(0).id);

    const auto ghostIds = dawhermes::core::findMidiNotesIntersectingRange(comparison->midiNotes, request);
    EXPECT_EQ(ghostIds.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(!containsNoteId(ids, comparison->midiNotes.front().id));

    EXPECT_TRUE(!dawhermes::core::isMeaningfulMarqueeDrag(1.0, 1.0));
    EXPECT_TRUE(dawhermes::core::isMeaningfulMarqueeDrag(8.0, 0.0));

    selection.clearSelection();
    EXPECT_TRUE(!selection.hasSelection());
    return true;
}

bool testMidiNoteCreationDefaultsAndHistory()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Editable").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        {
            makeMidiNote(60, 100, 0.0, 0.25, 2),
            makeMidiNote(64, 100, 1.0, 0.25, 3),
            makeMidiNote(67, 100, 2.0, 0.25, 3),
        }));

    auto* track = project.findTrackById(midiId);
    EXPECT_TRUE(track != nullptr);
    const auto initialIds = noteIdsFor(track->midiNotes);

    dawhermes::core::MidiNoteCreationRequest request;
    request.clickedBeat = 1.13;
    request.clickedPitch = 61;
    request.defaultVelocity = 100;
    request.defaultChannel = dawhermes::core::mostCommonMidiChannel(track->midiNotes);
    request.gridStepBeats = dawhermes::core::gridStepBeats(16);

    auto note = dawhermes::core::makeCreatedMidiNote(request);
    note.id = project.allocateMidiNoteId();
    EXPECT_EQ(note.pitch, 61);
    EXPECT_TRUE(std::abs(note.startBeat - 1.25) < 0.0001);
    EXPECT_TRUE(std::abs(note.durationBeats - 0.25) < 0.0001);
    EXPECT_EQ(note.velocity, 100);
    EXPECT_EQ(note.channel, 3);
    EXPECT_TRUE(!containsNoteId(initialIds, note.id));

    dawhermes::core::ProjectHistory history;
    auto createCommand = std::make_unique<TestCreateMidiNoteCommand>(project, midiId, note);
    EXPECT_TRUE(createCommand->redo());
    history.pushExecuted(std::move(createCommand));
    EXPECT_EQ(history.size(), static_cast<std::size_t>(1));

    track = project.findTrackById(midiId);
    EXPECT_TRUE(track != nullptr);
    EXPECT_EQ(track->midiNotes.size(), static_cast<std::size_t>(4));
    EXPECT_TRUE(containsNoteId(noteIdsFor(track->midiNotes), note.id));

    EXPECT_TRUE(history.undo());
    track = project.findTrackById(midiId);
    EXPECT_TRUE(track != nullptr);
    EXPECT_EQ(track->midiNotes.size(), static_cast<std::size_t>(3));
    EXPECT_TRUE(!containsNoteId(noteIdsFor(track->midiNotes), note.id));

    EXPECT_TRUE(history.redo());
    track = project.findTrackById(midiId);
    EXPECT_TRUE(track != nullptr);
    EXPECT_EQ(track->midiNotes.size(), static_cast<std::size_t>(4));
    EXPECT_TRUE(containsNoteId(noteIdsFor(track->midiNotes), note.id));

    const auto emptyId = project.addTrack(TrackType::midi, "Empty").id;
    auto* emptyTrack = project.findTrackById(emptyId);
    EXPECT_TRUE(emptyTrack != nullptr);
    dawhermes::core::MidiNoteCreationRequest emptyRequest;
    emptyRequest.clickedBeat = -2.0;
    emptyRequest.clickedPitch = 200;
    emptyRequest.defaultChannel = dawhermes::core::mostCommonMidiChannel(emptyTrack->midiNotes);
    emptyRequest.gridStepBeats = 0.01;
    const auto emptyNote = dawhermes::core::makeCreatedMidiNote(emptyRequest);
    EXPECT_EQ(emptyNote.channel, 1);
    EXPECT_EQ(emptyNote.pitch, 127);
    EXPECT_TRUE(std::abs(emptyNote.startBeat - 0.0) < 0.0001);
    EXPECT_TRUE(std::abs(emptyNote.durationBeats - dawhermes::core::kMinimumMidiNoteDurationBeats) < 0.0001);
    return true;
}

bool testMidiNoteDeletionHistoryAndSelectionCleanup()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(60, 100, 0.0, 0.5, 1),
            makeMidiNote(64, 100, 1.0, 0.5, 1),
            makeMidiNote(67, 100, 2.0, 0.5, 1),
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { makeMidiNote(72, 100, 0.0, 0.5, 1) }));

    auto* primary = project.findTrackById(primaryId);
    const auto* comparison = project.findTrackById(comparisonId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_TRUE(comparison != nullptr);
    const auto selectedId = primary->midiNotes.at(1).id;
    const auto retainedId = primary->midiNotes.at(0).id;
    const auto comparisonIdNote = comparison->midiNotes.front().id;

    dawhermes::core::MidiNoteSelectionState selection;
    selection.selectSingle(primaryId, selectedId);

    std::vector<MidiNote> deletedNotes;
    for (const auto& note : primary->midiNotes) {
        if (selection.isSelected(note.id)) {
            deletedNotes.push_back(note);
        }
    }

    dawhermes::core::ProjectHistory history;
    auto deleteCommand = std::make_unique<TestDeleteMidiNotesCommand>(
        project,
        primaryId,
        selection.selectedNoteIds(),
        deletedNotes);
    EXPECT_TRUE(deleteCommand->redo());
    history.pushExecuted(std::move(deleteCommand));
    selection.removeDeletedNotes(project.findTrackById(primaryId)->midiNotes);

    primary = project.findTrackById(primaryId);
    comparison = project.findTrackById(comparisonId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_TRUE(comparison != nullptr);
    EXPECT_EQ(primary->midiNotes.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(!containsNoteId(noteIdsFor(primary->midiNotes), selectedId));
    EXPECT_TRUE(containsNoteId(noteIdsFor(primary->midiNotes), retainedId));
    EXPECT_TRUE(containsNoteId(noteIdsFor(comparison->midiNotes), comparisonIdNote));
    EXPECT_TRUE(!selection.hasSelection());
    EXPECT_EQ(history.size(), static_cast<std::size_t>(1));

    EXPECT_TRUE(history.undo());
    primary = project.findTrackById(primaryId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_EQ(primary->midiNotes.size(), static_cast<std::size_t>(3));
    EXPECT_TRUE(containsNoteId(noteIdsFor(primary->midiNotes), selectedId));

    EXPECT_TRUE(history.redo());
    primary = project.findTrackById(primaryId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_EQ(primary->midiNotes.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(!containsNoteId(noteIdsFor(primary->midiNotes), selectedId));

    std::vector<MidiNote> scratch = primary->midiNotes;
    EXPECT_EQ(dawhermes::core::deleteSelectedNotes(scratch, {}), static_cast<std::size_t>(0));
    EXPECT_EQ(scratch.size(), primary->midiNotes.size());
    return true;
}

bool testMidiEditingRefreshRegressions()
{
    ProjectModel project;
    const auto audioId = project.addTrack(TrackType::audio, "Audio").id;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto candidateId = project.addTrack(TrackType::midi, "Candidate").id;
    EXPECT_TRUE(project.setAudioSourcePath(audioId, "C:/tmp/source.wav"));
    EXPECT_TRUE(project.replaceMidiNotes(primaryId, { makeMidiNote(60, 100, 0.0, 0.5, 1) }));
    EXPECT_TRUE(project.replaceMidiNotes(candidateId, { makeMidiNote(60, 100, 0.0, 0.5, 1) }));

    auto* primary = project.findTrackById(primaryId);
    const auto* candidate = project.findTrackById(candidateId);
    const auto* audio = project.findTrackById(audioId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_TRUE(candidate != nullptr);
    EXPECT_TRUE(audio != nullptr);
    const auto sourcePath = audio->audioSourcePath;
    const auto originalComparison = dawhermes::core::summarizeMidiComparison(
        dawhermes::core::compareMidiNotes(primary->midiNotes, candidate->midiNotes));
    EXPECT_EQ(originalComparison.unchangedCount, static_cast<std::size_t>(1));

    MidiNote inserted = makeMidiNote(67, 100, 3.0, 0.5, 1);
    inserted.id = project.allocateMidiNoteId();
    primary->midiNotes.push_back(inserted);
    dawhermes::core::sortMidiNotesByStart(primary->midiNotes);

    const auto updatedComparison = dawhermes::core::summarizeMidiComparison(
        dawhermes::core::compareMidiNotes(primary->midiNotes, candidate->midiNotes));
    EXPECT_EQ(updatedComparison.unchangedCount, static_cast<std::size_t>(1));
    EXPECT_EQ(updatedComparison.removedCount, static_cast<std::size_t>(1));

    dawhermes::core::TimelineViewportState horizontal;
    horizontal.startBeat = 2.0;
    horizontal.visibleBeats = 4.0;
    dawhermes::core::PitchViewportState vertical;
    vertical.highestVisiblePitch = 80.0;
    vertical.visiblePitchSpan = 24.0;
    const auto geometry = dawhermes::core::computeVisibleNoteGeometry(
        primary->midiNotes,
        800,
        240,
        horizontal,
        vertical,
        64);
    EXPECT_EQ(geometry.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(primary->midiNotes.size(), static_cast<std::size_t>(2));

    const auto lanes = dawhermes::core::buildTimelineLaneGeometry(project.tracks(), 30);
    EXPECT_EQ(lanes.size(), static_cast<std::size_t>(3));

    audio = project.findTrackById(audioId);
    EXPECT_TRUE(audio != nullptr);
    EXPECT_EQ(audio->audioSourcePath, sourcePath);
    EXPECT_TRUE(!containsNoteId(noteIdsFor(candidate->midiNotes), inserted.id));
    return true;
}

bool testMidiNoteMouseMoveSnapClampAndHistory()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(60, 91, 1.0, 0.5, 2),
            makeMidiNote(64, 92, 2.0, 0.25, 3),
            makeMidiNote(67, 93, 3.0, 0.75, 4),
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { makeMidiNote(60, 100, 1.0, 0.5, 1) }));

    auto* primary = project.findTrackById(primaryId);
    const auto* comparison = project.findTrackById(comparisonId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_TRUE(comparison != nullptr);
    const auto comparisonBefore = comparison->midiNotes;
    const auto firstId = primary->midiNotes.at(0).id;
    const auto beforeNotes = primary->midiNotes;

    dawhermes::core::MoveSelectedNotesRequest move;
    move.selectedNoteIds = { firstId };
    move.requestedDeltaBeats = 0.37;
    move.requestedDeltaSemitones = 2;
    move.gridStepBeats = dawhermes::core::gridStepBeats(16);
    const auto result = dawhermes::core::moveSelectedNotes(primary->midiNotes, move);
    EXPECT_TRUE(result.changed);
    EXPECT_TRUE(std::abs(result.appliedDeltaBeats - 0.25) < 0.0001);
    EXPECT_EQ(result.appliedDeltaSemitones, 2);

    const auto movedIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, firstId);
    EXPECT_TRUE(movedIndex.has_value());
    const auto& moved = primary->midiNotes.at(movedIndex.value());
    EXPECT_TRUE(std::abs(moved.startBeat - 1.25) < 0.0001);
    EXPECT_EQ(moved.pitch, 62);
    EXPECT_TRUE(std::abs(moved.durationBeats - 0.5) < 0.0001);
    EXPECT_EQ(moved.velocity, 91);
    EXPECT_EQ(moved.channel, 2);
    EXPECT_EQ(project.findTrackById(comparisonId)->midiNotes, comparisonBefore);

    dawhermes::core::ProjectHistory history;
    history.pushExecuted(std::make_unique<TestReplaceMidiNotesCommand>(
        project,
        primaryId,
        "Move MIDI Notes",
        beforeNotes,
        primary->midiNotes));
    EXPECT_EQ(history.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, beforeNotes);
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, primary->midiNotes);

    dawhermes::core::ProjectHistory canceledHistory;
    auto canceledScratch = beforeNotes;
    EXPECT_TRUE(!dawhermes::core::moveSelectedNotes(canceledScratch, dawhermes::core::MoveSelectedNotesRequest { { firstId }, 0.0, 0 }).changed);
    EXPECT_EQ(canceledHistory.size(), static_cast<std::size_t>(0));
    return true;
}

bool testMidiNoteMultiMoveOffsetsClampsAndClickedSelection()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(1, 100, 0.1, 0.5, 1),
            makeMidiNote(7, 100, 1.1, 0.5, 1),
            makeMidiNote(80, 100, 3.0, 0.5, 1),
        }));

    auto* primary = project.findTrackById(primaryId);
    EXPECT_TRUE(primary != nullptr);
    const auto lowId = primary->midiNotes.at(0).id;
    const auto highId = primary->midiNotes.at(1).id;
    const auto unselectedId = primary->midiNotes.at(2).id;

    dawhermes::core::MoveSelectedNotesRequest clampLow;
    clampLow.selectedNoteIds = { lowId, highId };
    clampLow.requestedDeltaBeats = -5.0;
    clampLow.requestedDeltaSemitones = -5;
    clampLow.gridStepBeats = dawhermes::core::gridStepBeats(16);
    const auto clampLowResult = dawhermes::core::moveSelectedNotes(primary->midiNotes, clampLow);
    EXPECT_TRUE(clampLowResult.changed);
    EXPECT_TRUE(std::abs(clampLowResult.appliedDeltaBeats + 0.1) < 0.0001);
    EXPECT_EQ(clampLowResult.appliedDeltaSemitones, -1);

    const auto lowIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, lowId).value();
    const auto highIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, highId).value();
    EXPECT_TRUE(std::abs(primary->midiNotes.at(lowIndex).startBeat - 0.0) < 0.0001);
    EXPECT_TRUE(std::abs(primary->midiNotes.at(highIndex).startBeat - 1.0) < 0.0001);
    EXPECT_EQ(primary->midiNotes.at(lowIndex).pitch, 0);
    EXPECT_EQ(primary->midiNotes.at(highIndex).pitch, 6);
    EXPECT_TRUE(std::abs((primary->midiNotes.at(highIndex).startBeat - primary->midiNotes.at(lowIndex).startBeat) - 1.0) < 0.0001);
    EXPECT_EQ(primary->midiNotes.at(highIndex).pitch - primary->midiNotes.at(lowIndex).pitch, 6);

    primary->midiNotes = {
        MidiNote { 120, 100, 0.0, 0.5, 1, lowId },
        MidiNote { 126, 100, 1.0, 0.5, 1, highId },
        MidiNote { 80, 100, 3.0, 0.5, 1, unselectedId },
    };
    dawhermes::core::MoveSelectedNotesRequest clampHigh;
    clampHigh.selectedNoteIds = { lowId, highId };
    clampHigh.requestedDeltaSemitones = 20;
    const auto clampHighResult = dawhermes::core::moveSelectedNotes(primary->midiNotes, clampHigh);
    EXPECT_TRUE(clampHighResult.changed);
    EXPECT_EQ(clampHighResult.appliedDeltaSemitones, 1);

    dawhermes::core::MidiNoteSelectionState selection;
    selection.setSelection(primaryId, { lowId, highId }, std::nullopt);
    selection.selectSingle(primaryId, unselectedId);
    EXPECT_EQ(selection.selectionCount(), static_cast<std::size_t>(1));
    EXPECT_TRUE(selection.isSelected(unselectedId));
    EXPECT_TRUE(!selection.isSelected(lowId));
    return true;
}

bool testMidiNoteMoveSnapDenominators()
{
    const std::vector<std::pair<int, double>> cases {
        { 4, 0.0 },
        { 8, 0.5 },
        { 16, 0.5 },
        { 32, 0.5 },
    };

    for (const auto& [denominator, expectedStart] : cases) {
        std::vector<MidiNote> notes { MidiNote { 60, 100, 0.0, 0.25, 1, 42 } };
        dawhermes::core::MoveSelectedNotesRequest request;
        request.selectedNoteIds = { 42 };
        request.requestedDeltaBeats = 0.49;
        request.gridStepBeats = dawhermes::core::gridStepBeats(denominator);
        const auto result = dawhermes::core::moveSelectedNotes(notes, request);
        EXPECT_TRUE(result.changed || expectedStart == 0.0);
        EXPECT_TRUE(std::abs(notes.front().startBeat - expectedStart) < 0.0001);
    }

    return true;
}

bool testMidiNoteResizeSnapClampAndHistory()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(60, 100, 1.0, 1.0, 1),
            makeMidiNote(64, 100, 2.0, 0.5, 1),
            makeMidiNote(67, 100, 4.0, 0.25, 1),
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { makeMidiNote(60, 100, 1.0, 1.0, 1) }));

    auto* primary = project.findTrackById(primaryId);
    EXPECT_TRUE(primary != nullptr);
    const auto comparisonBefore = project.findTrackById(comparisonId)->midiNotes;
    const auto anchorId = primary->midiNotes.at(0).id;
    const auto groupId = primary->midiNotes.at(1).id;
    const auto beforeNotes = primary->midiNotes;

    EXPECT_TRUE(dawhermes::core::isMidiNoteRightEdgeHit(10.0, 100.0, 108.0));
    EXPECT_TRUE(!dawhermes::core::isMidiNoteRightEdgeHit(10.0, 100.0, 50.0));

    dawhermes::core::ResizeSelectedNotesRequest resize;
    resize.selectedNoteIds = { anchorId, groupId };
    resize.anchorNoteId = anchorId;
    resize.requestedAnchorEndBeat = 2.37;
    resize.gridStepBeats = dawhermes::core::gridStepBeats(16);
    const auto result = dawhermes::core::resizeSelectedNotes(primary->midiNotes, resize);
    EXPECT_TRUE(result.changed);
    EXPECT_TRUE(std::abs(result.appliedDurationDeltaBeats - 0.25) < 0.0001);

    const auto anchorIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, anchorId).value();
    const auto groupIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, groupId).value();
    EXPECT_TRUE(std::abs(primary->midiNotes.at(anchorIndex).startBeat - 1.0) < 0.0001);
    EXPECT_TRUE(std::abs(primary->midiNotes.at(anchorIndex).durationBeats - 1.25) < 0.0001);
    EXPECT_TRUE(std::abs(primary->midiNotes.at(groupIndex).durationBeats - 0.75) < 0.0001);
    EXPECT_EQ(primary->midiNotes.at(anchorIndex).pitch, 60);
    EXPECT_EQ(primary->midiNotes.at(anchorIndex).velocity, 100);
    EXPECT_EQ(project.findTrackById(comparisonId)->midiNotes, comparisonBefore);

    dawhermes::core::ProjectHistory history;
    history.pushExecuted(std::make_unique<TestReplaceMidiNotesCommand>(
        project,
        primaryId,
        "Resize MIDI Notes",
        beforeNotes,
        primary->midiNotes));
    EXPECT_EQ(history.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, beforeNotes);
    EXPECT_TRUE(history.redo());
    EXPECT_TRUE(std::abs(project.findTrackById(primaryId)->midiNotes.at(anchorIndex).durationBeats - 1.25) < 0.0001);

    auto scratch = beforeNotes;
    dawhermes::core::ResizeSelectedNotesRequest minResize;
    minResize.selectedNoteIds = { anchorId };
    minResize.anchorNoteId = anchorId;
    minResize.requestedAnchorEndBeat = 0.0;
    minResize.gridStepBeats = dawhermes::core::gridStepBeats(32);
    const auto minResult = dawhermes::core::resizeSelectedNotes(scratch, minResize);
    EXPECT_TRUE(minResult.changed);
    EXPECT_TRUE(std::abs(scratch.at(0).startBeat - 1.0) < 0.0001);
    EXPECT_TRUE(std::abs(scratch.at(0).durationBeats - dawhermes::core::kMinimumMidiNoteDurationBeats) < 0.0001);

    dawhermes::core::ProjectHistory canceledHistory;
    auto noOpScratch = beforeNotes;
    dawhermes::core::ResizeSelectedNotesRequest noOp;
    noOp.selectedNoteIds = { anchorId };
    noOp.anchorNoteId = anchorId;
    noOp.requestedAnchorEndBeat = 2.0;
    EXPECT_TRUE(!dawhermes::core::resizeSelectedNotes(noOpScratch, noOp).changed);
    EXPECT_EQ(canceledHistory.size(), static_cast<std::size_t>(0));
    return true;
}

bool testMidiNoteKeyboardNudgeBehaviour()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(60, 100, 1.0, 0.5, 1),
            makeMidiNote(72, 100, 2.0, 0.5, 1),
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { makeMidiNote(84, 100, 1.0, 0.5, 1) }));

    auto* primary = project.findTrackById(primaryId);
    EXPECT_TRUE(primary != nullptr);
    const auto comparisonBefore = project.findTrackById(comparisonId)->midiNotes;
    const auto firstId = primary->midiNotes.at(0).id;
    const auto secondId = primary->midiNotes.at(1).id;

    dawhermes::core::ProjectHistory history;
    const auto pushMove = [&](double deltaBeats, int deltaSemitones, double gridStep) {
        const auto before = primary->midiNotes;
        dawhermes::core::MoveSelectedNotesRequest request;
        request.selectedNoteIds = { firstId, secondId };
        request.requestedDeltaBeats = deltaBeats;
        request.requestedDeltaSemitones = deltaSemitones;
        request.gridStepBeats = gridStep;
        const auto result = dawhermes::core::moveSelectedNotes(primary->midiNotes, request);
        if (result.changed) {
            history.pushExecuted(std::make_unique<TestReplaceMidiNotesCommand>(
                project,
                primaryId,
                "Move MIDI Notes",
                before,
                primary->midiNotes));
        }
    };

    pushMove(dawhermes::core::gridStepBeats(8), 0, dawhermes::core::gridStepBeats(8));
    EXPECT_TRUE(std::abs(primary->midiNotes.at(0).startBeat - 1.5) < 0.0001);
    pushMove(-dawhermes::core::gridStepBeats(8), 0, dawhermes::core::gridStepBeats(8));
    EXPECT_TRUE(std::abs(primary->midiNotes.at(0).startBeat - 1.0) < 0.0001);
    pushMove(0.0, 1, dawhermes::core::gridStepBeats(16));
    EXPECT_EQ(primary->midiNotes.at(0).pitch, 61);
    pushMove(0.0, -1, dawhermes::core::gridStepBeats(16));
    EXPECT_EQ(primary->midiNotes.at(0).pitch, 60);
    pushMove(0.0, 12, dawhermes::core::gridStepBeats(16));
    EXPECT_EQ(primary->midiNotes.at(0).pitch, 72);

    EXPECT_EQ(history.size(), static_cast<std::size_t>(5));
    EXPECT_EQ(project.findTrackById(comparisonId)->midiNotes, comparisonBefore);

    dawhermes::core::MoveSelectedNotesRequest clamp;
    clamp.selectedNoteIds = { firstId, secondId };
    clamp.requestedDeltaBeats = -100.0;
    clamp.requestedDeltaSemitones = 100;
    const auto clampResult = dawhermes::core::moveSelectedNotes(primary->midiNotes, clamp);
    EXPECT_TRUE(clampResult.changed);
    EXPECT_TRUE(primary->midiNotes.at(0).startBeat >= 0.0);
    EXPECT_TRUE(primary->midiNotes.at(1).pitch <= 127);

    std::vector<MidiNote> noSelectionNotes = primary->midiNotes;
    dawhermes::core::MoveSelectedNotesRequest none;
    none.requestedDeltaBeats = 1.0;
    none.requestedDeltaSemitones = 1;
    EXPECT_TRUE(!dawhermes::core::moveSelectedNotes(noSelectionNotes, none).changed);
    EXPECT_EQ(noSelectionNotes, primary->midiNotes);
    return true;
}

bool testMidiSnapControlEditingGridBehaviour()
{
    dawhermes::core::MidiNoteCreationRequest createSnapped;
    createSnapped.clickedBeat = 1.13;
    createSnapped.clickedPitch = 60;
    createSnapped.snapEnabled = true;
    createSnapped.gridStepBeats = dawhermes::core::gridStepBeats(16);
    const auto snappedNote = dawhermes::core::makeCreatedMidiNote(createSnapped);
    EXPECT_TRUE(std::abs(snappedNote.startBeat - 1.25) < 0.0001);

    auto unsnappedCreate = createSnapped;
    unsnappedCreate.snapEnabled = false;
    const auto unsnappedNote = dawhermes::core::makeCreatedMidiNote(unsnappedCreate);
    EXPECT_TRUE(std::abs(unsnappedNote.startBeat - 1.13) < 0.0001);

    std::vector<MidiNote> snappedMoveNotes { MidiNote { 60, 100, 1.0, 0.5, 1, 11 } };
    dawhermes::core::MoveSelectedNotesRequest snappedMove;
    snappedMove.selectedNoteIds = { 11 };
    snappedMove.requestedDeltaBeats = 0.37;
    snappedMove.snapEnabled = true;
    snappedMove.gridStepBeats = dawhermes::core::gridStepBeats(16);
    const auto snappedMoveResult = dawhermes::core::moveSelectedNotes(snappedMoveNotes, snappedMove);
    EXPECT_TRUE(snappedMoveResult.changed);
    EXPECT_TRUE(std::abs(snappedMoveNotes.front().startBeat - 1.25) < 0.0001);

    std::vector<MidiNote> unsnappedMoveNotes { MidiNote { 60, 100, 1.0, 0.5, 1, 11 } };
    auto unsnappedMove = snappedMove;
    unsnappedMove.snapEnabled = false;
    const auto unsnappedMoveResult = dawhermes::core::moveSelectedNotes(unsnappedMoveNotes, unsnappedMove);
    EXPECT_TRUE(unsnappedMoveResult.changed);
    EXPECT_TRUE(std::abs(unsnappedMoveNotes.front().startBeat - 1.37) < 0.0001);

    std::vector<MidiNote> snappedResizeNotes { MidiNote { 60, 100, 1.0, 1.0, 1, 12 } };
    dawhermes::core::ResizeSelectedNotesRequest snappedResize;
    snappedResize.selectedNoteIds = { 12 };
    snappedResize.anchorNoteId = 12;
    snappedResize.requestedAnchorEndBeat = 2.37;
    snappedResize.snapEnabled = true;
    snappedResize.gridStepBeats = dawhermes::core::gridStepBeats(16);
    const auto snappedResizeResult = dawhermes::core::resizeSelectedNotes(snappedResizeNotes, snappedResize);
    EXPECT_TRUE(snappedResizeResult.changed);
    EXPECT_TRUE(std::abs(snappedResizeNotes.front().durationBeats - 1.25) < 0.0001);

    std::vector<MidiNote> unsnappedResizeNotes { MidiNote { 60, 100, 1.0, 1.0, 1, 12 } };
    auto unsnappedResize = snappedResize;
    unsnappedResize.snapEnabled = false;
    const auto unsnappedResizeResult = dawhermes::core::resizeSelectedNotes(unsnappedResizeNotes, unsnappedResize);
    EXPECT_TRUE(unsnappedResizeResult.changed);
    EXPECT_TRUE(std::abs(unsnappedResizeNotes.front().durationBeats - 1.37) < 0.0001);

    std::vector<MidiNote> keyboardNudgeNotes { MidiNote { 60, 100, 1.0, 0.5, 1, 13 } };
    dawhermes::core::MoveSelectedNotesRequest keyboardNudge;
    keyboardNudge.selectedNoteIds = { 13 };
    keyboardNudge.requestedDeltaBeats = dawhermes::core::gridStepBeats(8);
    keyboardNudge.snapEnabled = true;
    keyboardNudge.gridStepBeats = dawhermes::core::gridStepBeats(8);
    EXPECT_TRUE(dawhermes::core::moveSelectedNotes(keyboardNudgeNotes, keyboardNudge).changed);
    EXPECT_TRUE(std::abs(keyboardNudgeNotes.front().startBeat - 1.5) < 0.0001);

    const auto gridBefore = dawhermes::core::buildGridBeatPositions(0.0, 1.0, 16);
    bool snapEnabled = true;
    dawhermes::core::ProjectHistory history;
    snapEnabled = !snapEnabled;
    const auto gridAfter = dawhermes::core::buildGridBeatPositions(0.0, 1.0, 16);
    EXPECT_TRUE(!snapEnabled);
    EXPECT_EQ(gridBefore, gridAfter);
    EXPECT_EQ(history.size(), static_cast<std::size_t>(0));
    return true;
}

bool testMidiVelocityEditingBehaviour()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(60, 40, 0.0, 0.5, 1),
            makeMidiNote(64, 50, 1.0, 0.5, 2),
            makeMidiNote(67, 60, 2.0, 0.5, 3),
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { makeMidiNote(72, 99, 0.0, 0.5, 4) }));

    auto* primary = project.findTrackById(primaryId);
    const auto* comparison = project.findTrackById(comparisonId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_TRUE(comparison != nullptr);
    const auto comparisonBefore = comparison->midiNotes;
    const auto firstId = primary->midiNotes.at(0).id;
    const auto secondId = primary->midiNotes.at(1).id;
    const auto thirdId = primary->midiNotes.at(2).id;
    const auto beforeNotes = primary->midiNotes;

    dawhermes::core::MidiNoteSelectionState selection;
    EXPECT_EQ(dawhermes::core::applyVelocityToSelectedNotes(primary->midiNotes, selection.selectedNoteIds(), 88), static_cast<std::size_t>(0));
    EXPECT_EQ(primary->midiNotes, beforeNotes);

    selection.setSelection(primaryId, { firstId, secondId }, firstId);
    EXPECT_EQ(selection.primarySelectedNoteId().value(), firstId);
    EXPECT_EQ(primary->midiNotes.at(dawhermes::core::findNoteIndexById(primary->midiNotes, firstId).value()).velocity, 40);

    const auto changedCount = dawhermes::core::applyVelocityToSelectedNotes(
        primary->midiNotes,
        selection.selectedNoteIds(),
        200);
    EXPECT_EQ(changedCount, static_cast<std::size_t>(2));
    EXPECT_EQ(primary->midiNotes.at(dawhermes::core::findNoteIndexById(primary->midiNotes, firstId).value()).velocity, 127);
    EXPECT_EQ(primary->midiNotes.at(dawhermes::core::findNoteIndexById(primary->midiNotes, secondId).value()).velocity, 127);
    EXPECT_EQ(primary->midiNotes.at(dawhermes::core::findNoteIndexById(primary->midiNotes, thirdId).value()).velocity, 60);
    EXPECT_EQ(project.findTrackById(comparisonId)->midiNotes, comparisonBefore);

    const auto afterNotes = primary->midiNotes;
    dawhermes::core::ProjectHistory history;
    history.pushExecuted(std::make_unique<TestReplaceMidiNotesCommand>(
        project,
        primaryId,
        "Set MIDI Note Velocity",
        beforeNotes,
        afterNotes));
    EXPECT_EQ(history.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, beforeNotes);
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, afterNotes);

    EXPECT_EQ(dawhermes::core::applyVelocityToSelectedNotes(primary->midiNotes, { thirdId }, 0), static_cast<std::size_t>(1));
    EXPECT_EQ(primary->midiNotes.at(dawhermes::core::findNoteIndexById(primary->midiNotes, thirdId).value()).velocity, 1);

    std::vector<MidiNote> scratch = primary->midiNotes;
    EXPECT_EQ(dawhermes::core::deleteSelectedNotes(scratch, selection.selectedNoteIds()), static_cast<std::size_t>(2));
    selection.removeDeletedNotes(scratch);
    EXPECT_TRUE(!selection.hasSelection());
    return true;
}

bool testMidiQuantizeSelectedNotesBehaviour()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Primary").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            makeMidiNote(60, 80, 0.24, 0.5, 1),
            makeMidiNote(64, 90, 1.26, 0.75, 2),
            makeMidiNote(67, 100, 2.00, 0.25, 3),
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { makeMidiNote(60, 80, 0.24, 0.5, 1) }));

    auto* primary = project.findTrackById(primaryId);
    EXPECT_TRUE(primary != nullptr);
    const auto comparisonBefore = project.findTrackById(comparisonId)->midiNotes;
    const auto firstId = primary->midiNotes.at(0).id;
    const auto secondId = primary->midiNotes.at(1).id;
    const auto thirdId = primary->midiNotes.at(2).id;
    const auto beforeNotes = primary->midiNotes;

    std::vector<MidiNote> noSelection = primary->midiNotes;
    dawhermes::core::ProjectHistory noSelectionHistory;
    EXPECT_EQ(dawhermes::core::quantizeSelectedNoteStarts(noSelection, {}, dawhermes::core::gridStepBeats(16)), static_cast<std::size_t>(0));
    EXPECT_EQ(noSelection, primary->midiNotes);
    EXPECT_EQ(noSelectionHistory.size(), static_cast<std::size_t>(0));

    const std::vector<std::pair<int, double>> denominatorCases {
        { 4, 0.0 },
        { 8, 0.0 },
        { 16, 0.25 },
        { 32, 0.25 },
    };
    for (const auto& [denominator, expectedStart] : denominatorCases) {
        std::vector<MidiNote> notes { MidiNote { 60, 80, 0.24, 0.5, 1, 77 } };
        const auto changed = dawhermes::core::quantizeSelectedNoteStarts(
            notes,
            { 77 },
            dawhermes::core::gridStepBeats(denominator));
        EXPECT_EQ(changed, static_cast<std::size_t>(1));
        EXPECT_TRUE(std::abs(notes.front().startBeat - expectedStart) < 0.0001);
    }

    const auto changedCount = dawhermes::core::quantizeSelectedNoteStarts(
        primary->midiNotes,
        { firstId, secondId, thirdId },
        dawhermes::core::gridStepBeats(16));
    EXPECT_EQ(changedCount, static_cast<std::size_t>(2));
    const auto firstIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, firstId).value();
    const auto secondIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, secondId).value();
    const auto thirdIndex = dawhermes::core::findNoteIndexById(primary->midiNotes, thirdId).value();
    EXPECT_TRUE(std::abs(primary->midiNotes.at(firstIndex).startBeat - 0.25) < 0.0001);
    EXPECT_TRUE(std::abs(primary->midiNotes.at(secondIndex).startBeat - 1.25) < 0.0001);
    EXPECT_TRUE(std::abs(primary->midiNotes.at(thirdIndex).startBeat - 2.0) < 0.0001);
    EXPECT_TRUE(std::abs(primary->midiNotes.at(firstIndex).durationBeats - 0.5) < 0.0001);
    EXPECT_EQ(primary->midiNotes.at(firstIndex).pitch, 60);
    EXPECT_EQ(primary->midiNotes.at(secondIndex).velocity, 90);
    EXPECT_EQ(primary->midiNotes.at(secondIndex).channel, 2);
    EXPECT_EQ(project.findTrackById(comparisonId)->midiNotes, comparisonBefore);

    const auto afterNotes = primary->midiNotes;
    dawhermes::core::ProjectHistory history;
    history.pushExecuted(std::make_unique<TestReplaceMidiNotesCommand>(
        project,
        primaryId,
        "Quantize MIDI Notes",
        beforeNotes,
        afterNotes));
    EXPECT_EQ(history.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(history.undo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, beforeNotes);
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, afterNotes);

    EXPECT_EQ(dawhermes::core::quantizeSelectedNoteStarts(primary->midiNotes, { firstId, secondId }, dawhermes::core::gridStepBeats(16)), static_cast<std::size_t>(0));
    return true;
}

bool testMidiPlaybackEventGenerationFromEditedNotes()
{
    ProjectModel project;
    const auto trackId = project.addTrack(TrackType::midi, "Audition").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        trackId,
        {
            MidiNote { 62, 64, 0.50, 0.75, 9, 101 },
            MidiNote { 72, 127, -2.0, 0.25, 1, 102 },
        }));

    const auto* track = project.findTrackById(trackId);
    EXPECT_TRUE(track != nullptr);
    const auto notesBefore = track->midiNotes;
    const auto snapshotResult = dawhermes::audio::createMidiPlaybackSnapshot(*track);
    EXPECT_TRUE(snapshotResult.ok);
    EXPECT_EQ(snapshotResult.snapshot.events.size(), static_cast<std::size_t>(4));
    EXPECT_TRUE(approxEqual(snapshotResult.snapshot.durationSeconds, 0.625));
    EXPECT_EQ(project.findTrackById(trackId)->midiNotes, notesBefore);

    const auto movedOn = std::find_if(
        snapshotResult.snapshot.events.begin(),
        snapshotResult.snapshot.events.end(),
        [](const auto& event) {
            return event.kind == dawhermes::audio::MidiPlaybackEventKind::noteOn && event.pitch == 62;
        });
    EXPECT_TRUE(movedOn != snapshotResult.snapshot.events.end());
    EXPECT_TRUE(approxEqual(movedOn->timeSeconds, 0.25));
    EXPECT_TRUE(approxEqual(movedOn->amplitude, 64.0 / 127.0));

    const auto movedOff = std::find_if(
        snapshotResult.snapshot.events.begin(),
        snapshotResult.snapshot.events.end(),
        [](const auto& event) {
            return event.kind == dawhermes::audio::MidiPlaybackEventKind::noteOff && event.pitch == 62;
        });
    EXPECT_TRUE(movedOff != snapshotResult.snapshot.events.end());
    EXPECT_TRUE(approxEqual(movedOff->timeSeconds, 0.625));

    const auto clampedStart = std::find_if(
        snapshotResult.snapshot.events.begin(),
        snapshotResult.snapshot.events.end(),
        [](const auto& event) {
            return event.kind == dawhermes::audio::MidiPlaybackEventKind::noteOn && event.pitch == 72;
        });
    EXPECT_TRUE(clampedStart != snapshotResult.snapshot.events.end());
    EXPECT_TRUE(approxEqual(clampedStart->timeSeconds, 0.0));
    EXPECT_TRUE(approxEqual(clampedStart->amplitude, 1.0));

    dawhermes::core::Track audioTrack;
    audioTrack.type = TrackType::audio;
    audioTrack.midiNotes = track->midiNotes;
    EXPECT_TRUE(!dawhermes::audio::createMidiPlaybackSnapshot(audioTrack).ok);

    dawhermes::core::Track groupTrack;
    groupTrack.type = TrackType::group;
    groupTrack.midiNotes = track->midiNotes;
    EXPECT_TRUE(!dawhermes::audio::createMidiPlaybackSnapshot(groupTrack).ok);

    dawhermes::core::Track emptyMidiTrack;
    emptyMidiTrack.type = TrackType::midi;
    EXPECT_TRUE(!dawhermes::audio::createMidiPlaybackSnapshot(emptyMidiTrack).ok);
    return true;
}

bool testMidiPlaybackTempoTimingAndOrdering()
{
    const std::vector<dawhermes::core::MidiTempoEvent> fallbackTempo;
    EXPECT_TRUE(approxEqual(dawhermes::audio::midiBeatToSeconds(1.0, fallbackTempo), 0.5));
    EXPECT_TRUE(approxEqual(dawhermes::audio::midiBeatToSeconds(0.5, fallbackTempo), 0.25));
    EXPECT_TRUE(approxEqual(dawhermes::audio::midiBeatToSeconds(-4.0, fallbackTempo), 0.0));
    EXPECT_TRUE(approxEqual(dawhermes::audio::midiSecondsToBeat(0.25, fallbackTempo), 0.5));

    const std::vector<dawhermes::core::MidiTempoEvent> tempoMap {
        dawhermes::core::MidiTempoEvent { 0.0, 500000 },
        dawhermes::core::MidiTempoEvent { 2.0, 1000000 },
    };
    EXPECT_TRUE(approxEqual(dawhermes::audio::midiBeatToSeconds(3.0, tempoMap), 2.0));
    EXPECT_TRUE(approxEqual(dawhermes::audio::midiSecondsToBeat(2.0, tempoMap), 3.0));

    dawhermes::core::Track track;
    track.type = TrackType::midi;
    track.name = "Ordering";
    track.midiNotes = {
        MidiNote { 67, 100, 1.0, 1.0, 1, 1 },
        MidiNote { 60, 100, 0.0, 1.0, 16, 2 },
    };
    dawhermes::core::MidiSourceMetadata metadata;
    metadata.tempoMap = tempoMap;
    track.midiSourceMetadata = metadata;

    const auto result = dawhermes::audio::createMidiPlaybackSnapshot(track);
    EXPECT_TRUE(result.ok);
    EXPECT_TRUE(approxEqual(result.snapshot.durationSeconds, 1.0));

    bool sawOffBeforeOn = false;
    for (std::size_t index = 0; index + 1 < result.snapshot.events.size(); ++index) {
        const auto& current = result.snapshot.events[index];
        const auto& next = result.snapshot.events[index + 1];
        if (approxEqual(current.timeSeconds, 0.5)
            && approxEqual(next.timeSeconds, 0.5)
            && current.kind == dawhermes::audio::MidiPlaybackEventKind::noteOff
            && next.kind == dawhermes::audio::MidiPlaybackEventKind::noteOn) {
            sawOffBeforeOn = true;
        }
    }
    EXPECT_TRUE(sawOffBeforeOn);
    return true;
}

bool testMidiPlaybackSnapshotAndNoHistoryBehaviour()
{
    ProjectModel project;
    const auto trackId = project.addTrack(TrackType::midi, "Snapshot").id;
    EXPECT_TRUE(project.replaceMidiNotes(trackId, { MidiNote { 60, 100, 0.0, 1.0, 1, 1 } }));
    const auto tempDir = createTempDirectory("midi-playback-source-safety");
    const auto sourcePath = tempDir / "source.mid";
    {
        std::ofstream source(sourcePath, std::ios::binary | std::ios::trunc);
        source << "source-midi-sentinel";
    }
    dawhermes::core::MidiSourceMetadata metadata;
    metadata.sourceFilePath = sourcePath.string();
    EXPECT_TRUE(project.setMidiSourceMetadata(trackId, metadata));
    const auto sourceBefore = readBinaryFile(sourcePath);

    dawhermes::core::ProjectHistory history;
    const auto historySize = history.size();
    const auto notesBefore = project.findTrackById(trackId)->midiNotes;
    const auto snapshotResult = dawhermes::audio::createMidiPlaybackSnapshot(*project.findTrackById(trackId));
    EXPECT_TRUE(snapshotResult.ok);
    EXPECT_EQ(history.size(), historySize);
    EXPECT_EQ(project.findTrackById(trackId)->midiNotes, notesBefore);

    project.findTrackById(trackId)->midiNotes.front().pitch = 72;
    EXPECT_EQ(snapshotResult.snapshot.events.front().pitch, 60);
    EXPECT_EQ(project.findTrackById(trackId)->midiNotes.front().pitch, 72);

    dawhermes::audio::MidiAuditionEngine engine;
    engine.setVolume(0.5f);
    EXPECT_TRUE(approxEqual(engine.volume(), 0.5));
    engine.stop();
    engine.panic();
    EXPECT_EQ(history.size(), historySize);
    EXPECT_EQ(readBinaryFile(sourcePath), sourceBefore);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    return true;
}

bool testMidiPlaybackTransportCommandState()
{
    dawhermes::core::Track playable;
    playable.type = TrackType::midi;
    playable.midiNotes = { MidiNote { 60, 100, 0.0, 1.0, 1, 1 } };

    const auto ready = dawhermes::audio::midiTransportCommandState(playable, false);
    EXPECT_TRUE(ready.playEnabled);
    EXPECT_TRUE(!ready.stopEnabled);
    EXPECT_TRUE(ready.panicEnabled);

    const auto playing = dawhermes::audio::midiTransportCommandState(playable, true);
    EXPECT_TRUE(!playing.playEnabled);
    EXPECT_TRUE(playing.stopEnabled);
    EXPECT_TRUE(playing.panicEnabled);

    dawhermes::core::Track emptyMidi;
    emptyMidi.type = TrackType::midi;
    EXPECT_TRUE(!dawhermes::audio::midiTransportCommandState(emptyMidi, false).playEnabled);

    dawhermes::core::Track audioTrack;
    audioTrack.type = TrackType::audio;
    audioTrack.midiNotes = playable.midiNotes;
    EXPECT_TRUE(!dawhermes::audio::midiTransportCommandState(audioTrack, false).playEnabled);

    dawhermes::core::Track groupTrack;
    groupTrack.type = TrackType::group;
    groupTrack.midiNotes = playable.midiNotes;
    EXPECT_TRUE(!dawhermes::audio::midiTransportCommandState(groupTrack, false).playEnabled);
    EXPECT_TRUE(!dawhermes::audio::midiTransportCommandState(std::nullopt, false).playEnabled);
    return true;
}

bool testSelectionPlaybackMidiAudioPolicies()
{
    const auto monoPath = createSyntheticPlaybackWavFixture(
        "selection-mono",
        44100,
        1,
        4410);
    const auto stereoPath = createSyntheticPlaybackWavFixture(
        "selection-stereo",
        48000,
        2,
        4800);

    ProjectModel project;
    const auto editedMidiId = project.addTrack(TrackType::midi, "Edited MIDI").id;
    const auto comparisonMidiId = project.addTrack(TrackType::midi, "Comparison Ghost Source").id;
    const auto monoAudioId = project.addTrack(TrackType::audio, "Mono Stem").id;
    const auto stereoAudioId = project.addTrack(TrackType::audio, "Stereo Stem").id;
    const auto emptyAudioId = project.addTrack(TrackType::audio, "Empty Audio").id;
    const auto groupId = project.addTrack(TrackType::group, "Group").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        editedMidiId,
        { MidiNote { 62, 91, 0.0, 1.0, 1, 101 } }));
    EXPECT_TRUE(project.replaceMidiNotes(
        comparisonMidiId,
        { MidiNote { 72, 100, 0.0, 1.0, 1, 201 } }));
    EXPECT_TRUE(project.setAudioSourcePath(monoAudioId, monoPath.string()));
    EXPECT_TRUE(project.setAudioSourcePath(stereoAudioId, stereoPath.string()));

    SelectionState selection;
    selection.selectTrack(editedMidiId);
    auto midiOnly = dawhermes::audio::createSelectionPlaybackSnapshot(project, selection);
    EXPECT_TRUE(midiOnly.ok);
    EXPECT_EQ(midiOnly.snapshot.midiTrackCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(midiOnly.snapshot.audioTrackCount(), static_cast<std::size_t>(0));
    EXPECT_TRUE(midiOnly.snapshot.midi.has_value());
    EXPECT_EQ(midiOnly.snapshot.midi->events.front().pitch, 62);

    selection.selectTrack(monoAudioId);
    auto audioOnly = dawhermes::audio::createSelectionPlaybackSnapshot(project, selection);
    EXPECT_TRUE(audioOnly.ok);
    EXPECT_EQ(audioOnly.snapshot.midiTrackCount(), static_cast<std::size_t>(0));
    EXPECT_EQ(audioOnly.snapshot.audioTrackCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(audioOnly.snapshot.audioStems.front().channels.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(approxEqual(audioOnly.snapshot.audioStems.front().sourceSampleRate, 44100.0));

    selection.selectTrack(editedMidiId);
    selection.toggleTrack(monoAudioId);
    selection.toggleTrack(stereoAudioId);
    selection.toggleTrack(emptyAudioId);
    selection.toggleTrack(groupId);
    auto combined = dawhermes::audio::createSelectionPlaybackSnapshot(project, selection);
    EXPECT_TRUE(combined.ok);
    EXPECT_EQ(combined.snapshot.midiTrackCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(combined.snapshot.audioTrackCount(), static_cast<std::size_t>(2));
    EXPECT_TRUE(combined.snapshot.midi.has_value());
    EXPECT_EQ(combined.snapshot.midi->events.front().pitch, 62);
    EXPECT_EQ(combined.snapshot.audioStems.at(1).channels.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(approxEqual(combined.snapshot.audioStems.at(1).sourceSampleRate, 48000.0));
    EXPECT_EQ(
        dawhermes::audio::describeSelectionPlayback(combined.snapshot),
        std::string("Playing selection: 1 MIDI track, 2 audio tracks"));

    selection.toggleTrack(comparisonMidiId);
    auto comparisonSelection = dawhermes::audio::createSelectionPlaybackSnapshot(project, selection);
    EXPECT_TRUE(comparisonSelection.ok);
    EXPECT_EQ(comparisonSelection.snapshot.midiTrackCount(), static_cast<std::size_t>(1));
    EXPECT_TRUE(comparisonSelection.snapshot.midi.has_value());
    EXPECT_EQ(comparisonSelection.snapshot.midi->events.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(comparisonSelection.snapshot.midi->events.front().pitch, 62);

    std::error_code ec;
    std::filesystem::remove(monoPath, ec);
    std::filesystem::remove(stereoPath, ec);
    return true;
}

bool testSelectionPlaybackWavSafetyAndTiming()
{
    const auto monoPath = createSyntheticPlaybackWavFixture(
        "safety-mono",
        44100,
        1,
        4410);
    const auto sourceBytes = readBinaryFile(monoPath);
    const auto unreadablePath = createTempWavFixture("selection-unreadable");
    const auto unreadableBytes = readBinaryFile(unreadablePath);
    const auto missingPath = monoPath.parent_path() / "dawhermes-definitely-missing.wav";

    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Edited").id;
    const auto readableId = project.addTrack(TrackType::audio, "Readable").id;
    const auto unreadableId = project.addTrack(TrackType::audio, "Unreadable").id;
    const auto missingId = project.addTrack(TrackType::audio, "Missing").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        { MidiNote { 65, 87, 0.0, 2.0, 3, 999 } }));
    EXPECT_TRUE(project.setAudioSourcePath(readableId, monoPath.string()));
    EXPECT_TRUE(project.setAudioSourcePath(unreadableId, unreadablePath.string()));
    EXPECT_TRUE(project.setAudioSourcePath(missingId, missingPath.string()));

    dawhermes::core::MidiSourceMetadata metadata;
    metadata.tempoMap = { dawhermes::core::MidiTempoEvent { 0.0, 1000000 } };
    EXPECT_TRUE(project.setMidiSourceMetadata(midiId, metadata));

    SelectionState selection;
    selection.selectTrack(midiId);
    selection.toggleTrack(readableId);
    selection.toggleTrack(unreadableId);
    selection.toggleTrack(missingId);

    dawhermes::core::ProjectHistory history;
    const auto historySize = history.size();
    const auto result = dawhermes::audio::createSelectionPlaybackSnapshot(project, selection);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.snapshot.midiTrackCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.snapshot.audioTrackCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(result.skippedAudioTracks.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(result.snapshot.audioStems.front().sourcePath, monoPath.string());
    EXPECT_TRUE(approxEqual(result.snapshot.audioStems.front().durationSeconds(), 0.1));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::audioSourceFramePosition(0.05, 44100.0),
        2205.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::audioSourceFramePosition(0.05, 48000.0),
        2400.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::audioSourceFramePosition(0.0, 44100.0),
        0.0));
    EXPECT_TRUE(result.snapshot.midi.has_value());
    EXPECT_TRUE(approxEqual(result.snapshot.midi->events.front().timeSeconds, 0.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionPlayheadBeat(0.5, result.snapshot),
        0.5));
    EXPECT_EQ(history.size(), historySize);
    EXPECT_EQ(readBinaryFile(monoPath), sourceBytes);
    EXPECT_EQ(readBinaryFile(unreadablePath), unreadableBytes);
    EXPECT_EQ(project.findTrackById(readableId)->audioSourcePath, monoPath.string());

    SelectionState invalidOnly;
    invalidOnly.selectTrack(unreadableId);
    invalidOnly.toggleTrack(missingId);
    const auto invalidResult = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        invalidOnly);
    EXPECT_TRUE(!invalidResult.ok);
    EXPECT_EQ(invalidResult.skippedAudioTracks.size(), static_cast<std::size_t>(2));
    EXPECT_TRUE(
        invalidResult.message.find("Skipped unreadable audio track:") == 0);

    std::error_code ec;
    std::filesystem::remove(monoPath, ec);
    std::filesystem::remove(unreadablePath, ec);
    return true;
}

bool testSelectionPlaybackDecodedAudioBudget()
{
    EXPECT_EQ(
        dawhermes::audio::decodedWavBytes(100, 1).value(),
        static_cast<std::uint64_t>(400));
    EXPECT_EQ(
        dawhermes::audio::decodedWavBytes(100, 2).value(),
        static_cast<std::uint64_t>(800));
    EXPECT_TRUE(!dawhermes::audio::decodedWavBytes(100, 0).has_value());
    EXPECT_TRUE(!dawhermes::audio::decodedWavBytes(100, 3).has_value());
    EXPECT_TRUE(!dawhermes::audio::decodedWavBytes(
        std::numeric_limits<std::uint64_t>::max(),
        2).has_value());
    static_assert(
        dawhermes::audio::kMaximumDecodedWavBytes
            ==
        static_cast<std::uint64_t>(512ULL * 1024ULL * 1024ULL));
    static_assert(dawhermes::audio::kWavDecodeBlockFrames == 4096);

    const auto oversizedPath = createSyntheticPlaybackWavFixture(
        "budget-oversized",
        44100,
        2,
        100);
    const auto normalPath = createSyntheticPlaybackWavFixture(
        "budget-normal",
        44100,
        1,
        100);
    const auto smallerPath = createSyntheticPlaybackWavFixture(
        "budget-smaller",
        44100,
        1,
        50);
    const auto oversizedBefore = readBinaryFile(oversizedPath);
    const auto normalBefore = readBinaryFile(normalPath);
    const auto smallerBefore = readBinaryFile(smallerPath);

    ProjectModel project;
    const auto oversizedId =
        project.addTrack(TrackType::audio, "Oversized").id;
    const auto normalId = project.addTrack(TrackType::audio, "Normal").id;
    const auto smallerId = project.addTrack(TrackType::audio, "Smaller").id;
    EXPECT_TRUE(project.setAudioSourcePath(
        oversizedId,
        dawhermes::core::pathToUtf8(oversizedPath)));
    EXPECT_TRUE(project.setAudioSourcePath(
        normalId,
        dawhermes::core::pathToUtf8(normalPath)));
    EXPECT_TRUE(project.setAudioSourcePath(
        smallerId,
        dawhermes::core::pathToUtf8(smallerPath)));

    SelectionState selection;
    selection.selectTrack(oversizedId);
    selection.toggleTrack(normalId);
    selection.toggleTrack(smallerId);
    dawhermes::audio::SelectionPlaybackOptions options;
    options.maximumDecodedAudioBytes = 600;
    const auto result = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        selection,
        options);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.snapshot.audioTrackCount(), static_cast<std::size_t>(2));
    EXPECT_EQ(result.skippedAudioTracks.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(
        result.skippedAudioTracks.front().sourceTrackId,
        oversizedId);
    EXPECT_TRUE(
        result.skippedAudioTracks.front().reason.find(
            "audition memory limit") != std::string::npos);
    EXPECT_EQ(
        result.estimatedSelectedAudioBytes,
        static_cast<std::uint64_t>(1400));
    EXPECT_EQ(result.decodedAudioBytes, static_cast<std::uint64_t>(600));
    EXPECT_TRUE(!result.selectedAudioByteEstimateOverflow);
    EXPECT_EQ(
        result.snapshot.audioStems.front().channels.front().size(),
        static_cast<std::size_t>(100));
    EXPECT_EQ(
        result.snapshot.audioStems.back().channels.front().size(),
        static_cast<std::size_t>(50));

    options.maximumDecodedAudioBytes = 500;
    selection.selectTrack(normalId);
    selection.toggleTrack(smallerId);
    const auto aggregateExceeded =
        dawhermes::audio::createSelectionPlaybackSnapshot(
            project,
            selection,
            options);
    EXPECT_TRUE(aggregateExceeded.ok);
    EXPECT_EQ(
        aggregateExceeded.snapshot.audioTrackCount(),
        static_cast<std::size_t>(1));
    EXPECT_EQ(
        aggregateExceeded.skippedAudioTracks.size(),
        static_cast<std::size_t>(1));
    EXPECT_EQ(
        aggregateExceeded.skippedAudioTracks.front().sourceTrackId,
        smallerId);

    options.maximumDecodedAudioBytes = 799;
    selection.selectTrack(oversizedId);
    const auto onlyExceeded =
        dawhermes::audio::createSelectionPlaybackSnapshot(
            project,
            selection,
            options);
    EXPECT_TRUE(!onlyExceeded.ok);
    EXPECT_EQ(onlyExceeded.snapshot.audioTrackCount(), static_cast<std::size_t>(0));
    EXPECT_TRUE(
        onlyExceeded.message.find("audition memory limit")
        != std::string::npos);

    EXPECT_EQ(readBinaryFile(oversizedPath), oversizedBefore);
    EXPECT_EQ(readBinaryFile(normalPath), normalBefore);
    EXPECT_EQ(readBinaryFile(smallerPath), smallerBefore);

    std::error_code error;
    std::filesystem::remove(oversizedPath, error);
    std::filesystem::remove(normalPath, error);
    std::filesystem::remove(smallerPath, error);
    return true;
}

bool testSelectionPlaybackTransportAndStopPanicState()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "MIDI").id;
    const auto audioId = project.addTrack(TrackType::audio, "Audio").id;
    const auto emptyMidiId = project.addTrack(TrackType::midi, "Empty MIDI").id;
    const auto groupId = project.addTrack(TrackType::group, "Group").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        { MidiNote { 60, 100, 0.0, 1.0, 1, 1 } }));
    EXPECT_TRUE(project.setAudioSourcePath(audioId, "assigned.wav"));

    SelectionState selection;
    EXPECT_TRUE(!dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        dawhermes::audio::TransportMode::stopped,
        0.0).playEnabled);
    selection.selectTrack(emptyMidiId);
    selection.toggleTrack(groupId);
    EXPECT_TRUE(!dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        dawhermes::audio::TransportMode::stopped,
        0.0).playEnabled);
    selection.selectTrack(midiId);
    auto ready = dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        dawhermes::audio::TransportMode::stopped,
        0.5);
    EXPECT_TRUE(ready.playEnabled);
    EXPECT_TRUE(!ready.stopEnabled);
    selection.selectTrack(audioId);
    ready = dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        dawhermes::audio::TransportMode::stopped,
        2.0);
    EXPECT_TRUE(ready.playEnabled);
    const auto playing = dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        dawhermes::audio::TransportMode::playing,
        2.0);
    EXPECT_TRUE(!playing.playEnabled);
    EXPECT_TRUE(playing.pauseEnabled);
    EXPECT_TRUE(playing.stopEnabled);
    EXPECT_TRUE(playing.rewindEnabled);
    EXPECT_TRUE(playing.fastForwardEnabled);
    EXPECT_TRUE(playing.panicEnabled);

    const auto paused = dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        dawhermes::audio::TransportMode::paused,
        2.0);
    EXPECT_TRUE(paused.playEnabled);
    EXPECT_TRUE(!paused.pauseEnabled);
    EXPECT_TRUE(paused.stopEnabled);

    auto midiSnapshot = dawhermes::audio::createMidiPlaybackSnapshot(
        *project.findTrackById(midiId));
    EXPECT_TRUE(midiSnapshot.ok);
    dawhermes::audio::SelectionPlaybackSnapshot snapshot;
    snapshot.durationSeconds = midiSnapshot.snapshot.durationSeconds;
    snapshot.playheadTempoMap = midiSnapshot.snapshot.tempoMap;
    snapshot.midi = std::move(midiSnapshot.snapshot);
    dawhermes::audio::AudioStemPlaybackSnapshot stem;
    stem.sourceTrackId = audioId;
    stem.sourceTrackName = "Audio";
    stem.sourcePath = "assigned.wav";
    stem.sourceSampleRate = 44100.0;
    stem.frameCount = 2;
    stem.channels = { { 0.25f, -0.25f } };
    snapshot.audioStems.push_back(std::move(stem));
    EXPECT_EQ(snapshot.midiTrackCount(), static_cast<std::size_t>(1));
    EXPECT_EQ(snapshot.audioTrackCount(), static_cast<std::size_t>(1));

    dawhermes::audio::SharedTransportState state;
    state.prepare(
        std::make_shared<const dawhermes::audio::SelectionPlaybackSnapshot>(
            snapshot),
        0.0);
    state.play();
    EXPECT_TRUE(state.isPlaying());
    EXPECT_TRUE(state.isPlayheadVisible());
    EXPECT_TRUE(state.hasPreparedPlayback());
    EXPECT_TRUE(state.snapshot()->midi.has_value());
    EXPECT_EQ(state.snapshot()->audioStems.size(), static_cast<std::size_t>(1));
    state.updatePositionFromAudio(0.25);
    state.pause();
    EXPECT_TRUE(state.isPaused());
    EXPECT_TRUE(state.isPlayheadVisible());
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.25));
    state.seek(0.40);
    EXPECT_TRUE(state.isPaused());
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.40));
    state.play();
    EXPECT_TRUE(state.isPlaying());
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.40));
    state.stop();
    EXPECT_TRUE(!state.isPlaying());
    EXPECT_TRUE(state.hasPreparedPlayback());
    EXPECT_TRUE(state.snapshot() != nullptr);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
    EXPECT_TRUE(!state.isPlayheadVisible());
    EXPECT_EQ(
        dawhermes::audio::formatTransportCounter(
            state.currentSeconds(),
            state.totalSeconds()),
        std::string("00:00 / 00:01"));

    state.prepare(
        std::make_shared<const dawhermes::audio::SelectionPlaybackSnapshot>(
            std::move(snapshot)),
        0.0);
    state.play();
    EXPECT_TRUE(state.isPlaying());
    state.panic();
    EXPECT_TRUE(!state.isPlaying());
    EXPECT_TRUE(!state.hasPreparedPlayback());
    EXPECT_TRUE(!state.isPlayheadVisible());
    return true;
}

bool testStoppedPlayableSelectionIdentityAndSeekPreservation()
{
    dawhermes::audio::SharedTransportState state;
    state.setPreviewDuration(30.0, 1);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
    EXPECT_TRUE(!state.isPlayheadVisible());

    state.seek(10.0);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 10.0));
    EXPECT_TRUE(state.isPlayheadVisible());

    state.setPreviewDuration(40.0, 1);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 10.0));
    EXPECT_TRUE(state.isPlayheadVisible());

    state.complete();
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 40.0));
    EXPECT_TRUE(state.isPlayheadVisible());

    state.setPreviewDuration(60.0, 2);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
    EXPECT_TRUE(!state.isPlayheadVisible());

    state.seek(5.0);
    state.setPreviewDuration(20.0, 2);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 5.0));
    EXPECT_TRUE(state.isPlayheadVisible());

    state.setPreviewDuration(3.0, 2);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 3.0));
    EXPECT_TRUE(state.isPlayheadVisible());
    return true;
}

bool testStoppedTransportResynchronizesCurrentSelection()
{
    const auto makeAudioSnapshot = [](
                                       double durationSeconds,
                                       double bpm,
                                       std::uint64_t sourceTrackId) {
        dawhermes::audio::SelectionPlaybackSnapshot snapshot;
        snapshot.durationSeconds = durationSeconds;
        snapshot.playheadTempoMap = {
            dawhermes::core::MidiTempoEvent {
                0.0,
                static_cast<int>(std::llround(60000000.0 / bpm)),
            },
        };
        dawhermes::audio::AudioStemPlaybackSnapshot stem;
        stem.sourceTrackId = sourceTrackId;
        stem.sourceTrackName = "Controlled stem";
        stem.sourcePath = "controlled.wav";
        stem.sourceSampleRate = 1.0;
        stem.frameCount = static_cast<std::uint64_t>(
            std::ceil(durationSeconds));
        stem.channels = {
            std::vector<float>(
                static_cast<std::size_t>(stem.frameCount),
                0.25f),
        };
        snapshot.audioStems.push_back(std::move(stem));
        return snapshot;
    };

    dawhermes::audio::SelectionPlaybackSummary summaryB;
    summaryB.playable = true;
    summaryB.audioTrackCount = 1;
    summaryB.durationSeconds = 7.0;
    summaryB.tempoSource = dawhermes::audio::PlaybackTempoSource::detectedWav;
    summaryB.tempoMap = {
        dawhermes::core::MidiTempoEvent {
            0.0,
            static_cast<int>(std::llround(60000000.0 / 90.0)),
        },
    };
    summaryB.identity.readableAudioTrackIds = { 202 };

    for (const auto pauseBeforeSelectionChange : { false, true }) {
        dawhermes::audio::SharedTransportState state;
        dawhermes::core::TimelineViewportState viewport;
        viewport.startBeat = 42.0;
        viewport.visibleBeats = 16.0;
        const auto viewportBeforeStop = viewport;

        state.setPreviewDuration(20.0, 1);
        auto snapshotA = makeAudioSnapshot(20.0, 120.0, 101);
        state.prepare(
            std::make_shared<const dawhermes::audio::SelectionPlaybackSnapshot>(
                std::move(snapshotA)),
            0.0);
        state.play();
        state.updatePositionFromAudio(12.0);
        if (pauseBeforeSelectionChange) {
            state.pause();
            EXPECT_TRUE(state.isPaused());
        } else {
            EXPECT_TRUE(state.isPlaying());
        }

        // Selection B is already summarized, but immutable playback A remains
        // authoritative until Stop.
        EXPECT_TRUE(state.snapshot() != nullptr);
        EXPECT_TRUE(approxEqual(state.totalSeconds(), 20.0));
        EXPECT_TRUE(approxEqual(state.currentSeconds(), 12.0));

        state.stop();
        dawhermes::audio::synchronizeStoppedTransportPreview(
            state,
            summaryB.durationSeconds,
            2);

        EXPECT_EQ(state.mode(), dawhermes::audio::TransportMode::stopped);
        EXPECT_TRUE(!state.hasPreparedPlayback());
        EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
        EXPECT_TRUE(approxEqual(state.totalSeconds(), summaryB.durationSeconds));
        EXPECT_TRUE(!state.isPlayheadVisible());
        EXPECT_EQ(
            dawhermes::audio::formatTransportCounter(
                state.currentSeconds(),
                state.totalSeconds()),
            std::string("00:00 / 00:07"));
        EXPECT_TRUE(approxEqual(
            dawhermes::audio::selectionSummaryBpm(
                state.currentSeconds(),
                summaryB),
            90.0,
            0.01));
        EXPECT_TRUE(approxEqual(viewport.startBeat, viewportBeforeStop.startBeat));
        EXPECT_TRUE(approxEqual(
            viewport.visibleBeats,
            viewportBeforeStop.visibleBeats));

        auto snapshotB = makeAudioSnapshot(7.0, 90.0, 202);
        state.prepare(
            std::make_shared<const dawhermes::audio::SelectionPlaybackSnapshot>(
                std::move(snapshotB)),
            state.currentSeconds());
        state.play();
        EXPECT_TRUE(state.isPlaying());
        EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
        EXPECT_TRUE(approxEqual(
            dawhermes::audio::selectionPlaybackBpm(
                state.currentSeconds(),
                *state.snapshot()),
            90.0,
            0.01));

        state.stop();
        dawhermes::audio::synchronizeStoppedTransportPreview(
            state,
            summaryB.durationSeconds,
            2);
        state.seek(100.0);
        EXPECT_TRUE(approxEqual(state.currentSeconds(), 7.0));
        state.seek(-100.0);
        EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));

        state.panic();
        dawhermes::audio::synchronizeStoppedTransportPreview(
            state,
            summaryB.durationSeconds,
            2);
        EXPECT_TRUE(!state.hasPreparedPlayback());
        EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
        EXPECT_TRUE(approxEqual(state.totalSeconds(), 7.0));
        EXPECT_TRUE(!state.isPlayheadVisible());
    }
    return true;
}

bool testTransportPauseResumeSeekAndCounter()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Resume MIDI").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        { MidiNote { 60, 100, 0.0, 4.0, 1, 1 } }));
    const auto midi = dawhermes::audio::createMidiPlaybackSnapshot(
        *project.findTrackById(midiId));
    EXPECT_TRUE(midi.ok);

    dawhermes::audio::SelectionPlaybackSnapshot snapshot;
    snapshot.midi = midi.snapshot;
    snapshot.playheadTempoMap = midi.snapshot.tempoMap;
    snapshot.durationSeconds = 30.0;
    dawhermes::audio::AudioStemPlaybackSnapshot stem;
    stem.sourceSampleRate = 1.0;
    stem.frameCount = 30;
    stem.channels = { std::vector<float>(30, 0.25f) };
    snapshot.audioStems.push_back(std::move(stem));

    dawhermes::core::SelectionState trackSelection;
    trackSelection.selectTrack(midiId);
    dawhermes::core::MidiNoteSelectionState noteSelection;
    noteSelection.selectSingle(midiId, project.findTrackById(midiId)->midiNotes.front().id);
    const auto trackIdsBefore = trackSelection.selectedTrackIds();
    const auto noteIdsBefore = noteSelection.selectedNoteIds();
    dawhermes::core::ProjectHistory history;
    const auto historySize = history.size();

    dawhermes::audio::SharedTransportState state;
    state.prepare(
        std::make_shared<const dawhermes::audio::SelectionPlaybackSnapshot>(
            snapshot),
        0.0);
    state.play();
    state.updatePositionFromAudio(12.25);
    state.pause();
    const auto pausedPosition = state.currentSeconds();
    state.updatePositionFromAudio(18.0);
    EXPECT_TRUE(state.isPaused());
    EXPECT_TRUE(approxEqual(state.currentSeconds(), pausedPosition));

    const auto resumeAtPause = dawhermes::audio::createMidiResumeState(
        midi.snapshot,
        0.75);
    EXPECT_EQ(resumeAtPause.activeNoteOns.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(resumeAtPause.activeNoteOns.front().pitch, 60);

    state.seek(dawhermes::audio::seekTransportSeconds(
        state.currentSeconds(),
        dawhermes::audio::kTransportSeekSeconds,
        state.totalSeconds()));
    EXPECT_TRUE(state.isPaused());
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 17.25));
    state.play();
    EXPECT_TRUE(state.isPlaying());
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 17.25));

    state.seek(dawhermes::audio::seekTransportSeconds(
        state.currentSeconds(),
        dawhermes::audio::kTransportSeekSeconds,
        state.totalSeconds()));
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 22.25));
    state.seek(dawhermes::audio::seekTransportSeconds(
        state.currentSeconds(),
        100.0,
        state.totalSeconds()));
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 30.0));
    state.seek(dawhermes::audio::seekTransportSeconds(
        state.currentSeconds(),
        -45.0,
        state.totalSeconds()));
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
    state.complete();
    EXPECT_EQ(state.mode(), dawhermes::audio::TransportMode::stopped);
    EXPECT_TRUE(approxEqual(state.currentSeconds(), state.totalSeconds()));
    EXPECT_TRUE(state.isPlayheadVisible());
    state.stop();
    EXPECT_TRUE(approxEqual(state.currentSeconds(), 0.0));
    EXPECT_TRUE(!state.isPlayheadVisible());

    EXPECT_EQ(
        dawhermes::audio::formatTransportCounter(42.0, 197.2),
        std::string("00:42 / 03:18"));
    EXPECT_EQ(
        dawhermes::audio::formatTransportCounter(-10.0, 90.0),
        std::string("00:00 / 01:30"));
    EXPECT_EQ(
        dawhermes::audio::formatTransportCounter(999.0, 90.0),
        std::string("01:30 / 01:30"));
    EXPECT_EQ(
        dawhermes::audio::formatTransportCounter(3723.0, 3723.0),
        std::string("1:02:03 / 1:02:03"));
    EXPECT_EQ(
        dawhermes::audio::formatTransportCounter(0.0, 0.0),
        std::string("00:00 / 00:00"));
    EXPECT_TRUE(approxEqual(dawhermes::audio::kTransportSeekSeconds, 5.0));
    EXPECT_EQ(history.size(), historySize);
    EXPECT_EQ(trackSelection.selectedTrackIds(), trackIdsBefore);
    EXPECT_EQ(noteSelection.selectedNoteIds(), noteIdsBefore);
    return true;
}

bool testPlaybackTempoSourcePriorityAndDuration()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Tempo MIDI").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        { MidiNote { 64, 100, 0.0, 4.0, 1, 1 } }));
    dawhermes::core::MidiSourceMetadata metadata;
    metadata.containsExplicitTempoEvents = true;
    metadata.tempoMap = {
        dawhermes::core::MidiTempoEvent { 0.0, 500000 },
        dawhermes::core::MidiTempoEvent { 2.0, 400000 },
    };
    EXPECT_TRUE(project.setMidiSourceMetadata(midiId, metadata));

    SelectionState selection;
    selection.selectTrack(midiId);
    dawhermes::audio::SelectionPlaybackOptions detectedOptions;
    detectedOptions.detectedWavBpm = 90.0;
    const auto explicitResult = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        selection,
        detectedOptions);
    EXPECT_TRUE(explicitResult.ok);
    EXPECT_EQ(
        explicitResult.snapshot.tempoSource,
        dawhermes::audio::PlaybackTempoSource::explicitMidi);
    EXPECT_TRUE(approxEqual(explicitResult.snapshot.durationSeconds, 1.8));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionPlaybackBpm(0.9, explicitResult.snapshot),
        120.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionPlaybackBpm(1.1, explicitResult.snapshot),
        150.0));
    const auto explicitSummary =
        dawhermes::audio::createSelectionPlaybackSummary(
            project,
            selection,
            detectedOptions);
    EXPECT_EQ(
        explicitSummary.tempoSource,
        dawhermes::audio::PlaybackTempoSource::explicitMidi);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(0.9, explicitSummary),
        120.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(1.1, explicitSummary),
        150.0));

    dawhermes::audio::SharedTransportState stoppedTempoState;
    stoppedTempoState.setPreviewDuration(
        explicitSummary.durationSeconds,
        1);
    stoppedTempoState.seek(1.1);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(
            stoppedTempoState.currentSeconds(),
            explicitSummary),
        150.0));
    stoppedTempoState.stop();
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(
            stoppedTempoState.currentSeconds(),
            explicitSummary),
        120.0));
    stoppedTempoState.complete();
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(
            stoppedTempoState.currentSeconds(),
            explicitSummary),
        150.0));

    metadata.containsExplicitTempoEvents = true;
    metadata.tempoMap = {
        dawhermes::core::MidiTempoEvent {
            0.0,
            static_cast<int>(std::llround(60000000.0 / 128.5)),
        },
    };
    EXPECT_TRUE(project.setMidiSourceMetadata(midiId, metadata));
    const auto fractional = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        selection,
        detectedOptions);
    EXPECT_TRUE(fractional.ok);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionPlaybackBpm(0.0, fractional.snapshot),
        128.5,
        0.01));
    const auto fractionalSummary =
        dawhermes::audio::createSelectionPlaybackSummary(
            project,
            selection,
            detectedOptions);
    EXPECT_EQ(
        fractionalSummary.tempoSource,
        dawhermes::audio::PlaybackTempoSource::explicitMidi);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(0.0, fractionalSummary),
        128.5,
        0.01));
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(
            fractionalSummary.durationSeconds,
            fractionalSummary),
        128.5,
        0.01));

    metadata.containsExplicitTempoEvents = false;
    metadata.tempoMap = { dawhermes::core::MidiTempoEvent {} };
    EXPECT_TRUE(project.setMidiSourceMetadata(midiId, metadata));
    const auto detected = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        selection,
        detectedOptions);
    EXPECT_TRUE(detected.ok);
    EXPECT_EQ(
        detected.snapshot.tempoSource,
        dawhermes::audio::PlaybackTempoSource::detectedWav);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionPlaybackBpm(0.0, detected.snapshot),
        90.0,
        0.01));
    EXPECT_TRUE(approxEqual(detected.snapshot.durationSeconds, 4.0 * (60.0 / 90.0), 0.001));

    const auto fallback = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        selection);
    EXPECT_TRUE(fallback.ok);
    EXPECT_EQ(
        fallback.snapshot.tempoSource,
        dawhermes::audio::PlaybackTempoSource::fallback);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionPlaybackBpm(0.0, fallback.snapshot),
        120.0,
        0.01));

    const auto activeFallbackTempo = fallback.snapshot.playheadTempoMap;
    const auto nextPlayback = dawhermes::audio::createSelectionPlaybackSnapshot(
        project,
        selection,
        detectedOptions);
    EXPECT_EQ(
        fallback.snapshot.playheadTempoMap,
        activeFallbackTempo);
    EXPECT_EQ(
        nextPlayback.snapshot.tempoSource,
        dawhermes::audio::PlaybackTempoSource::detectedWav);

    const auto tempDir = createTempDirectory("midi-no-explicit-tempo");
    const auto midiPath = tempDir / "no-tempo.mid";
    juce::MidiMessageSequence sequence;
    auto noteOn = juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100));
    noteOn.setTimeStamp(0.0);
    sequence.addEvent(noteOn);
    auto noteOff = juce::MidiMessage::noteOff(1, 60);
    noteOff.setTimeStamp(960.0);
    sequence.addEvent(noteOff);
    sequence.updateMatchedPairs();
    juce::MidiFile midiFile;
    midiFile.setTicksPerQuarterNote(960);
    midiFile.addTrack(sequence);
    {
        juce::FileOutputStream output { juce::File(midiPath.string()) };
        EXPECT_TRUE(output.openedOk());
        EXPECT_TRUE(midiFile.writeTo(output, 1));
    }
    std::string parseError;
    const auto parsed = dawhermes::ui::parseMidiImportDocument(midiPath, parseError);
    EXPECT_TRUE(parsed.has_value());
    EXPECT_TRUE(!parsed->containsExplicitTempoEvents);
    EXPECT_EQ(parsed->noteBearingTracks.size(), static_cast<std::size_t>(1));
    const auto imported = dawhermes::ui::makeImportedMidiSourceMetadata(
        parsed.value(),
        parsed->noteBearingTracks.front());
    EXPECT_TRUE(imported.containsExplicitTempoEvents.has_value());
    EXPECT_TRUE(!imported.containsExplicitTempoEvents.value());

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    return true;
}

bool testPlaybackSummaryDurationsAndAudioOnlyTempo()
{
    const auto audioPath = createSyntheticClickTrackWavFixture(
        "summary-audio",
        120.0,
        48000,
        2,
        2.0);
    const auto sourceBytes = readBinaryFile(audioPath);
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Short MIDI").id;
    const auto audioId = project.addTrack(TrackType::audio, "Long Audio").id;
    const auto missingId = project.addTrack(TrackType::audio, "Missing Audio").id;
    const auto groupId = project.addTrack(TrackType::group, "Group").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        { MidiNote { 60, 100, 0.0, 1.0, 1, 1 } }));
    EXPECT_TRUE(project.setAudioSourcePath(audioId, audioPath.string()));
    EXPECT_TRUE(project.setAudioSourcePath(
        missingId,
        (audioPath.parent_path() / "missing-summary.wav").string()));

    SelectionState selection;
    selection.selectTrack(midiId);
    auto midiOnly = dawhermes::audio::createSelectionPlaybackSummary(
        project,
        selection);
    EXPECT_TRUE(midiOnly.playable);
    EXPECT_EQ(midiOnly.midiTrackCount, static_cast<std::size_t>(1));
    EXPECT_EQ(midiOnly.audioTrackCount, static_cast<std::size_t>(0));
    EXPECT_EQ(midiOnly.identity.primaryMidiTrackId.value(), midiId);
    EXPECT_TRUE(midiOnly.identity.readableAudioTrackIds.empty());

    selection.toggleTrack(audioId);
    selection.toggleTrack(missingId);
    selection.toggleTrack(groupId);
    const auto combined = dawhermes::audio::createSelectionPlaybackSummary(
        project,
        selection);
    EXPECT_TRUE(combined.playable);
    EXPECT_EQ(combined.midiTrackCount, static_cast<std::size_t>(1));
    EXPECT_EQ(combined.audioTrackCount, static_cast<std::size_t>(1));
    EXPECT_EQ(combined.skippedAudioTracks.size(), static_cast<std::size_t>(1));
    EXPECT_TRUE(approxEqual(combined.durationSeconds, 2.0, 0.001));
    EXPECT_EQ(combined.identity.primaryMidiTrackId.value(), midiId);
    EXPECT_EQ(
        combined.identity.readableAudioTrackIds,
        std::vector<std::uint64_t> { audioId });

    selection.selectTrack(audioId);
    dawhermes::audio::SelectionPlaybackOptions options;
    options.detectedWavBpm = 120.0;
    const auto audioOnly = dawhermes::audio::createSelectionPlaybackSummary(
        project,
        selection,
        options);
    EXPECT_TRUE(audioOnly.playable);
    EXPECT_EQ(audioOnly.midiTrackCount, static_cast<std::size_t>(0));
    EXPECT_EQ(audioOnly.audioTrackCount, static_cast<std::size_t>(1));
    EXPECT_EQ(
        audioOnly.tempoSource,
        dawhermes::audio::PlaybackTempoSource::detectedWav);
    EXPECT_TRUE(approxEqual(audioOnly.durationSeconds, 2.0, 0.001));

    selection.selectTrack(missingId);
    const auto missingOnly = dawhermes::audio::createSelectionPlaybackSummary(
        project,
        selection);
    EXPECT_TRUE(!missingOnly.playable);
    EXPECT_TRUE(approxEqual(missingOnly.durationSeconds, 0.0));
    EXPECT_EQ(
        dawhermes::audio::formatTransportCounter(0.0, missingOnly.durationSeconds),
        std::string("00:00 / 00:00"));
    EXPECT_EQ(readBinaryFile(audioPath), sourceBytes);

    std::error_code ec;
    std::filesystem::remove(audioPath, ec);
    return true;
}

bool testWavBpmOctaveCandidateSelection()
{
    struct OctaveCase {
        double rawAutocorrelationBpm = 0.0;
        double expectedBpm = 0.0;
        double weakBeatStrength = 0.28;
    };
    const std::vector<OctaveCase> octaveCases {
        { 60.0, 120.0 },
        { 70.0, 140.0 },
        { 75.0, 150.0 },
        { 90.0, 180.0, 0.40 }
    };
    for (const auto& octaveCase : octaveCases) {
        const auto alternating = createSyntheticOnsetEnvelope(
            octaveCase.expectedBpm,
            20.0,
            { 1.0, octaveCase.weakBeatStrength });
        const auto decision =
            dawhermes::audio::evaluateWavBpmCandidates(alternating);
        EXPECT_TRUE(decision.bpm.has_value());
        EXPECT_TRUE(approxEqual(
            decision.strongestAutocorrelationBpm,
            octaveCase.rawAutocorrelationBpm,
            1.5));
        EXPECT_TRUE(approxEqual(
            decision.bpm.value(),
            octaveCase.expectedBpm,
            1.5));
        EXPECT_TRUE(
            decision.confidence
            >= dawhermes::audio::kMinimumWavBpmConfidence);
        EXPECT_TRUE(decision.rhythmicCoverage >= 0.8);
    }

    const auto true70 = createSyntheticOnsetEnvelope(
        70.0,
        20.0,
        { 1.0 });
    const auto true70Decision =
        dawhermes::audio::evaluateWavBpmCandidates(true70);
    EXPECT_TRUE(true70Decision.bpm.has_value());
    EXPECT_TRUE(approxEqual(true70Decision.bpm.value(), 70.0, 1.5));
    EXPECT_TRUE(
        true70Decision.confidence
        >= dawhermes::audio::kMinimumWavBpmConfidence);

    const auto ambiguous140 = createSyntheticOnsetEnvelope(
        140.0,
        20.0,
        { 1.0, 0.20 });
    const auto ambiguousDecision =
        dawhermes::audio::evaluateWavBpmCandidates(ambiguous140);
    EXPECT_TRUE(
        ambiguousDecision.confidence
        < dawhermes::audio::kMinimumWavBpmConfidence);
    return true;
}

bool testWavBpmDetectionAndSourceSafety()
{
    struct Fixture {
        std::filesystem::path path;
        double expectedBpm = 0.0;
        std::string sourceBytes;
    };

    std::vector<Fixture> fixtures;
    const std::vector<double> tempoMatrix {
        60.0,
        70.0,
        75.0,
        90.0,
        120.0,
        128.0,
        140.0,
        150.0,
        180.0
    };
    for (const auto bpm : tempoMatrix) {
        const auto path = createSyntheticClickTrackWavFixture(
            "bpm-matrix-" + std::to_string(static_cast<int>(bpm)),
            bpm,
            bpm == 90.0 ? 44100 : 48000,
            bpm == 120.0 ? 2 : 1,
            16.0);
        fixtures.push_back({ path, bpm, readBinaryFile(path) });
    }

    const auto alternating140 = createSyntheticRhythmWavFixture(
        "bpm140-alternating-accents",
        140.0,
        48000,
        1,
        20.0,
        2.0,
        {
            { 0.0, 1.0, 1200.0, 0.025, 105.0 },
            { 1.0, 0.28, 1200.0, 0.025, 105.0 }
        });
    fixtures.push_back({
        alternating140,
        140.0,
        readBinaryFile(alternating140)
    });

    const auto musical140 = createSyntheticRhythmWavFixture(
        "bpm140-musical-pattern",
        140.0,
        48000,
        2,
        20.0,
        4.0,
        {
            { 0.0, 1.0, 90.0, 0.08, 45.0 },
            { 0.5, 0.12, 3000.0, 0.02, 120.0 },
            { 1.0, 0.38, 180.0, 0.05, 70.0 },
            { 1.5, 0.12, 3000.0, 0.02, 120.0 },
            { 2.0, 0.80, 90.0, 0.08, 45.0 },
            { 2.5, 0.12, 3000.0, 0.02, 120.0 },
            { 3.0, 0.32, 180.0, 0.05, 70.0 },
            { 3.5, 0.12, 3000.0, 0.02, 120.0 }
        });
    fixtures.push_back({ musical140, 140.0, readBinaryFile(musical140) });

    const auto true70 = createSyntheticRhythmWavFixture(
        "bpm70-genuine-slow",
        70.0,
        48000,
        1,
        20.0,
        1.0,
        {
            { 0.0, 1.0, 90.0, 0.08, 45.0 }
        });
    fixtures.push_back({ true70, 70.0, readBinaryFile(true70) });

    const auto silence = createSyntheticClickTrackWavFixture(
        "silence",
        0.0,
        44100,
        1,
        8.0,
        0);
    const auto steadyTone = createSyntheticClickTrackWavFixture(
        "steady-low-energy",
        0.0,
        48000,
        2,
        8.0,
        3);

    for (const auto& fixture : fixtures) {
        const auto estimate = dawhermes::audio::analyzeWavBpm(fixture.path);
        EXPECT_TRUE(estimate.isConfident());
        EXPECT_TRUE(approxEqual(
            estimate.bpm.value(),
            fixture.expectedBpm,
            1.5));
        EXPECT_TRUE(
            estimate.bpm.value() >= 60.0
            && estimate.bpm.value() <= 200.0);
        EXPECT_EQ(readBinaryFile(fixture.path), fixture.sourceBytes);
    }

    EXPECT_TRUE(!dawhermes::audio::analyzeWavBpm(silence).isConfident());
    EXPECT_TRUE(!dawhermes::audio::analyzeWavBpm(steadyTone).isConfident());

    std::error_code ec;
    for (const auto& fixture : fixtures) {
        std::filesystem::remove(fixture.path, ec);
    }
    std::filesystem::remove(silence, ec);
    std::filesystem::remove(steadyTone, ec);
    return true;
}

bool testWavBpmCacheReuseAndInvalidation()
{
    const auto path = createSyntheticClickTrackWavFixture(
        "cache",
        120.0,
        44100,
        1,
        8.0);
    const auto firstFingerprint = dawhermes::audio::fingerprintWavFile(path);
    EXPECT_TRUE(firstFingerprint.has_value());
    dawhermes::audio::WavBpmCache cache;
    dawhermes::audio::WavBpmEstimate estimate;
    estimate.bpm = 120.0;
    estimate.confidence = 0.9;
    cache.store(firstFingerprint.value(), estimate);
    const auto cached = cache.find(firstFingerprint.value());
    EXPECT_TRUE(cached.has_value());
    EXPECT_TRUE(cached->isConfident());
    EXPECT_EQ(cache.size(), static_cast<std::size_t>(1));

    {
        std::ofstream append(path, std::ios::binary | std::ios::app);
        append.put('\0');
    }
    const auto changedFingerprint = dawhermes::audio::fingerprintWavFile(path);
    EXPECT_TRUE(changedFingerprint.has_value());
    EXPECT_TRUE(!(changedFingerprint.value() == firstFingerprint.value()));
    EXPECT_TRUE(!cache.find(changedFingerprint.value()).has_value());
    dawhermes::audio::WavBpmEstimate lowConfidence;
    lowConfidence.confidence = 0.2;
    lowConfidence.analyzedSeconds = 8.0;
    cache.store(changedFingerprint.value(), lowConfidence);
    EXPECT_EQ(cache.size(), static_cast<std::size_t>(1));
    const auto changedCached = cache.find(changedFingerprint.value());
    EXPECT_TRUE(changedCached.has_value());
    EXPECT_TRUE(!changedCached->isConfident());
    EXPECT_TRUE(approxEqual(changedCached->confidence, 0.2));
    EXPECT_TRUE(!cache.find(firstFingerprint.value()).has_value());

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
}

bool testWavBpmCacheBoundedLruPolicy()
{
    dawhermes::audio::WavBpmCache cache;
    std::vector<dawhermes::audio::WavFileFingerprint> fingerprints;
    fingerprints.reserve(dawhermes::audio::kMaximumWavBpmCacheEntries + 1);
    for (std::size_t index = 0;
         index < dawhermes::audio::kMaximumWavBpmCacheEntries;
         ++index) {
        dawhermes::audio::WavFileFingerprint fingerprint;
        fingerprint.sourcePath = "cache-entry-" + std::to_string(index);
        fingerprint.fileSize = index + 1;
        fingerprints.push_back(fingerprint);
        dawhermes::audio::WavBpmEstimate estimate;
        estimate.bpm = 60.0 + static_cast<double>(index % 120);
        estimate.confidence = index == 7 ? 0.1 : 0.9;
        cache.store(fingerprint, estimate);
    }
    EXPECT_EQ(
        cache.size(),
        dawhermes::audio::kMaximumWavBpmCacheEntries);

    EXPECT_TRUE(cache.find(fingerprints.front()).has_value());
    EXPECT_TRUE(cache.find(fingerprints.at(7)).has_value());
    EXPECT_TRUE(!cache.find(fingerprints.at(7))->isConfident());

    dawhermes::audio::WavFileFingerprint newest;
    newest.sourcePath = "cache-entry-newest";
    newest.fileSize = 9999;
    dawhermes::audio::WavBpmEstimate newestEstimate;
    newestEstimate.bpm = 123.0;
    newestEstimate.confidence = 0.95;
    cache.store(newest, newestEstimate);

    EXPECT_EQ(
        cache.size(),
        dawhermes::audio::kMaximumWavBpmCacheEntries);
    EXPECT_TRUE(cache.find(fingerprints.front()).has_value());
    EXPECT_TRUE(!cache.find(fingerprints.at(1)).has_value());
    EXPECT_TRUE(cache.find(newest).has_value());

    for (std::size_t index = 0; index < 32; ++index) {
        dawhermes::audio::WavFileFingerprint additional;
        additional.sourcePath = "cache-entry-additional-"
            + std::to_string(index);
        additional.fileSize = 20000 + index;
        cache.store(additional, newestEstimate);
        EXPECT_TRUE(
            cache.size()
            <= dawhermes::audio::kMaximumWavBpmCacheEntries);
    }
    EXPECT_TRUE(cache.find(newest).has_value());
    return true;
}

struct ControlledWavBpmAnalysis {
    explicit ControlledWavBpmAnalysis(std::string pathToBlock)
        : blockedPath(std::move(pathToBlock))
    {
    }

    dawhermes::audio::WavBpmFingerprintFunction fingerprintFunction()
    {
        return [this](const std::filesystem::path& sourcePath) {
            const auto key = dawhermes::core::pathToUtf8(
                sourcePath.lexically_normal());
            std::scoped_lock lock(mutex);
            const auto version = fingerprintVersions.contains(key)
                ? fingerprintVersions.at(key)
                : std::uintmax_t { 1 };
            return std::optional<dawhermes::audio::WavFileFingerprint> {
                dawhermes::audio::WavFileFingerprint {
                    key,
                    version,
                    {},
                },
            };
        };
    }

    dawhermes::audio::WavBpmAnalyzeFunction analyzeFunction()
    {
        return [this](const std::filesystem::path& sourcePath) {
            const auto key = dawhermes::core::pathToUtf8(
                sourcePath.lexically_normal());
            std::unique_lock lock(mutex);
            const auto callCount = ++analysisCalls[key];
            condition.notify_all();
            if (key == blockedPath && callCount == 1) {
                condition.wait(lock, [this]() { return releaseBlockedAnalysis; });
            }

            dawhermes::audio::WavBpmEstimate estimate;
            estimate.bpm = key.find("B.wav") != std::string::npos
                ? 130.0
                : (key.find("C.wav") != std::string::npos ? 150.0 : 110.0);
            estimate.confidence = 0.9;
            estimate.analyzedSeconds = 8.0;
            return estimate;
        };
    }

    bool waitForAnalysisCount(const std::string& path, int expectedCount)
    {
        std::unique_lock lock(mutex);
        return condition.wait_for(
            lock,
            std::chrono::seconds(3),
            [&]() { return analysisCalls[path] >= expectedCount; });
    }

    void setFingerprintVersion(const std::string& path, std::uintmax_t version)
    {
        std::scoped_lock lock(mutex);
        fingerprintVersions[path] = version;
    }

    void releaseFirstAnalysis()
    {
        {
            std::scoped_lock lock(mutex);
            releaseBlockedAnalysis = true;
        }
        condition.notify_all();
    }

    int analysisCallCount(const std::string& path)
    {
        std::scoped_lock lock(mutex);
        return analysisCalls[path];
    }

    std::mutex mutex;
    std::condition_variable condition;
    std::map<std::string, std::uintmax_t> fingerprintVersions;
    std::map<std::string, int> analysisCalls;
    std::string blockedPath;
    bool releaseBlockedAnalysis { false };
};

std::optional<dawhermes::audio::WavBpmAnalysisResult>
waitForControlledWavBpmResult(
    dawhermes::audio::WavBpmAnalysisService& service)
{
    const auto deadline = std::chrono::steady_clock::now()
        + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto result = service.pollCompleted(); result.has_value()) {
            return result;
        }
        std::this_thread::yield();
    }
    return std::nullopt;
}

bool testWavBpmNewestRequestWins()
{
    const std::filesystem::path pathA("A.wav");
    const std::filesystem::path pathB("B.wav");
    const std::filesystem::path pathC("C.wav");
    const auto keyA = dawhermes::core::pathToUtf8(pathA);
    const auto keyB = dawhermes::core::pathToUtf8(pathB);
    const auto keyC = dawhermes::core::pathToUtf8(pathC);

    {
        ControlledWavBpmAnalysis controlled(keyA);
        controlled.setFingerprintVersion(keyA, 1);
        dawhermes::audio::WavBpmAnalysisService service(
            controlled.fingerprintFunction(),
            controlled.analyzeFunction());
        const auto generationA = service.request(pathA);
        const auto started = controlled.waitForAnalysisCount(keyA, 1);
        controlled.setFingerprintVersion(keyA, 2);
        controlled.releaseFirstAnalysis();
        const auto retried = controlled.waitForAnalysisCount(keyA, 2);
        const auto completed = waitForControlledWavBpmResult(service);
        service.stop();

        EXPECT_TRUE(started);
        EXPECT_TRUE(retried);
        EXPECT_TRUE(completed.has_value());
        EXPECT_EQ(completed->requestGeneration, generationA);
        EXPECT_EQ(completed->fingerprint.sourcePath, keyA);
        EXPECT_EQ(completed->fingerprint.fileSize, std::uintmax_t { 2 });
        EXPECT_EQ(controlled.analysisCallCount(keyA), 2);
        EXPECT_TRUE(!service.isAnalyzing());
    }

    {
        ControlledWavBpmAnalysis controlled(keyA);
        controlled.setFingerprintVersion(keyA, 1);
        controlled.setFingerprintVersion(keyB, 1);
        dawhermes::audio::WavBpmAnalysisService service(
            controlled.fingerprintFunction(),
            controlled.analyzeFunction());
        service.request(pathA);
        const auto startedA = controlled.waitForAnalysisCount(keyA, 1);
        const auto generationB = service.request(pathB);
        controlled.setFingerprintVersion(keyA, 2);
        controlled.releaseFirstAnalysis();
        const auto startedB = controlled.waitForAnalysisCount(keyB, 1);
        const auto completed = waitForControlledWavBpmResult(service);
        service.stop();

        EXPECT_TRUE(startedA);
        EXPECT_TRUE(startedB);
        EXPECT_TRUE(completed.has_value());
        EXPECT_EQ(completed->requestGeneration, generationB);
        EXPECT_EQ(completed->fingerprint.sourcePath, keyB);
        EXPECT_EQ(controlled.analysisCallCount(keyA), 1);
        EXPECT_EQ(controlled.analysisCallCount(keyB), 1);
        EXPECT_TRUE(!service.isAnalyzing());
    }

    {
        ControlledWavBpmAnalysis controlled(keyA);
        dawhermes::audio::WavBpmAnalysisService service(
            controlled.fingerprintFunction(),
            controlled.analyzeFunction());
        service.request(pathA);
        const auto startedA = controlled.waitForAnalysisCount(keyA, 1);
        service.request(pathB);
        const auto generationC = service.request(pathC);
        controlled.releaseFirstAnalysis();
        const auto startedC = controlled.waitForAnalysisCount(keyC, 1);
        const auto completed = waitForControlledWavBpmResult(service);
        service.stop();

        EXPECT_TRUE(startedA);
        EXPECT_TRUE(startedC);
        EXPECT_TRUE(completed.has_value());
        EXPECT_EQ(completed->requestGeneration, generationC);
        EXPECT_EQ(completed->fingerprint.sourcePath, keyC);
        EXPECT_EQ(controlled.analysisCallCount(keyA), 1);
        EXPECT_EQ(controlled.analysisCallCount(keyB), 0);
        EXPECT_EQ(controlled.analysisCallCount(keyC), 1);
        EXPECT_TRUE(!service.isAnalyzing());
    }

    {
        ControlledWavBpmAnalysis controlled(keyA);
        dawhermes::audio::WavBpmAnalysisService service(
            controlled.fingerprintFunction(),
            controlled.analyzeFunction());
        service.request(pathA);
        const auto startedA = controlled.waitForAnalysisCount(keyA, 1);
        std::atomic<bool> stopped { false };
        std::thread stopper([&]() {
            service.stop();
            stopped.store(true, std::memory_order_release);
        });
        controlled.releaseFirstAnalysis();
        stopper.join();

        EXPECT_TRUE(startedA);
        EXPECT_TRUE(stopped.load(std::memory_order_acquire));
        EXPECT_TRUE(!service.isAnalyzing());
    }

    return true;
}

bool testAsynchronousWavBpmAnalysisAndCacheReuse()
{
    const auto path = createSyntheticClickTrackWavFixture(
        "async-cache",
        120.0,
        44100,
        1,
        8.0);
    dawhermes::audio::WavBpmAnalysisService service;
    const auto firstGeneration = service.request(path);
    std::optional<dawhermes::audio::WavBpmAnalysisResult> first;
    for (int attempt = 0; attempt < 500 && !first.has_value(); ++attempt) {
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
        first = service.pollCompleted();
    }
    EXPECT_TRUE(first.has_value());
    EXPECT_EQ(first->requestGeneration, firstGeneration);
    EXPECT_TRUE(first->estimate.isConfident());
    EXPECT_TRUE(approxEqual(first->estimate.bpm.value(), 120.0, 1.5));
    EXPECT_TRUE(!first->reusedCache);

    const auto secondGeneration = service.request(path);
    const auto second = service.pollCompleted();
    EXPECT_TRUE(second.has_value());
    EXPECT_EQ(second->requestGeneration, secondGeneration);
    EXPECT_TRUE(second->reusedCache);
    EXPECT_TRUE(approxEqual(second->estimate.bpm.value(), 120.0, 1.5));
    service.stop();

    std::error_code ec;
    std::filesystem::remove(path, ec);
    return true;
}

bool testTransportViewportFollowAndSeekVisibility()
{
    dawhermes::core::ProjectHistory history;
    const auto historySize = history.size();
    const dawhermes::core::PitchViewportState pitchViewport;
    EXPECT_TRUE(dawhermes::audio::shouldAutomaticallyFollowPlayhead(
        dawhermes::audio::TransportMode::playing));
    EXPECT_TRUE(!dawhermes::audio::shouldAutomaticallyFollowPlayhead(
        dawhermes::audio::TransportMode::paused));
    EXPECT_TRUE(!dawhermes::audio::shouldAutomaticallyFollowPlayhead(
        dawhermes::audio::TransportMode::stopped));

    dawhermes::core::TimelineViewportState viewport;
    viewport.startBeat = 10.0;
    viewport.visibleBeats = 20.0;
    viewport.minVisibleBeats = 1.0;
    viewport.maxVisibleBeats = 512.0;

    const auto comfortable = dawhermes::core::followTimelinePlayhead(
        viewport,
        20.0,
        100.0);
    EXPECT_TRUE(approxEqual(comfortable.startBeat, 10.0));
    EXPECT_TRUE(approxEqual(comfortable.visibleBeats, 20.0));

    const auto followed = dawhermes::core::followTimelinePlayhead(
        viewport,
        27.0,
        100.0);
    EXPECT_TRUE(followed.startBeat > viewport.startBeat);
    EXPECT_TRUE(approxEqual(followed.visibleBeats, viewport.visibleBeats));
    const auto followedRange = dawhermes::core::timelineVisibleRange(followed);
    EXPECT_TRUE(27.0 >= followedRange.startBeat && 27.0 <= followedRange.endBeat);

    const auto seekForward = dawhermes::core::ensureTimelineBeatVisible(
        viewport,
        70.0,
        100.0);
    const auto seekForwardRange = dawhermes::core::timelineVisibleRange(seekForward);
    EXPECT_TRUE(70.0 >= seekForwardRange.startBeat && 70.0 <= seekForwardRange.endBeat);
    EXPECT_TRUE(approxEqual(seekForward.visibleBeats, viewport.visibleBeats));

    const auto seekBackward = dawhermes::core::ensureTimelineBeatVisible(
        seekForward,
        2.0,
        100.0);
    const auto seekBackwardRange = dawhermes::core::timelineVisibleRange(seekBackward);
    EXPECT_TRUE(2.0 >= seekBackwardRange.startBeat && 2.0 <= seekBackwardRange.endBeat);
    EXPECT_TRUE(approxEqual(seekBackward.visibleBeats, viewport.visibleBeats));
    EXPECT_TRUE(approxEqual(
        pitchViewport.highestVisiblePitch,
        dawhermes::core::PitchViewportState {}.highestVisiblePitch));
    EXPECT_EQ(history.size(), historySize);
    return true;
}

bool testMidiTrackExporterBasicsAndRoundTrip()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Bass Edited").id;
    const auto audioId = project.addTrack(TrackType::audio, "Audio").id;
    const auto groupId = project.addTrack(TrackType::group, "Group").id;
    const auto emptyMidiId = project.addTrack(TrackType::midi, "Empty").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        {
            MidiNote { 60, 91, 1.25, 0.50, 2, 987654321 },
            MidiNote { 64, 73, 2.00, 0.25, 3, 987654322 },
        }));

    const auto* midiTrack = project.findTrackById(midiId);
    const auto* audioTrack = project.findTrackById(audioId);
    const auto* groupTrack = project.findTrackById(groupId);
    const auto* emptyTrack = project.findTrackById(emptyMidiId);
    EXPECT_TRUE(midiTrack != nullptr);
    EXPECT_TRUE(audioTrack != nullptr);
    EXPECT_TRUE(groupTrack != nullptr);
    EXPECT_TRUE(emptyTrack != nullptr);
    EXPECT_TRUE(dawhermes::midi::canExportMidiTrack(*midiTrack));
    EXPECT_TRUE(!dawhermes::midi::canExportMidiTrack(*audioTrack));
    EXPECT_TRUE(!dawhermes::midi::canExportMidiTrack(*groupTrack));
    EXPECT_TRUE(!dawhermes::midi::canExportMidiTrack(*emptyTrack));
    EXPECT_TRUE(!dawhermes::midi::exportMidiTrackToFile(*emptyTrack, createTempDirectory("empty-export") / "empty.mid").ok);

    const auto tempDir = createTempDirectory("midi-export-basics");
    const auto outputPath = tempDir / "bass-edited.mid";
    {
        std::ofstream existingOutput(outputPath, std::ios::binary | std::ios::trunc);
        existingOutput << std::string(65536, 'x');
    }
    const auto result = dawhermes::midi::exportMidiTrackToFile(*midiTrack, outputPath);
    EXPECT_TRUE(result.ok);
    EXPECT_EQ(result.exportedNoteCount, static_cast<std::size_t>(2));
    EXPECT_TRUE(std::filesystem::exists(outputPath));
    EXPECT_TRUE(std::filesystem::file_size(outputPath) < static_cast<std::uintmax_t>(65536));

    std::string parseError;
    const auto document = dawhermes::ui::parseMidiImportDocument(outputPath, parseError);
    EXPECT_TRUE(document.has_value());
    EXPECT_TRUE(parseError.empty());
    EXPECT_TRUE(document->containsExplicitTempoEvents);
    EXPECT_EQ(document->noteBearingTracks.size(), static_cast<std::size_t>(1));
    const auto& candidate = document->noteBearingTracks.front();
    const auto importedMetadata = dawhermes::ui::makeImportedMidiSourceMetadata(
        document.value(),
        candidate);
    EXPECT_TRUE(importedMetadata.containsExplicitTempoEvents.value_or(false));
    EXPECT_EQ(candidate.sourceTrackName, std::string("Bass Edited"));
    EXPECT_EQ(candidate.notes.size(), static_cast<std::size_t>(2));

    const auto* first = findNoteByPitch(candidate.notes, 60);
    const auto* second = findNoteByPitch(candidate.notes, 64);
    EXPECT_TRUE(first != nullptr);
    EXPECT_TRUE(second != nullptr);
    EXPECT_EQ(first->velocity, 91);
    EXPECT_EQ(first->channel, 2);
    EXPECT_TRUE(approxEqual(first->startBeat, 1.25));
    EXPECT_TRUE(approxEqual(first->durationBeats, 0.50));
    EXPECT_EQ(second->velocity, 73);
    EXPECT_EQ(second->channel, 3);
    EXPECT_TRUE(approxEqual(second->startBeat, 2.00));
    EXPECT_TRUE(approxEqual(second->durationBeats, 0.25));
    EXPECT_EQ(first->id, static_cast<std::uint64_t>(0));
    EXPECT_EQ(second->id, static_cast<std::uint64_t>(0));
    EXPECT_TRUE(readBinaryFile(outputPath).find("987654321") == std::string::npos);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    return true;
}

bool testMidiTrackExporterTempoSignaturePpqAndOrdering()
{
    ProjectModel project;
    const auto midiId = project.addTrack(TrackType::midi, "Metered").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiId,
        {
            MidiNote { 72, 100, 0.0, 1.0, 1, 1 },
            MidiNote { 60, 90, 1.0, 0.5, 1, 2 },
            MidiNote { 60, 80, 0.0, 1.0, 1, 3 },
            MidiNote { -4, 0, -1.0, 0.0, 99, 4 },
        }));

    dawhermes::core::MidiSourceMetadata metadata;
    metadata.midiFileType = 1;
    metadata.ticksPerQuarterNote = 480;
    metadata.tempoMap = {
        dawhermes::core::MidiTempoEvent { 0.0, 500000 },
        dawhermes::core::MidiTempoEvent { 2.0, 600000 },
    };
    metadata.timeSignatureMap = {
        dawhermes::core::MidiTimeSignatureEvent { 0.0, 3, 4 },
        dawhermes::core::MidiTimeSignatureEvent { 4.0, 5, 8 },
    };
    EXPECT_TRUE(project.setMidiSourceMetadata(midiId, metadata));

    const auto* track = project.findTrackById(midiId);
    EXPECT_TRUE(track != nullptr);

    dawhermes::midi::MidiTrackExportResult createResult;
    const auto midiFile = dawhermes::midi::createMidiFileForTrack(*track, {}, createResult);
    EXPECT_TRUE(midiFile.has_value());
    EXPECT_TRUE(createResult.ok);
    EXPECT_EQ(createResult.ticksPerQuarterNote, 480);
    EXPECT_EQ(createResult.midiFileType, 1);
    EXPECT_EQ(midiFile->getTimeFormat(), static_cast<short>(480));
    EXPECT_EQ(midiFile->getNumTracks(), 2);

    juce::MidiMessageSequence tempoEvents;
    midiFile->findAllTempoEvents(tempoEvents);
    EXPECT_EQ(tempoEvents.getNumEvents(), 2);
    EXPECT_TRUE(approxEqual(tempoEvents.getEventPointer(1)->message.getTimeStamp(), 960.0));

    juce::MidiMessageSequence timeSignatureEvents;
    midiFile->findAllTimeSigEvents(timeSignatureEvents);
    EXPECT_EQ(timeSignatureEvents.getNumEvents(), 2);
    int numerator = 0;
    int denominator = 0;
    timeSignatureEvents.getEventPointer(1)->message.getTimeSignatureInfo(numerator, denominator);
    EXPECT_EQ(numerator, 5);
    EXPECT_EQ(denominator, 8);

    const auto* noteTrack = midiFile->getTrack(1);
    EXPECT_TRUE(noteTrack != nullptr);
    bool sawNoteOffBeforeNoteOnAtSameTick = false;
    for (int index = 0; index + 1 < noteTrack->getNumEvents(); ++index) {
        const auto* current = noteTrack->getEventPointer(index);
        const auto* next = noteTrack->getEventPointer(index + 1);
        if (current == nullptr || next == nullptr) {
            continue;
        }

        if (approxEqual(current->message.getTimeStamp(), 480.0)
            && approxEqual(next->message.getTimeStamp(), 480.0)
            && current->message.isNoteOff()
            && next->message.isNoteOn()) {
            sawNoteOffBeforeNoteOnAtSameTick = true;
            break;
        }
    }
    EXPECT_TRUE(sawNoteOffBeforeNoteOnAtSameTick);

    std::vector<int> zeroTickNoteOnPitches;
    for (int index = 0; index < noteTrack->getNumEvents(); ++index) {
        const auto* event = noteTrack->getEventPointer(index);
        EXPECT_TRUE(event != nullptr);
        EXPECT_TRUE(event->message.getTimeStamp() >= 0.0);
        if (approxEqual(event->message.getTimeStamp(), 0.0) && event->message.isNoteOn()) {
            zeroTickNoteOnPitches.push_back(event->message.getNoteNumber());
        }
    }

    dawhermes::midi::MidiTrackExportResult secondCreateResult;
    const auto secondMidiFile = dawhermes::midi::createMidiFileForTrack(*track, {}, secondCreateResult);
    EXPECT_TRUE(secondMidiFile.has_value());
    const auto* secondNoteTrack = secondMidiFile->getTrack(1);
    EXPECT_TRUE(secondNoteTrack != nullptr);
    std::vector<int> secondZeroTickNoteOnPitches;
    for (int index = 0; index < secondNoteTrack->getNumEvents(); ++index) {
        const auto* event = secondNoteTrack->getEventPointer(index);
        EXPECT_TRUE(event != nullptr);
        if (approxEqual(event->message.getTimeStamp(), 0.0) && event->message.isNoteOn()) {
            secondZeroTickNoteOnPitches.push_back(event->message.getNoteNumber());
        }
    }
    EXPECT_EQ(secondZeroTickNoteOnPitches, zeroTickNoteOnPitches);

    const auto tempDir = createTempDirectory("midi-export-metadata");
    const auto outputPath = tempDir / "metered.mid";
    const auto writeResult = dawhermes::midi::exportMidiTrackToFile(*track, outputPath);
    EXPECT_TRUE(writeResult.ok);

    juce::FileInputStream stream(juce::File(outputPath.string()));
    EXPECT_TRUE(stream.openedOk());
    juce::MidiFile readBack;
    int midiFileType = -1;
    EXPECT_TRUE(readBack.readFrom(stream, true, &midiFileType));
    EXPECT_EQ(midiFileType, 1);
    EXPECT_EQ(readBack.getTimeFormat(), static_cast<short>(480));
    juce::MidiMessageSequence readBackTempoEvents;
    readBack.findAllTempoEvents(readBackTempoEvents);
    EXPECT_EQ(readBackTempoEvents.getNumEvents(), 2);
    juce::MidiMessageSequence readBackSignatures;
    readBack.findAllTimeSigEvents(readBackSignatures);
    EXPECT_EQ(readBackSignatures.getNumEvents(), 2);

    dawhermes::core::Track fallbackTrack;
    fallbackTrack.type = TrackType::midi;
    fallbackTrack.name = "Fallback";
    fallbackTrack.midiNotes = { MidiNote { 60, 100, 0.0, 0.25, 1, 9 } };
    dawhermes::midi::MidiTrackExportResult fallbackResult;
    const auto fallbackFile = dawhermes::midi::createMidiFileForTrack(fallbackTrack, {}, fallbackResult);
    EXPECT_TRUE(fallbackFile.has_value());
    EXPECT_EQ(fallbackResult.ticksPerQuarterNote, 960);
    EXPECT_EQ(fallbackResult.midiFileType, 1);
    juce::MidiMessageSequence fallbackTempos;
    fallbackFile->findAllTempoEvents(fallbackTempos);
    EXPECT_EQ(fallbackTempos.getNumEvents(), 1);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    return true;
}

bool testMidiTrackExporterEditedStateAndRegressions()
{
    ProjectModel project;
    const auto primaryId = project.addTrack(TrackType::midi, "Edited State").id;
    const auto comparisonId = project.addTrack(TrackType::midi, "Comparison").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        primaryId,
        {
            MidiNote { 60, 70, 0.25, 0.50, 1, 1 },
            MidiNote { 64, 80, 1.00, 0.50, 1, 2 },
            MidiNote { 67, 90, 2.13, 0.25, 1, 3 },
        }));
    EXPECT_TRUE(project.replaceMidiNotes(comparisonId, { MidiNote { 60, 70, 0.25, 0.50, 1, 50 } }));

    auto* primary = project.findTrackById(primaryId);
    const auto* comparison = project.findTrackById(comparisonId);
    EXPECT_TRUE(primary != nullptr);
    EXPECT_TRUE(comparison != nullptr);

    dawhermes::core::MidiSourceMetadata sourceMetadata;
    sourceMetadata.sourceFileName = "source.mid";
    sourceMetadata.sourceFilePath = (createTempDirectory("midi-export-source") / "source.mid").string();
    sourceMetadata.ticksPerQuarterNote = 960;
    EXPECT_TRUE(project.setMidiSourceMetadata(primaryId, sourceMetadata));
    {
        std::ofstream sourceOut(sourceMetadata.sourceFilePath, std::ios::binary | std::ios::trunc);
        sourceOut << "source-midi-sentinel";
    }
    const auto sourceBefore = readBinaryFile(sourceMetadata.sourceFilePath);

    const auto beforeNotes = primary->midiNotes;
    const auto deletedId = primary->midiNotes.at(1).id;
    const auto movedId = primary->midiNotes.at(0).id;
    const auto quantizedId = primary->midiNotes.at(2).id;
    const auto comparisonBefore = comparison->midiNotes;
    const auto comparisonSummaryBefore = dawhermes::core::summarizeMidiComparison(
        dawhermes::core::compareMidiNotes(primary->midiNotes, comparison->midiNotes));
    const auto timelineGeometryBefore = dawhermes::core::buildTimelineLaneGeometry(project.tracks(), 30);

    auto created = MidiNote { 72, 100, 3.00, 0.25, 2, project.allocateMidiNoteId() };
    primary->midiNotes.push_back(created);
    EXPECT_EQ(dawhermes::core::deleteSelectedNotes(primary->midiNotes, { deletedId }), static_cast<std::size_t>(1));
    dawhermes::core::MoveSelectedNotesRequest move;
    move.selectedNoteIds = { movedId };
    move.requestedDeltaBeats = 0.25;
    move.requestedDeltaSemitones = 2;
    move.gridStepBeats = dawhermes::core::gridStepBeats(16);
    EXPECT_TRUE(dawhermes::core::moveSelectedNotes(primary->midiNotes, move).changed);
    dawhermes::core::ResizeSelectedNotesRequest resize;
    resize.selectedNoteIds = { movedId };
    resize.anchorNoteId = movedId;
    resize.requestedAnchorEndBeat = 1.25;
    resize.gridStepBeats = dawhermes::core::gridStepBeats(16);
    EXPECT_TRUE(dawhermes::core::resizeSelectedNotes(primary->midiNotes, resize).changed);
    EXPECT_EQ(dawhermes::core::applyVelocityToSelectedNotes(primary->midiNotes, { created.id }, 111), static_cast<std::size_t>(1));
    EXPECT_EQ(dawhermes::core::quantizeSelectedNoteStarts(primary->midiNotes, { quantizedId }, dawhermes::core::gridStepBeats(16)), static_cast<std::size_t>(1));
    const auto afterNotes = primary->midiNotes;

    dawhermes::core::MidiNoteSelectionState selection;
    selection.setSelection(primaryId, { movedId, created.id }, movedId);
    const auto selectedBefore = selection.selectedNoteIds();

    dawhermes::core::ProjectHistory history;
    history.pushExecuted(std::make_unique<TestReplaceMidiNotesCommand>(
        project,
        primaryId,
        "Edited MIDI Notes",
        beforeNotes,
        afterNotes));
    const auto historySizeBeforeExport = history.size();

    const auto tempDir = createTempDirectory("midi-export-edited");
    const auto outputPath = tempDir / "edited.mid";
    const auto exportResult = dawhermes::midi::exportMidiTrackToFile(*primary, outputPath);
    EXPECT_TRUE(exportResult.ok);
    EXPECT_EQ(history.size(), historySizeBeforeExport);
    EXPECT_EQ(selection.selectedNoteIds(), selectedBefore);
    EXPECT_EQ(project.findTrackById(comparisonId)->midiNotes, comparisonBefore);
    const auto timelineGeometryAfter = dawhermes::core::buildTimelineLaneGeometry(project.tracks(), 30);
    EXPECT_EQ(timelineGeometryAfter.size(), timelineGeometryBefore.size());
    for (std::size_t index = 0; index < timelineGeometryBefore.size(); ++index) {
        EXPECT_EQ(timelineGeometryAfter.at(index).trackId, timelineGeometryBefore.at(index).trackId);
        EXPECT_EQ(timelineGeometryAfter.at(index).trackType, timelineGeometryBefore.at(index).trackType);
        EXPECT_EQ(timelineGeometryAfter.at(index).rowIndex, timelineGeometryBefore.at(index).rowIndex);
        EXPECT_EQ(timelineGeometryAfter.at(index).y, timelineGeometryBefore.at(index).y);
        EXPECT_EQ(timelineGeometryAfter.at(index).height, timelineGeometryBefore.at(index).height);
    }

    const auto comparisonSummaryAfter = dawhermes::core::summarizeMidiComparison(
        dawhermes::core::compareMidiNotes(primary->midiNotes, comparisonBefore));
    EXPECT_EQ(comparisonSummaryBefore.unchangedCount, static_cast<std::size_t>(1));
    EXPECT_TRUE(comparisonSummaryAfter.removedCount >= static_cast<std::size_t>(1));

    EXPECT_EQ(readBinaryFile(sourceMetadata.sourceFilePath), sourceBefore);

    std::string parseError;
    const auto document = dawhermes::ui::parseMidiImportDocument(outputPath, parseError);
    EXPECT_TRUE(document.has_value());
    EXPECT_EQ(document->noteBearingTracks.size(), static_cast<std::size_t>(1));
    const auto& notes = document->noteBearingTracks.front().notes;
    EXPECT_EQ(notes.size(), static_cast<std::size_t>(3));
    EXPECT_TRUE(findNoteByPitch(notes, 64) == nullptr);

    const auto* moved = findNoteByPitch(notes, 62);
    const auto* quantized = findNoteByPitch(notes, 67);
    const auto* exportedCreated = findNoteByPitch(notes, 72);
    EXPECT_TRUE(moved != nullptr);
    EXPECT_TRUE(quantized != nullptr);
    EXPECT_TRUE(exportedCreated != nullptr);
    EXPECT_TRUE(approxEqual(moved->startBeat, 0.50));
    EXPECT_TRUE(approxEqual(moved->durationBeats, 0.75));
    EXPECT_TRUE(approxEqual(quantized->startBeat, 2.25));
    EXPECT_EQ(exportedCreated->velocity, 111);
    EXPECT_EQ(exportedCreated->channel, 2);

    EXPECT_TRUE(history.undo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, beforeNotes);
    EXPECT_TRUE(history.redo());
    EXPECT_EQ(project.findTrackById(primaryId)->midiNotes, afterNotes);

    std::error_code ec;
    std::filesystem::remove_all(tempDir, ec);
    std::filesystem::remove_all(std::filesystem::path(sourceMetadata.sourceFilePath).parent_path(), ec);
    return true;
}

bool testDeleteGroupTrackRemovesChildren()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto groupId = controller.addTrack(TrackType::group, "Hermes Group").id;
    const auto childAId = controller.addTrack(TrackType::midi, "Child A", groupId).id;
    const auto childBId = controller.addTrack(TrackType::midi, "Child B", groupId).id;

    const auto* childA = project.findTrackById(childAId);
    const auto* childB = project.findTrackById(childBId);
    EXPECT_TRUE(childA != nullptr);
    EXPECT_TRUE(childB != nullptr);

    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(3));
    EXPECT_EQ(childA->parentTrackId, groupId);
    EXPECT_EQ(childB->parentTrackId, groupId);

    EXPECT_TRUE(controller.deleteTrackById(groupId));
    EXPECT_TRUE(project.empty());
    return true;
}

bool testStubEngineNotImplemented()
{
    StubHermesEngine engine;
    const auto result = engine.drumsMakeMidiFromWav(
        dawhermes::hermes::HermesTrackContext { 1, "Audio Track 1", TrackType::audio, {} },
        HermesDrumsOptions {});

    EXPECT_EQ(result.status, HermesOperationStatus::notImplemented);
    EXPECT_TRUE(result.message.find("not integrated") != std::string::npos);
    return true;
}

bool testHermesCommandEnablementAudioVsMidi()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);
    const auto wavFixture = createTempWavFixture("command-availability");

    const auto& audio = controller.addTrack(TrackType::audio);
    controller.selectTrack(audio.id);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::drumsMakeMidiFromWav,
            project,
            selection),
        HermesCommandAvailability::requiresAudioFile);

    EXPECT_TRUE(project.setAudioSourcePath(audio.id, wavFixture.string()));

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::drumsMakeMidiFromWav,
            project,
            selection),
        HermesCommandAvailability::enabled);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::setFixBpm,
            project,
            selection),
        HermesCommandAvailability::requiresMidiTrack);

    const auto& midi = controller.addTrack(TrackType::midi);
    controller.selectTrack(midi.id);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::setFixBpm,
            project,
            selection),
        HermesCommandAvailability::enabled);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::drumsMakeMidiFromWav,
            project,
            selection),
        HermesCommandAvailability::requiresAudioTrack);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::synchronizeMidiWithWav,
            project,
            selection),
        HermesCommandAvailability::requiresAudioAndMidi);

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);

    return true;
}

bool testHermesCommandEnablementValidAudioMidiPair()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);
    const auto wavFixture = createTempWavFixture("pair-enable");

    const auto audioId = controller.addTrack(TrackType::audio, "Audio Pair").id;
    const auto midiId = controller.addTrack(TrackType::midi, "MIDI Pair").id;

    EXPECT_TRUE(project.setAudioSourcePath(audioId, wavFixture.string()));
    EXPECT_TRUE(controller.replaceMidiNotesOnTrack(
        midiId,
        { makeMidiNote(40, 100, 0.0, 0.5) }));

    controller.selectTrack(audioId);
    controller.toggleTrackSelection(midiId);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::bassMakeRepairMidiFromWav,
            project,
            selection),
        HermesCommandAvailability::enabled);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::synchronizeMidiWithWav,
            project,
            selection),
        HermesCommandAvailability::enabled);

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);

    return true;
}

bool testValidateAudioMidiPairContext()
{
    const auto wavFixture = createTempWavFixture("pair-context");

    dawhermes::hermes::HermesAudioMidiPairContext context;
    context.audioTrack = { 1, "Audio", TrackType::audio, wavFixture.string() };
    context.midiTrack = { 2, "MIDI", TrackType::midi, {} };
    context.midiNotes = { makeMidiNote(40, 100, 0.0, 0.5) };
    context.midiSourceMetadata.ticksPerQuarterNote = 960;
    context.midiSourceMetadata.tempoMap = { dawhermes::core::MidiTempoEvent {} };

    EXPECT_TRUE(dawhermes::hermes::validateTrackContextForAudioMidiPair(context).ok);

    auto invalid = context;
    invalid.midiNotes.clear();
    EXPECT_TRUE(!dawhermes::hermes::validateTrackContextForAudioMidiPair(invalid).ok);

    invalid = context;
    invalid.midiSourceMetadata.ticksPerQuarterNote = 0;
    EXPECT_TRUE(!dawhermes::hermes::validateTrackContextForAudioMidiPair(invalid).ok);

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testHermesCacheCreateAndClear()
{
    const auto tempRoot = std::filesystem::temp_directory_path() / "dawhermes-cache-test";
    std::filesystem::create_directories(tempRoot);

    EnvironmentGuard localAppDataGuard("LOCALAPPDATA");
    setEnvironment("LOCALAPPDATA", tempRoot.string());

    std::string error;
    const auto firstJobDir = dawhermes::hermes::createHermesJobDirectory("unit_test", error);
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(!firstJobDir.empty());
    EXPECT_TRUE(std::filesystem::exists(firstJobDir));

    const auto secondJobDir = dawhermes::hermes::createHermesJobDirectory("unit_test", error);
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(std::filesystem::exists(secondJobDir));

    const auto removed = dawhermes::hermes::clearHermesCache(error);
    EXPECT_TRUE(error.empty());
    EXPECT_TRUE(removed >= static_cast<std::size_t>(2));

    std::error_code ec;
    std::filesystem::remove_all(tempRoot, ec);
    return true;
}

bool testHermesJobRunnerSerializesJobs()
{
    dawhermes::hermes::HermesJobRunner runner([]() {
        return std::make_unique<FakeHermesEngine>();
    });

    std::mutex mutex;
    std::condition_variable condition;
    bool firstCompleted = false;
    bool firstSucceeded = false;
    bool secondCompleted = false;

    dawhermes::hermes::HermesJobRequest firstRequest;
    firstRequest.kind = dawhermes::hermes::HermesOperationKind::drumsExtraction;

    std::string submitError;
    EXPECT_TRUE(runner.submit(
        firstRequest,
        [&](dawhermes::hermes::HermesJobResult result) {
            std::lock_guard<std::mutex> lock(mutex);
            firstSucceeded = result.operationResult.isSuccess();
            firstCompleted = true;
            condition.notify_all();
        },
        submitError));

    dawhermes::hermes::HermesJobRequest secondRequest;
    secondRequest.kind = dawhermes::hermes::HermesOperationKind::drumsExtraction;

    std::string busyError;
    EXPECT_TRUE(!runner.submit(
        secondRequest,
        [&](dawhermes::hermes::HermesJobResult) {
            std::lock_guard<std::mutex> lock(mutex);
            secondCompleted = true;
            condition.notify_all();
        },
        busyError));
    EXPECT_TRUE(!busyError.empty());

    {
        std::unique_lock<std::mutex> lock(mutex);
        condition.wait_for(lock, std::chrono::seconds(3), [&]() { return firstCompleted; });
    }

    EXPECT_TRUE(firstCompleted);
    EXPECT_TRUE(firstSucceeded);
    EXPECT_TRUE(!secondCompleted);

    runner.stop();
    return true;
}

bool testMidiNoteEventValidation()
{
    std::string reason;
    EXPECT_TRUE(dawhermes::hermes::isValidMidiNoteEvent(makeMidiNote(36, 100, 0.0, 0.25), reason));
    EXPECT_TRUE(reason.empty());

    EXPECT_TRUE(!dawhermes::hermes::isValidMidiNoteEvent(makeMidiNote(-1, 100, 0.0, 0.25), reason));
    EXPECT_TRUE(reason.find("Pitch") != std::string::npos);

    EXPECT_TRUE(!dawhermes::hermes::isValidMidiNoteEvent(makeMidiNote(36, 0, 0.0, 0.25), reason));
    EXPECT_TRUE(reason.find("Velocity") != std::string::npos);

    EXPECT_TRUE(!dawhermes::hermes::isValidMidiNoteEvent(makeMidiNote(36, 100, -0.5, 0.25), reason));
    EXPECT_TRUE(reason.find("Start") != std::string::npos);

    EXPECT_TRUE(!dawhermes::hermes::isValidMidiNoteEvent(makeMidiNote(36, 100, 0.0, 0.0), reason));
    EXPECT_TRUE(reason.find("Duration") != std::string::npos);

    EXPECT_TRUE(!dawhermes::hermes::isValidMidiNoteEvent(makeMidiNote(36, 100, 0.0, 0.25, 17), reason));
    EXPECT_TRUE(reason.find("channel") != std::string::npos);
    return true;
}

bool testSeparateLayoutCreatesSeparateMidiTracks()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto wavFixture = createTempWavFixture("layout-separate");
    const auto& source = controller.addTrack(TrackType::audio, "Source Drums");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    HermesOperationResult operation = HermesOperationResult::success(
        "ok",
        HermesResultLayout::separateMidiTracks,
        std::vector<HermesGeneratedMidiTrack> {
            makeGeneratedTrack("Kick", { makeMidiNote(36, 100, 0.0, 0.25) }, "kick"),
            makeGeneratedTrack("Snare", { makeMidiNote(38, 102, 1.0, 0.25) }, "snare"),
            makeGeneratedTrack("Hat", { makeMidiNote(42, 95, 0.5, 0.125) }, "hat")
        },
        120.0,
        {});

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        operation,
        "group-separate",
        controller,
        applied);

    EXPECT_TRUE(applyResult.ok);
    EXPECT_EQ(applyResult.insertedTrackCount, static_cast<std::size_t>(3));
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(4));
    EXPECT_EQ(applied.trackIds.size(), static_cast<std::size_t>(3));

    for (const auto trackId : applied.trackIds) {
        const auto* track = project.findTrackById(trackId);
        EXPECT_TRUE(track != nullptr);
        EXPECT_EQ(track->type, TrackType::midi);
        EXPECT_EQ(track->generatedGroupId, std::string("group-separate"));
    }

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testGroupedLayoutCreatesSharedProjectHierarchy()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto wavFixture = createTempWavFixture("layout-grouped");
    const auto& source = controller.addTrack(TrackType::audio, "Grouped Source");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    HermesOperationResult operation = HermesOperationResult::success(
        "ok",
        HermesResultLayout::groupedMultitrack,
        std::vector<HermesGeneratedMidiTrack> {
            makeGeneratedTrack("Kick", { makeMidiNote(36, 100, 0.0, 0.25) }, "kick"),
            makeGeneratedTrack("Snare", { makeMidiNote(38, 101, 0.5, 0.25) }, "snare")
        },
        120.0,
        {});

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        operation,
        "group-multitrack",
        controller,
        applied);

    EXPECT_TRUE(applyResult.ok);
    EXPECT_EQ(applyResult.insertedTrackCount, static_cast<std::size_t>(2));
    EXPECT_EQ(applied.trackIds.size(), static_cast<std::size_t>(3));
    EXPECT_EQ(applied.midiTrackIds.size(), static_cast<std::size_t>(2));

    const auto* groupTrack = project.findTrackById(applied.trackIds.at(0));
    const auto* firstMidi = project.findTrackById(applied.trackIds.at(1));
    const auto* secondMidi = project.findTrackById(applied.trackIds.at(2));

    EXPECT_TRUE(groupTrack != nullptr);
    EXPECT_TRUE(firstMidi != nullptr);
    EXPECT_TRUE(secondMidi != nullptr);

    EXPECT_EQ(groupTrack->type, TrackType::group);
    EXPECT_EQ(firstMidi->type, TrackType::midi);
    EXPECT_EQ(secondMidi->type, TrackType::midi);
    EXPECT_EQ(firstMidi->parentTrackId, groupTrack->id);
    EXPECT_EQ(secondMidi->parentTrackId, groupTrack->id);
    EXPECT_EQ(groupTrack->generatedGroupId, std::string("group-multitrack"));
    EXPECT_EQ(firstMidi->generatedGroupId, std::string("group-multitrack"));
    EXPECT_EQ(secondMidi->generatedGroupId, std::string("group-multitrack"));

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testSingleTrackLayoutCreatesOneMidiTrack()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto wavFixture = createTempWavFixture("layout-single");
    const auto& source = controller.addTrack(TrackType::audio, "Single Source");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    HermesOperationResult operation = HermesOperationResult::success(
        "ok",
        HermesResultLayout::singleDrumTrack,
        std::vector<HermesGeneratedMidiTrack> {
            makeGeneratedTrack(
                "Drums",
                {
                    makeMidiNote(36, 100, 0.0, 0.25),
                    makeMidiNote(38, 100, 1.0, 0.25),
                },
                "drums")
        },
        120.0,
        {});

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        operation,
        "group-single",
        controller,
        applied);

    EXPECT_TRUE(applyResult.ok);
    EXPECT_EQ(applyResult.insertedTrackCount, static_cast<std::size_t>(1));
    EXPECT_EQ(applyResult.insertedNoteCount, static_cast<std::size_t>(2));
    EXPECT_EQ(applied.trackIds.size(), static_cast<std::size_t>(1));

    const auto* generatedTrack = project.findTrackById(applied.trackIds.front());
    EXPECT_TRUE(generatedTrack != nullptr);
    EXPECT_EQ(generatedTrack->type, TrackType::midi);
    EXPECT_EQ(countNotesOnTrack(*generatedTrack), static_cast<std::size_t>(2));

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testFailureLeavesNoPartialProjectResult()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto wavFixture = createTempWavFixture("rollback");
    const auto& source = controller.addTrack(TrackType::audio, "Rollback Source");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    const auto initialTrackCount = project.tracks().size();
    HermesOperationResult operation = HermesOperationResult::success(
        "ok",
        HermesResultLayout::separateMidiTracks,
        std::vector<HermesGeneratedMidiTrack> {
            makeGeneratedTrack("Broken", { makeMidiNote(200, 100, 0.0, 0.25) }, "broken")
        },
        120.0,
        {});

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        operation,
        "group-fail",
        controller,
        applied);

    EXPECT_TRUE(!applyResult.ok);
    EXPECT_EQ(project.tracks().size(), initialTrackCount);
    EXPECT_TRUE(applied.trackIds.empty());

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testEnabledEmptyLayerCanCreateTrack()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto wavFixture = createTempWavFixture("empty-enabled-layer");
    const auto& source = controller.addTrack(TrackType::audio, "Empty Enabled Source");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    HermesOperationResult operation = HermesOperationResult::success(
        "ok",
        HermesResultLayout::separateMidiTracks,
        std::vector<HermesGeneratedMidiTrack> {
            makeGeneratedTrack("Hat", {}, "hat", true, true)
        },
        120.0,
        {});

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        operation,
        "group-empty-enabled",
        controller,
        applied);

    EXPECT_TRUE(applyResult.ok);
    EXPECT_EQ(applyResult.insertedTrackCount, static_cast<std::size_t>(1));
    EXPECT_EQ(applyResult.insertedNoteCount, static_cast<std::size_t>(0));
    EXPECT_EQ(applied.trackIds.size(), static_cast<std::size_t>(1));
    EXPECT_EQ(applied.midiTrackIds.size(), static_cast<std::size_t>(1));

    const auto* generatedTrack = project.findTrackById(applied.trackIds.front());
    EXPECT_TRUE(generatedTrack != nullptr);
    EXPECT_EQ(generatedTrack->type, TrackType::midi);
    EXPECT_EQ(generatedTrack->midiNotes.size(), static_cast<std::size_t>(0));

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testZeroNoteResultWithoutMeaningfulLayerIsRejected()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto wavFixture = createTempWavFixture("empty-not-meaningful");
    const auto& source = controller.addTrack(TrackType::audio, "Empty Rejected Source");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    HermesOperationResult operation = HermesOperationResult::success(
        "ok",
        HermesResultLayout::separateMidiTracks,
        std::vector<HermesGeneratedMidiTrack> {
            makeGeneratedTrack("DisabledEmpty", {}, "hat", false, true)
        },
        120.0,
        {});

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        operation,
        "group-empty-rejected",
        controller,
        applied);

    EXPECT_TRUE(!applyResult.ok);
    EXPECT_TRUE(applied.trackIds.empty());
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1));

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testUndoRedoRestoresHermesResultWithoutReanalysis()
{
    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);

    const auto wavFixture = createTempWavFixture("undo-redo");
    const auto& source = controller.addTrack(TrackType::audio, "Undo Source");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    HermesOperationResult operation = HermesOperationResult::success(
        "ok",
        HermesResultLayout::groupedMultitrack,
        std::vector<HermesGeneratedMidiTrack> {
            makeGeneratedTrack(
                "Kick",
                {
                    makeMidiNote(36, 100, 0.0, 0.25),
                    makeMidiNote(36, 97, 1.0, 0.25),
                },
                "kick"),
            makeGeneratedTrack(
                "Snare",
                {
                    makeMidiNote(38, 103, 0.5, 0.25),
                    makeMidiNote(38, 99, 1.5, 0.25),
                },
                "snare")
        },
        120.0,
        {});

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        operation,
        "group-undo-redo",
        controller,
        applied);

    EXPECT_TRUE(applyResult.ok);
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(4));

    const auto expectedTrackCount = applied.tracks.size();
    const auto expectedNoteCount = dawhermes::hermes::countAppliedHermesNotes(applied);

    EXPECT_TRUE(dawhermes::hermes::undoAppliedHermesResult(controller, applied));
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1));

    EXPECT_TRUE(dawhermes::hermes::redoAppliedHermesResult(controller, applied));
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(4));
    EXPECT_EQ(applied.trackIds.size(), expectedTrackCount);
    EXPECT_EQ(dawhermes::hermes::countAppliedHermesNotes(applied), expectedNoteCount);

    for (std::size_t i = 0; i < applied.trackIds.size(); ++i) {
        const auto* restored = project.findTrackById(applied.trackIds.at(i));
        EXPECT_TRUE(restored != nullptr);
        EXPECT_EQ(restored->name, applied.tracks.at(i).displayName);
        EXPECT_EQ(restored->midiNotes.size(), applied.tracks.at(i).notes.size());
        EXPECT_EQ(restored->generatedGroupId, std::string("group-undo-redo"));
    }

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

bool testMissingEmbeddedRuntimeHandledSafely()
{
    const auto wavFixture = createTempWavFixture("runtime-safe");
    const auto tempWorkspace = std::filesystem::temp_directory_path() / "dawhermes-runtime-safe-workspace";
    std::filesystem::create_directories(tempWorkspace);

    EnvironmentGuard repoGuard("DAWHERMES_HERMES_REPO");
    EnvironmentGuard userProfileGuard("USERPROFILE");
    CurrentDirectoryGuard currentDirectoryGuard;

    setEnvironment("DAWHERMES_HERMES_REPO", (tempWorkspace / "missing-midi-cleaner").string());
    setEnvironment("USERPROFILE", (tempWorkspace / "missing-userprofile").string());
    std::filesystem::current_path(tempWorkspace);

    dawhermes::hermes::EmbeddedHermesEngine engine;
    dawhermes::hermes::HermesTrackContext context {
        1,
        "Audio Track 1",
        TrackType::audio,
        wavFixture.string()
    };

    const auto result = engine.drumsMakeMidiFromWav(context, dawhermes::hermes::HermesDrumsOptions {});
    EXPECT_EQ(result.status, HermesOperationStatus::unavailable);
    EXPECT_TRUE(result.message.find("midi-cleaner repository was not found") != std::string::npos);

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    std::filesystem::remove_all(tempWorkspace, ec);
    return true;
}

bool testComposerAssistantDefaultsAndDisabledProbe()
{
    dawhermes::hermes::ComposerAssistantConnector connector;
    const auto defaults = connector.defaultSettings();

    EXPECT_TRUE(!defaults.enabled);
    EXPECT_EQ(defaults.host, std::string("100.126.75.32"));
    EXPECT_EQ(defaults.port, 3456);

    const auto probeResult = connector.probe(defaults);
    EXPECT_TRUE(!probeResult.ok);
    EXPECT_TRUE(probeResult.message.find("disabled") != std::string::npos);
    return true;
}

bool testComposerAssistantPortValidation()
{
    dawhermes::hermes::ComposerAssistantConnector connector;
    auto settings = connector.defaultSettings();

    settings.port = 0;
    EXPECT_TRUE(!connector.validateSettings(settings).ok);

    settings.port = 70000;
    EXPECT_TRUE(!connector.validateSettings(settings).ok);

    settings.port = 3456;
    EXPECT_TRUE(connector.validateSettings(settings).ok);
    return true;
}

bool testEmbeddedHermesStructuredResultAndInsertion()
{
    const auto wavFixture = createSyntheticDrumWavFixture("embedded-integration");

    ProjectModel project;
    SelectionState selection;
    ProjectController controller(project, selection);
    const auto& source = controller.addTrack(TrackType::audio, "Embedded Source");
    EXPECT_TRUE(project.setAudioSourcePath(source.id, wavFixture.string()));

    dawhermes::hermes::EmbeddedHermesEngine engine;
    dawhermes::hermes::HermesDrumsOptions options;
    options.resultLayout = HermesResultLayout::singleDrumTrack;
    options.profile = HermesDrumsProfile::balanced;
    options.detectionMode = HermesDetectionMode::multiDetector;
    options.targetMapping = dawhermes::hermes::HermesTargetMapping::generalMidi;
    options.c1MidiNote = 36;
    options.createEmptyEnabledLayers = false;

    const dawhermes::hermes::HermesTrackContext context {
        source.id,
        source.name,
        TrackType::audio,
        wavFixture.string()
    };

    const auto start = std::chrono::steady_clock::now();
    const auto result = engine.drumsMakeMidiFromWav(context, options);
    const auto end = std::chrono::steady_clock::now();
    const auto durationMs = std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count();

    std::cout << "[EVIDENCE] EmbeddedHermesLayout=singleDrumTrack\n";
    std::cout << "[EVIDENCE] EmbeddedHermesDurationMs=" << durationMs << "\n";
    EXPECT_EQ(result.status, HermesOperationStatus::success);
    EXPECT_TRUE(!result.generatedMidiTracks.empty());

    for (const auto& generatedTrack : result.generatedMidiTracks) {
        std::cout << "[EVIDENCE] EmbeddedHermesTrack=" << generatedTrack.trackName
                  << " Notes=" << generatedTrack.notes.size() << "\n";
    }
    std::cout << "[EVIDENCE] EmbeddedHermesWarnings=" << result.warnings.size() << "\n";
    for (const auto& warning : result.warnings) {
        std::cout << "[EVIDENCE] EmbeddedHermesWarningText=" << warning << "\n";
    }

    std::size_t generatedNotes = 0;
    for (const auto& generatedTrack : result.generatedMidiTracks) {
        for (const auto& note : generatedTrack.notes) {
            std::string reason;
            EXPECT_TRUE(dawhermes::hermes::isValidMidiNoteEvent(note, reason));
            ++generatedNotes;
        }
    }
    EXPECT_TRUE(generatedNotes > 0);

    dawhermes::hermes::AppliedHermesResult applied;
    const auto applyResult = dawhermes::hermes::applyHermesResultToProject(
        context,
        result,
        "group-embedded-integration",
        controller,
        applied);
    EXPECT_TRUE(applyResult.ok);
    EXPECT_TRUE(applyResult.insertedTrackCount > 0);
    EXPECT_TRUE(applyResult.insertedNoteCount > 0);
    std::cout << "[EVIDENCE] EmbeddedHermesInsertedTracks=" << applyResult.insertedTrackCount << "\n";
    std::cout << "[EVIDENCE] EmbeddedHermesInsertedNotes=" << applyResult.insertedNoteCount << "\n";
    EXPECT_EQ(project.tracks().size(), static_cast<std::size_t>(1) + applyResult.insertedTrackCount);

    std::error_code ec;
    std::filesystem::remove(wavFixture, ec);
    return true;
}

std::string normalizePathString(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    const auto normalized = (ec ? path : absolute).lexically_normal();
    return normalized.generic_string();
}

bool isExistingFile(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_regular_file(path, ec);
}

std::string jsonEscape(const std::string& value)
{
    std::ostringstream escaped;
    for (unsigned char ch : value) {
        switch (ch) {
        case '"':
            escaped << "\\\"";
            break;
        case '\\':
            escaped << "\\\\";
            break;
        case '\b':
            escaped << "\\b";
            break;
        case '\f':
            escaped << "\\f";
            break;
        case '\n':
            escaped << "\\n";
            break;
        case '\r':
            escaped << "\\r";
            break;
        case '\t':
            escaped << "\\t";
            break;
        default:
            if (ch < 0x20) {
                escaped << "\\u"
                        << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(ch)
                        << std::dec << std::setfill(' ');
            } else {
                escaped << static_cast<char>(ch);
            }
            break;
        }
    }

    return escaped.str();
}

std::string quoted(const std::string& value)
{
    return "\"" + jsonEscape(value) + "\"";
}

std::string jsonBool(bool value)
{
    return value ? "true" : "false";
}

std::string formatDouble(double value, int precision = 6)
{
    std::ostringstream out;
    out << std::fixed << std::setprecision(precision) << value;
    return out.str();
}

std::string jsonIntArray(const std::vector<int>& values)
{
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }

        out << values.at(i);
    }
    out << "]";
    return out.str();
}

std::string jsonStringArray(const std::vector<std::string>& values)
{
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }

        out << quoted(values.at(i));
    }
    out << "]";
    return out.str();
}

const char* operationStatusToString(HermesOperationStatus status)
{
    switch (status) {
    case HermesOperationStatus::success:
        return "success";
    case HermesOperationStatus::notImplemented:
        return "notImplemented";
    case HermesOperationStatus::invalidInput:
        return "invalidInput";
    case HermesOperationStatus::unavailable:
        return "unavailable";
    default:
        return "unknown";
    }
}

bool tempoMapsEquivalent(
    const std::vector<dawhermes::core::MidiTempoEvent>& left,
    const std::vector<dawhermes::core::MidiTempoEvent>& right)
{
    if (left.size() != right.size()) {
        return false;
    }

    constexpr double epsilon = 1.0e-9;
    for (std::size_t i = 0; i < left.size(); ++i) {
        const auto& l = left.at(i);
        const auto& r = right.at(i);
        if (std::abs(l.beatPosition - r.beatPosition) > epsilon) {
            return false;
        }

        if (l.microsecondsPerQuarterNote != r.microsecondsPerQuarterNote) {
            return false;
        }
    }

    return true;
}

struct RealAssetInputs {
    std::filesystem::path bassMidi;
    std::filesystem::path bassWav;
    std::filesystem::path drumMidi;
    std::filesystem::path drumWav;
    std::filesystem::path synthMidi;
    std::filesystem::path synthWav;
    std::filesystem::path reportPath;
};

struct ParsedMidiAsset {
    std::filesystem::path filePath;
    dawhermes::ui::MidiImportDocument document;
    dawhermes::ui::MidiImportTrackCandidate selectedTrack;
    dawhermes::core::MidiSourceMetadata selectedMetadata;
};

struct OperationExecution {
    HermesOperationResult result;
    double wallClockDurationMs { 0.0 };
    std::size_t generatedNoteCount { 0 };
    bool hasZeroNoteTrack { false };
};

void printRealAssetsUsage()
{
    std::cout
        << "Usage: DAWHermesTests --m2-real-assets "
        << "--bass-midi <path> --bass-wav <path> "
        << "--drum-midi <path> --drum-wav <path> "
        << "--synth-midi <path> --synth-wav <path> "
        << "[--report <path>]\n";
}

std::optional<RealAssetInputs> parseRealAssetInputs(int argc, char* argv[], std::string& error)
{
    error.clear();

    std::map<std::string, std::string> raw;
    for (int argIndex = 2; argIndex < argc; ++argIndex) {
        const std::string key(argv[argIndex]);
        if (key == "--help") {
            error = "__help__";
            return std::nullopt;
        }

        if (key.rfind("--", 0) != 0) {
            error = "Unexpected argument: " + key;
            return std::nullopt;
        }

        if (argIndex + 1 >= argc) {
            error = "Missing value for argument: " + key;
            return std::nullopt;
        }

        raw[key] = argv[++argIndex];
    }

    RealAssetInputs inputs;
    const auto parseRequiredPath = [&](const std::string& key, std::filesystem::path& output) {
        const auto valueIt = raw.find(key);
        if (valueIt == raw.end()) {
            error = "Missing required argument: " + key;
            return false;
        }

        output = std::filesystem::path(valueIt->second).lexically_normal();
        if (!isExistingFile(output)) {
            error = "Required file was not found: " + key + " => " + output.string();
            return false;
        }

        return true;
    };

    if (!parseRequiredPath("--bass-midi", inputs.bassMidi)
        || !parseRequiredPath("--bass-wav", inputs.bassWav)
        || !parseRequiredPath("--drum-midi", inputs.drumMidi)
        || !parseRequiredPath("--drum-wav", inputs.drumWav)
        || !parseRequiredPath("--synth-midi", inputs.synthMidi)
        || !parseRequiredPath("--synth-wav", inputs.synthWav)) {
        return std::nullopt;
    }

    const auto reportIt = raw.find("--report");
    if (reportIt != raw.end()) {
        inputs.reportPath = std::filesystem::path(reportIt->second).lexically_normal();
    } else {
        inputs.reportPath = std::filesystem::path("build") / "m2-real-assets-report.json";
    }

    inputs.bassMidi = std::filesystem::absolute(inputs.bassMidi).lexically_normal();
    inputs.bassWav = std::filesystem::absolute(inputs.bassWav).lexically_normal();
    inputs.drumMidi = std::filesystem::absolute(inputs.drumMidi).lexically_normal();
    inputs.drumWav = std::filesystem::absolute(inputs.drumWav).lexically_normal();
    inputs.synthMidi = std::filesystem::absolute(inputs.synthMidi).lexically_normal();
    inputs.synthWav = std::filesystem::absolute(inputs.synthWav).lexically_normal();
    inputs.reportPath = std::filesystem::absolute(inputs.reportPath).lexically_normal();

    return inputs;
}

std::optional<ParsedMidiAsset> parseMidiAsset(const std::filesystem::path& filePath, std::string& error)
{
    std::string parseError;
    const auto parsed = dawhermes::ui::parseMidiImportDocument(filePath, parseError);
    if (!parsed.has_value()) {
        error = parseError.empty() ? "Failed to parse MIDI file." : parseError;
        return std::nullopt;
    }

    if (parsed->noteBearingTracks.empty()) {
        error = "MIDI file contained no note-bearing tracks.";
        return std::nullopt;
    }

    ParsedMidiAsset asset;
    asset.filePath = filePath;
    asset.document = *parsed;
    asset.selectedTrack = asset.document.noteBearingTracks.front();
    asset.selectedMetadata = dawhermes::ui::makeImportedMidiSourceMetadata(asset.document, asset.selectedTrack);
    return asset;
}

std::optional<dawhermes::ui::WavFileInspection> parseWavAsset(
    const std::filesystem::path& filePath,
    std::string& error)
{
    return dawhermes::ui::inspectWavFile(filePath, error);
}

std::set<std::string> collectHermesCacheDirectories(const std::filesystem::path& cacheRoot)
{
    std::set<std::string> directories;
    std::error_code ec;
    if (!std::filesystem::exists(cacheRoot, ec) || ec) {
        return directories;
    }

    for (const auto& entry : std::filesystem::directory_iterator(cacheRoot, ec)) {
        if (ec) {
            break;
        }

        if (!entry.is_directory()) {
            continue;
        }

        directories.insert(normalizePathString(entry.path()));
    }

    return directories;
}

dawhermes::hermes::HermesTrackContext makeAudioContext(
    std::uint64_t trackId,
    const std::string& trackName,
    const std::filesystem::path& audioPath)
{
    dawhermes::hermes::HermesTrackContext context;
    context.trackId = trackId;
    context.trackName = trackName;
    context.trackType = TrackType::audio;
    context.audioSourcePath = normalizePathString(audioPath);
    return context;
}

dawhermes::hermes::HermesAudioMidiPairContext makePairContext(
    std::uint64_t audioTrackId,
    const std::string& audioTrackName,
    const std::filesystem::path& audioPath,
    std::uint64_t midiTrackId,
    const std::string& midiTrackName,
    const ParsedMidiAsset& midiAsset)
{
    dawhermes::hermes::HermesAudioMidiPairContext context;
    context.audioTrack = makeAudioContext(audioTrackId, audioTrackName, audioPath);
    context.midiTrack.trackId = midiTrackId;
    context.midiTrack.trackName = midiTrackName;
    context.midiTrack.trackType = TrackType::midi;
    context.midiNotes = midiAsset.selectedTrack.notes;
    context.midiSourceMetadata = midiAsset.selectedMetadata;
    return context;
}

std::size_t countGeneratedNotes(const HermesOperationResult& result)
{
    std::size_t count = 0;
    for (const auto& track : result.generatedMidiTracks) {
        count += track.notes.size();
    }

    return count;
}

bool hasZeroNoteGeneratedTrack(const HermesOperationResult& result)
{
    for (const auto& track : result.generatedMidiTracks) {
        if (track.notes.empty()) {
            return true;
        }
    }

    return false;
}

template <typename OperationCallable>
OperationExecution executeTimedOperation(OperationCallable&& callable)
{
    const auto start = std::chrono::steady_clock::now();
    auto result = callable();
    const auto end = std::chrono::steady_clock::now();

    OperationExecution execution;
    execution.wallClockDurationMs =
        static_cast<double>(std::chrono::duration_cast<std::chrono::milliseconds>(end - start).count());
    execution.generatedNoteCount = countGeneratedNotes(result);
    execution.hasZeroNoteTrack = hasZeroNoteGeneratedTrack(result);
    execution.result = std::move(result);
    return execution;
}

bool generatedMetadataLineageMatches(const OperationExecution& execution, const ParsedMidiAsset& sourceMidi)
{
    if (!execution.result.isSuccess() || execution.result.generatedMidiTracks.empty()) {
        return false;
    }

    const auto expectedSourcePath = normalizePathString(sourceMidi.filePath);
    for (const auto& generatedTrack : execution.result.generatedMidiTracks) {
        if (!generatedTrack.midiSourceMetadata.has_value()) {
            return false;
        }

        const auto& metadata = generatedTrack.midiSourceMetadata.value();
        if (normalizePathString(std::filesystem::path(metadata.sourceFilePath)) != expectedSourcePath) {
            return false;
        }

        if (metadata.sourceFileName != sourceMidi.selectedMetadata.sourceFileName) {
            return false;
        }

        if (metadata.sourceTrackIndex != sourceMidi.selectedMetadata.sourceTrackIndex) {
            return false;
        }

        if (metadata.ticksPerQuarterNote != sourceMidi.selectedMetadata.ticksPerQuarterNote) {
            return false;
        }

        if (metadata.noteCount == 0) {
            return false;
        }
    }

    return true;
}

std::vector<std::string> collectNewDirectories(
    const std::set<std::string>& before,
    const std::set<std::string>& after)
{
    std::vector<std::string> created;
    for (const auto& path : after) {
        if (before.find(path) == before.end()) {
            created.push_back(path);
        }
    }

    return created;
}

std::string serializeMidiAssetReport(const ParsedMidiAsset& asset)
{
    std::ostringstream out;
    out << "{";
    out << "\"sourcePath\": " << quoted(normalizePathString(asset.filePath)) << ", ";
    out << "\"sourceFileName\": " << quoted(asset.document.sourceFileName) << ", ";
    out << "\"midiFileType\": " << asset.document.midiFileType << ", ";
    out << "\"ticksPerQuarterNote\": " << asset.document.ticksPerQuarterNote << ", ";
    out << "\"totalSourceTrackCount\": " << asset.document.totalSourceTrackCount << ", ";
    out << "\"noteBearingTrackCount\": " << asset.document.noteBearingTracks.size() << ", ";
    out << "\"selectedTrackIndex\": " << asset.selectedTrack.sourceTrackIndex << ", ";
    out << "\"selectedTrackName\": " << quoted(asset.selectedTrack.sourceTrackName) << ", ";
    out << "\"selectedTrackNoteCount\": " << asset.selectedTrack.notes.size() << ", ";
    out << "\"selectedTrackChannels\": " << jsonIntArray(asset.selectedTrack.channelsUsed) << ", ";
    out << "\"selectedTrackDurationBeats\": " << formatDouble(asset.selectedTrack.approximateDurationBeats) << ", ";
    out << "\"documentDurationBeats\": " << formatDouble(asset.document.approximateDurationBeats) << ", ";
    out << "\"tempoEventCount\": " << asset.document.tempoMap.size();
    out << "}";
    return out.str();
}

std::string serializeWavInspectionReport(
    const std::filesystem::path& wavPath,
    const dawhermes::ui::WavFileInspection& inspection)
{
    std::ostringstream out;
    out << "{";
    out << "\"sourcePath\": " << quoted(normalizePathString(wavPath)) << ", ";
    out << "\"sampleRate\": " << formatDouble(inspection.sampleRate, 2) << ", ";
    out << "\"channelCount\": " << inspection.channelCount << ", ";
    out << "\"bitsPerSample\": " << inspection.bitsPerSample << ", ";
    out << "\"durationSeconds\": " << formatDouble(inspection.durationSeconds, 6) << ", ";
    out << "\"fileSizeBytes\": " << inspection.fileSizeBytes;
    out << "}";
    return out.str();
}

std::string serializeOperationStatistics(const dawhermes::hermes::HermesOperationStatistics& statistics)
{
    std::ostringstream out;
    out << "{";
    out << "\"inputNoteCount\": " << statistics.inputNoteCount << ", ";
    out << "\"outputNoteCount\": " << statistics.outputNoteCount << ", ";
    out << "\"mergedCount\": " << statistics.mergedCount << ", ";
    out << "\"insertedCount\": " << statistics.insertedCount << ", ";
    out << "\"splitCount\": " << statistics.splitCount << ", ";
    out << "\"removedOrMutedCount\": " << statistics.removedOrMutedCount << ", ";
    out << "\"alignedCount\": " << statistics.alignedCount << ", ";
    out << "\"keepOriginalCount\": " << statistics.keepOriginalCount << ", ";
    out << "\"reviewTimingCount\": " << statistics.reviewTimingCount << ", ";
    out << "\"noAudioEvidenceCount\": " << statistics.noAudioEvidenceCount;
    out << "}";
    return out.str();
}

std::string serializeGeneratedTrackSummaries(const HermesOperationResult& result)
{
    std::ostringstream out;
    out << "[";
    for (std::size_t i = 0; i < result.generatedMidiTracks.size(); ++i) {
        if (i != 0) {
            out << ", ";
        }

        const auto& track = result.generatedMidiTracks.at(i);
        out << "{";
        out << "\"trackName\": " << quoted(track.trackName) << ", ";
        out << "\"semanticLayer\": " << quoted(track.semanticLayer) << ", ";
        out << "\"enabledLayer\": " << jsonBool(track.enabledLayer) << ", ";
        out << "\"emptyLayer\": " << jsonBool(track.emptyLayer) << ", ";
        out << "\"noteCount\": " << track.notes.size() << ", ";
        out << "\"hasSourceMetadata\": " << jsonBool(track.midiSourceMetadata.has_value());
        out << "}";
    }
    out << "]";
    return out.str();
}

std::string serializeOperationReport(
    const OperationExecution& execution,
    bool tempoPreserved,
    bool lineageMatches)
{
    std::vector<std::string> warnings = execution.result.warnings;

    std::ostringstream out;
    out << "{";
    out << "\"status\": " << quoted(operationStatusToString(execution.result.status)) << ", ";
    out << "\"message\": " << quoted(execution.result.message) << ", ";
    out << "\"wallClockDurationMs\": " << formatDouble(execution.wallClockDurationMs, 3) << ", ";
    out << "\"engineDurationMs\": " << formatDouble(execution.result.durationMs, 3) << ", ";
    out << "\"generatedTrackCount\": " << execution.result.generatedMidiTracks.size() << ", ";
    out << "\"generatedNoteCount\": " << execution.generatedNoteCount << ", ";
    out << "\"hasZeroNoteTrack\": " << jsonBool(execution.hasZeroNoteTrack) << ", ";
    out << "\"ticksPerQuarterNote\": " << execution.result.ticksPerQuarterNote << ", ";
    out << "\"tempoEventCount\": " << execution.result.tempoMap.size() << ", ";
    out << "\"tempoPreserved\": " << jsonBool(tempoPreserved) << ", ";
    out << "\"metadataLineageMatchesSource\": " << jsonBool(lineageMatches) << ", ";
    out << "\"warnings\": " << jsonStringArray(warnings) << ", ";
    out << "\"statistics\": " << serializeOperationStatistics(execution.result.statistics) << ", ";
    out << "\"generatedTracks\": " << serializeGeneratedTrackSummaries(execution.result);
    out << "}";
    return out.str();
}

bool writeReportFile(const std::filesystem::path& filePath, const std::string& report, std::string& error)
{
    error.clear();

    std::error_code ec;
    const auto parent = filePath.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            error = "Failed to create report directory: " + parent.string();
            return false;
        }
    }

    std::ofstream out(filePath, std::ios::out | std::ios::trunc);
    if (!out.good()) {
        error = "Failed to open report file for writing: " + filePath.string();
        return false;
    }

    out << report;
    out.close();
    return true;
}

bool testAudioDeviceServiceStateAndFormatting()
{
    const dawhermes::audio::AudioDeviceSettingsState settings {
        "<DEVICESETUP deviceType=\"Synthetic\" audioOutputDeviceName=\"Out\"/>"
    };
    const auto serialized =
        dawhermes::audio::serializeAudioDeviceSettings(settings);
    EXPECT_TRUE(
        serialized.find("DAWHermesAudioDeviceSettings")
        != std::string::npos);
    const auto restored =
        dawhermes::audio::deserializeAudioDeviceSettings(serialized);
    EXPECT_TRUE(restored.has_value());
    EXPECT_TRUE(
        restored->deviceStateXml.find("DEVICESETUP")
        != std::string::npos);
    EXPECT_TRUE(!dawhermes::audio::deserializeAudioDeviceSettings(
        "<invalid/>").has_value());

    int restoreCalls = 0;
    int fallbackCalls = 0;
    auto opened = dawhermes::audio::runAudioDeviceOpenSequence(
        true,
        [&]() {
            ++restoreCalls;
            return std::string("missing saved device");
        },
        [&]() {
            ++fallbackCalls;
            return std::string();
        });
    EXPECT_EQ(
        opened.state,
        dawhermes::audio::AudioDeviceOpenState::defaultFallback);
    EXPECT_EQ(restoreCalls, 1);
    EXPECT_EQ(fallbackCalls, 1);

    const auto unavailable =
        dawhermes::audio::runAudioDeviceOpenSequence(
            false,
            []() { return std::string(); },
            []() { return std::string("no device"); });
    EXPECT_EQ(
        unavailable.state,
        dawhermes::audio::AudioDeviceOpenState::noDevice);
    EXPECT_TRUE(!unavailable.deviceAvailable());

    dawhermes::audio::AudioDeviceStatus status;
    status.deviceType = "WASAPI";
    status.outputDeviceName = "Speakers";
    status.sampleRate = 48000.0;
    status.bufferSizeSamples = 256;
    status.activeOutputChannels = 2;
    status.outputLatencySamples = 128;
    status.open = true;
    status.running = true;
    const auto detail =
        dawhermes::audio::formatAudioDeviceStatus(status);
    EXPECT_TRUE(detail.find("WASAPI") != std::string::npos);
    EXPECT_TRUE(detail.find("Speakers") != std::string::npos);
    EXPECT_TRUE(detail.find("48000 Hz") != std::string::npos);
    EXPECT_TRUE(detail.find("256 samples") != std::string::npos);
    EXPECT_TRUE(
        dawhermes::audio::formatAudioDeviceSummary(status)
            .find("Speakers")
        != std::string::npos);

    dawhermes::audio::AudioDeviceService service(nullptr, false);
    EXPECT_EQ(service.registeredProjectCallbackCount(), 1);
    EXPECT_TRUE(!service.status().open);
    EXPECT_TRUE(!service.testOutput());

    dawhermes::audio::MidiAuditionEngine toneEngine;
    toneEngine.prepareForOfflineTesting(1000.0);
    EXPECT_TRUE(toneEngine.startTestTone(0.5));
    std::vector<float> left(500);
    std::vector<float> right(500);
    toneEngine.renderOfflineForTesting(
        left.data(),
        right.data(),
        499);
    EXPECT_TRUE(toneEngine.isTestToneActive());
    toneEngine.renderOfflineForTesting(
        left.data() + 499,
        right.data() + 499,
        1);
    EXPECT_TRUE(!toneEngine.isTestToneActive());
    EXPECT_TRUE(std::any_of(
        left.begin(),
        left.end(),
        [](float sample) { return std::abs(sample) > 0.001f; }));
    EXPECT_EQ(left, right);
    return true;
}

bool testProjectPlaybackSnapshotAndTempoPolicy()
{
    const auto firstWav = createSyntheticPlaybackWavFixture(
        "project-first",
        100,
        1,
        300);
    const auto secondWav = createSyntheticPlaybackWavFixture(
        "project-second",
        100,
        2,
        500);
    const auto firstBefore = readBinaryFile(firstWav);
    const auto secondBefore = readBinaryFile(secondWav);

    ProjectModel project;
    const auto groupId =
        project.addTrack(TrackType::group, "Group").id;
    const auto midiA =
        project.addTrack(TrackType::midi, "MIDI A", groupId).id;
    const auto midiB =
        project.addTrack(TrackType::midi, "MIDI B").id;
    const auto emptyMidi =
        project.addTrack(TrackType::midi, "Empty generated").id;
    project.findTrackById(emptyMidi)->generatedGroupId = "generated";
    const auto audioA =
        project.addTrack(TrackType::audio, "Audio A").id;
    const auto audioB =
        project.addTrack(TrackType::audio, "Audio B").id;
    const auto missing =
        project.addTrack(TrackType::audio, "Missing").id;

    EXPECT_TRUE(project.replaceMidiNotes(
        midiA,
        { MidiNote { 60, 100, 0.0, 8.0, 1, 1 } }));
    EXPECT_TRUE(project.replaceMidiNotes(
        midiB,
        { MidiNote { 64, 100, 1.0, 1.0, 1, 2 } }));
    dawhermes::core::MidiSourceMetadata tempoA;
    tempoA.containsExplicitTempoEvents = true;
    tempoA.tempoMap = {
        dawhermes::core::MidiTempoEvent { 0.0, 500000 }
    };
    dawhermes::core::MidiSourceMetadata tempoB;
    tempoB.containsExplicitTempoEvents = true;
    tempoB.tempoMap = {
        dawhermes::core::MidiTempoEvent { 0.0, 600000 }
    };
    EXPECT_TRUE(project.setMidiSourceMetadata(midiA, tempoA));
    EXPECT_TRUE(project.setMidiSourceMetadata(midiB, tempoB));
    EXPECT_TRUE(project.setAudioSourcePath(
        audioA,
        dawhermes::core::pathToUtf8(firstWav)));
    EXPECT_TRUE(project.setAudioSourcePath(
        audioB,
        dawhermes::core::pathToUtf8(secondWav)));
    EXPECT_TRUE(project.setAudioSourcePath(
        missing,
        dawhermes::core::pathToUtf8(
            firstWav.parent_path() / "missing.wav")));

    SelectionState irrelevantSelection;
    irrelevantSelection.selectTrack(audioB);
    const auto summary =
        dawhermes::audio::createProjectPlaybackSummary(project);
    EXPECT_TRUE(summary.playable);
    EXPECT_EQ(summary.midiTrackCount, static_cast<std::size_t>(2));
    EXPECT_EQ(summary.audioTrackCount, static_cast<std::size_t>(2));
    EXPECT_EQ(
        summary.identity.midiTrackIds,
        std::vector<std::uint64_t>({ midiA, midiB }));
    EXPECT_EQ(
        summary.identity.readableAudioTrackIds,
        std::vector<std::uint64_t>({ audioA, audioB }));
    EXPECT_TRUE(summary.conflictingExplicitMidiTempo);
    EXPECT_TRUE(!summary.diagnostic.empty());
    EXPECT_EQ(
        summary.tempoSource,
        dawhermes::audio::PlaybackTempoSource::explicitMidi);
    EXPECT_TRUE(approxEqual(summary.durationSeconds, 5.0));

    const auto snapshot =
        dawhermes::audio::createProjectPlaybackSnapshot(project);
    EXPECT_TRUE(snapshot.ok);
    EXPECT_EQ(snapshot.snapshot.midiTrackCount(), static_cast<std::size_t>(2));
    EXPECT_EQ(snapshot.snapshot.audioTrackCount(), static_cast<std::size_t>(2));
    EXPECT_EQ(
        snapshot.snapshot.midiTrackIds,
        std::vector<std::uint64_t>({ midiA, midiB }));
    EXPECT_EQ(
        snapshot.snapshot.projectTrackRoutingIdentity.front().trackId,
        groupId);
    EXPECT_EQ(
        snapshot.snapshot.audioStems[0].sourceTrackId,
        audioA);
    EXPECT_EQ(
        snapshot.snapshot.audioStems[1].sourceTrackId,
        audioB);
    EXPECT_TRUE(snapshot.snapshot.midi.has_value());
    EXPECT_TRUE(std::all_of(
        snapshot.snapshot.midi->events.begin(),
        snapshot.snapshot.midi->events.end(),
        [midiA, midiB](const auto& event) {
            return event.sourceTrackId == midiA
                || event.sourceTrackId == midiB;
        }));
    std::map<std::uint64_t, int> eventCountsByInstance;
    for (const auto& event :
         snapshot.snapshot.midi->events) {
        ++eventCountsByInstance[event.noteInstanceId];
    }
    EXPECT_TRUE(std::all_of(
        eventCountsByInstance.begin(),
        eventCountsByInstance.end(),
        [](const auto& entry) {
            return entry.second == 2;
        }));
    EXPECT_TRUE(!snapshot.skippedAudioTracks.empty());
    EXPECT_EQ(readBinaryFile(firstWav), firstBefore);
    EXPECT_EQ(readBinaryFile(secondWav), secondBefore);

    dawhermes::audio::SelectionPlaybackOptions limited;
    limited.maximumDecodedAudioBytes =
        dawhermes::audio::decodedWavBytes(300, 1).value();
    const auto budgeted =
        dawhermes::audio::createProjectPlaybackSnapshot(
            project,
            limited);
    EXPECT_TRUE(budgeted.ok);
    EXPECT_EQ(
        budgeted.snapshot.audioStems.size(),
        static_cast<std::size_t>(1));
    EXPECT_EQ(
        budgeted.snapshot.audioStems.front().sourceTrackId,
        audioA);
    EXPECT_TRUE(approxEqual(
        budgeted.snapshot.durationSeconds,
        5.0));
    EXPECT_TRUE(std::any_of(
        budgeted.skippedAudioTracks.begin(),
        budgeted.skippedAudioTracks.end(),
        [audioB](const auto& skipped) {
            return skipped.sourceTrackId == audioB
                && skipped.reason.find("memory limit")
                    != std::string::npos;
        }));

    ProjectModel detectedProject;
    const auto detectedA =
        detectedProject.addTrack(TrackType::audio, "Uncertain").id;
    const auto detectedB =
        detectedProject.addTrack(TrackType::audio, "Confident").id;
    EXPECT_TRUE(detectedProject.setAudioSourcePath(
        detectedA,
        dawhermes::core::pathToUtf8(firstWav)));
    EXPECT_TRUE(detectedProject.setAudioSourcePath(
        detectedB,
        dawhermes::core::pathToUtf8(secondWav)));
    dawhermes::audio::SelectionPlaybackOptions detectedOptions;
    detectedOptions.detectedWavBpms[detectedB] = 140.0;
    const auto detectedSummary =
        dawhermes::audio::createProjectPlaybackSummary(
            detectedProject,
            detectedOptions);
    EXPECT_EQ(
        detectedSummary.tempoSource,
        dawhermes::audio::PlaybackTempoSource::detectedWav);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::selectionSummaryBpm(
            0.0,
            detectedSummary),
        140.0,
        0.01));

    EXPECT_TRUE(
        dawhermes::audio::projectTransportCommandState(
            project,
            dawhermes::audio::TransportMode::stopped,
            summary.durationSeconds)
            .playEnabled);

    std::error_code error;
    std::filesystem::remove(firstWav, error);
    std::filesystem::remove(secondWav, error);
    return true;
}

bool testProjectMuteSoloRoutingRules()
{
    ProjectModel project;
    const auto groupA =
        project.addTrack(TrackType::group, "Group A").id;
    const auto midiA =
        project.addTrack(TrackType::midi, "MIDI A", groupA).id;
    const auto wavA =
        project.addTrack(TrackType::audio, "WAV A", groupA).id;
    const auto midiB =
        project.addTrack(TrackType::midi, "MIDI B").id;
    EXPECT_TRUE(project.replaceMidiNotes(
        midiA,
        { MidiNote { 60, 100, 0.0, 4.0, 1, 1 } }));
    EXPECT_TRUE(project.replaceMidiNotes(
        midiB,
        { MidiNote { 67, 100, 0.0, 2.0, 1, 2 } }));
    const auto durationBefore =
        dawhermes::audio::createProjectPlaybackSummary(project)
            .durationSeconds;

    auto routing =
        dawhermes::core::createProjectRoutingState(project);
    EXPECT_TRUE(routing.isAudible(midiA));
    EXPECT_TRUE(routing.isAudible(wavA));
    EXPECT_TRUE(routing.isAudible(midiB));
    EXPECT_TRUE(!routing.isAudible(groupA));

    EXPECT_TRUE(project.setTrackSoloed(midiA, true));
    routing = dawhermes::core::createProjectRoutingState(project);
    EXPECT_TRUE(routing.isAudible(midiA));
    EXPECT_TRUE(!routing.isAudible(wavA));
    EXPECT_TRUE(!routing.isAudible(midiB));

    EXPECT_TRUE(project.setTrackSoloed(midiA, false));
    EXPECT_TRUE(project.setTrackSoloed(groupA, true));
    routing = dawhermes::core::createProjectRoutingState(project);
    EXPECT_TRUE(routing.isAudible(midiA));
    EXPECT_TRUE(routing.isAudible(wavA));
    EXPECT_TRUE(!routing.isAudible(midiB));

    EXPECT_TRUE(project.setTrackMuted(groupA, true));
    routing = dawhermes::core::createProjectRoutingState(project);
    EXPECT_TRUE(!routing.isAudible(midiA));
    EXPECT_TRUE(!routing.isAudible(wavA));

    EXPECT_TRUE(project.setTrackMuted(groupA, false));
    EXPECT_TRUE(project.setTrackMuted(midiA, true));
    EXPECT_TRUE(project.setTrackSoloed(midiA, true));
    routing = dawhermes::core::createProjectRoutingState(project);
    EXPECT_TRUE(!routing.isAudible(midiA));
    EXPECT_TRUE(routing.isAudible(wavA));

    EXPECT_TRUE(project.setTrackSoloed(groupA, false));
    EXPECT_TRUE(project.setTrackSoloed(midiA, false));
    EXPECT_TRUE(project.setTrackMuted(midiA, false));
    EXPECT_TRUE(project.setTrackSoloed(midiA, true));
    EXPECT_TRUE(project.setTrackSoloed(midiB, true));
    routing = dawhermes::core::createProjectRoutingState(project);
    EXPECT_TRUE(routing.isAudible(midiA));
    EXPECT_TRUE(!routing.isAudible(wavA));
    EXPECT_TRUE(routing.isAudible(midiB));

    dawhermes::core::ProjectHistory history;
    const auto historySize = history.size();
    EXPECT_TRUE(project.setTrackMuted(wavA, true));
    EXPECT_TRUE(project.setTrackSoloed(wavA, true));
    EXPECT_EQ(history.size(), historySize);
    EXPECT_TRUE(approxEqual(
        dawhermes::audio::createProjectPlaybackSummary(project)
            .durationSeconds,
        durationBefore));
    return true;
}

bool testTimelineLoopModelAndOfflinePlayback()
{
    const auto snapped = dawhermes::core::createTimelineLoopRange(
        3.1,
        1.1,
        8.0,
        true,
        4);
    EXPECT_TRUE(snapped.has_value());
    EXPECT_TRUE(approxEqual(snapped->startBeat, 1.0));
    EXPECT_TRUE(approxEqual(snapped->endBeat, 3.0));

    const auto unsnapped =
        dawhermes::core::createTimelineLoopRange(
            1.123,
            1.124,
            8.0,
            false,
            16);
    EXPECT_TRUE(unsnapped.has_value());
    EXPECT_TRUE(
        unsnapped->lengthBeats()
        >= dawhermes::core::kMinimumUnsnappedLoopBeats
            - 1.0e-9);

    const auto resized =
        dawhermes::core::resizeTimelineLoopRange(
            snapped.value(),
            dawhermes::core::TimelineLoopEdge::start,
            2.8,
            8.0,
            true,
            4);
    EXPECT_TRUE(resized.has_value());
    EXPECT_TRUE(approxEqual(resized->startBeat, 2.0));
    EXPECT_TRUE(approxEqual(resized->endBeat, 3.0));

    const auto moved =
        dawhermes::core::moveTimelineLoopRange(
            snapped.value(),
            20.0,
            8.0,
            true,
            4);
    EXPECT_TRUE(moved.has_value());
    EXPECT_TRUE(approxEqual(moved->startBeat, 6.0));
    EXPECT_TRUE(approxEqual(moved->endBeat, 8.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::core::timelineLoopPlayStartBeat(
            0.25,
            snapped,
            true),
        1.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::core::timelineLoopPlayStartBeat(
            2.0,
            snapped,
            true),
        2.0));
    EXPECT_TRUE(approxEqual(
        dawhermes::core::timelineLoopPlayStartBeat(
            0.25,
            snapped,
            false),
        0.25));

    ProjectModel midiProject;
    const auto midiId =
        midiProject.addTrack(TrackType::midi, "Loop MIDI").id;
    EXPECT_TRUE(midiProject.replaceMidiNotes(
        midiId,
        { MidiNote { 60, 100, 0.0, 4.0, 1, 1 } }));
    auto midiSnapshot =
        dawhermes::audio::createProjectPlaybackSnapshot(
            midiProject);
    EXPECT_TRUE(midiSnapshot.ok);
    midiSnapshot.snapshot.durationSeconds = 3.0;

    dawhermes::audio::MidiAuditionEngine midiEngine;
    midiEngine.prepareForOfflineTesting(1000.0);
    midiEngine.setProjectRoutingState(
        dawhermes::core::createProjectRoutingState(
            midiProject));
    std::string error;
    EXPECT_TRUE(midiEngine.startPlayback(
        std::move(midiSnapshot.snapshot),
        0.5,
        error));
    midiEngine.setTimelineLoop(
        dawhermes::core::TimelineLoopRange { 1.0, 2.0 },
        true);
    std::vector<float> midiLeft(600);
    std::vector<float> midiRight(600);
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        600);
    EXPECT_TRUE(midiEngine.isPlaying());
    EXPECT_TRUE(approxEqual(
        midiEngine.playheadSeconds(),
        0.6,
        0.002));
    EXPECT_TRUE(midiEngine.activeVoiceCountForTesting() > 0);

    midiEngine.setProjectRoutingState({});
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_EQ(
        midiEngine.activeVoiceCountForTesting(),
        static_cast<std::size_t>(0));
    midiEngine.setProjectRoutingState(
        dawhermes::core::createProjectRoutingState(
            midiProject));
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_TRUE(midiEngine.activeVoiceCountForTesting() > 0);

    midiEngine.pause();
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_TRUE(midiEngine.isPaused());
    EXPECT_EQ(
        midiEngine.activeVoiceCountForTesting(),
        static_cast<std::size_t>(0));
    EXPECT_TRUE(midiEngine.resume(error));
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_TRUE(midiEngine.activeVoiceCountForTesting() > 0);
    midiEngine.seekTo(2.5);
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_TRUE(approxEqual(
        midiEngine.playheadSeconds(),
        0.501,
        0.002));
    const auto fiveSecondSeek =
        dawhermes::audio::seekTransportSeconds(
            midiEngine.playheadSeconds(),
            dawhermes::audio::kTransportSeekSeconds,
            midiEngine.totalDurationSeconds());
    midiEngine.seekTo(fiveSecondSeek);
    midiEngine.setTimelineLoop(
        dawhermes::core::TimelineLoopRange { 0.5, 1.0 },
        true);
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_TRUE(approxEqual(
        midiEngine.playheadSeconds(),
        0.251,
        0.002));
    midiEngine.stop();
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_TRUE(!midiEngine.isPlayheadVisible());
    EXPECT_EQ(
        midiEngine.activeVoiceCountForTesting(),
        static_cast<std::size_t>(0));
    midiEngine.panic();
    midiEngine.renderOfflineForTesting(
        midiLeft.data(),
        midiRight.data(),
        1);
    EXPECT_TRUE(!midiEngine.hasPreparedPlayback());

    dawhermes::audio::SelectionPlaybackSnapshot wavSnapshot;
    wavSnapshot.playheadTempoMap = {
        dawhermes::core::MidiTempoEvent { 0.0, 500000 }
    };
    wavSnapshot.durationSeconds = 3.0;
    dawhermes::audio::AudioStemPlaybackSnapshot stem;
    stem.sourceTrackId = 77;
    stem.sourceTrackName = "Loop WAV";
    stem.sourceSampleRate = 10.0;
    stem.frameCount = 30;
    stem.channels.resize(1);
    for (int index = 0; index < 30; ++index) {
        stem.channels.front().push_back(
            static_cast<float>(index) * 0.05f);
    }
    wavSnapshot.audioStems.push_back(std::move(stem));

    dawhermes::audio::MidiAuditionEngine wavEngine;
    wavEngine.prepareForOfflineTesting(10.0);
    wavEngine.setVolume(1.0f);
    dawhermes::core::ProjectRoutingState wavRouting;
    wavRouting.audibleTrackIds = { 77 };
    wavEngine.setProjectRoutingState(wavRouting);
    EXPECT_TRUE(wavEngine.startPlayback(
        std::move(wavSnapshot),
        0.5,
        error));
    wavEngine.setTimelineLoop(
        dawhermes::core::TimelineLoopRange { 1.0, 2.0 },
        true);
    std::vector<float> wavLeft(16);
    std::vector<float> wavRight(16);
    wavEngine.renderOfflineForTesting(
        wavLeft.data(),
        wavRight.data(),
        static_cast<int>(wavLeft.size()));
    EXPECT_TRUE(approxEqual(wavLeft[0], 0.25, 0.001));
    EXPECT_TRUE(approxEqual(wavLeft[5], 0.25, 0.001));
    EXPECT_TRUE(approxEqual(wavLeft[10], 0.25, 0.001));
    EXPECT_TRUE(approxEqual(
        wavEngine.playheadSeconds(),
        0.6,
        0.002));
    wavEngine.setProjectRoutingState({});
    float mutedLeft = 1.0f;
    float mutedRight = 1.0f;
    wavEngine.renderOfflineForTesting(
        &mutedLeft,
        &mutedRight,
        1);
    EXPECT_TRUE(approxEqual(mutedLeft, 0.0, 0.0001));
    wavEngine.setProjectRoutingState(wavRouting);
    float unmutedLeft = 0.0f;
    float unmutedRight = 0.0f;
    wavEngine.renderOfflineForTesting(
        &unmutedLeft,
        &unmutedRight,
        1);
    EXPECT_TRUE(std::abs(unmutedLeft) > 0.01f);
    return true;
}

int runM2RealAssetsMode(int argc, char* argv[])
{
    std::string parseError;
    const auto parsedInputs = parseRealAssetInputs(argc, argv, parseError);
    if (!parsedInputs.has_value()) {
        if (parseError == "__help__") {
            printRealAssetsUsage();
            return 0;
        }

        std::cerr << parseError << "\n";
        printRealAssetsUsage();
        return 2;
    }

    const auto inputs = parsedInputs.value();

    std::string error;
    const auto bassMidi = parseMidiAsset(inputs.bassMidi, error);
    if (!bassMidi.has_value()) {
        std::cerr << "Bass MIDI parse failed: " << error << "\n";
        return 3;
    }

    const auto drumMidi = parseMidiAsset(inputs.drumMidi, error);
    if (!drumMidi.has_value()) {
        std::cerr << "Drum MIDI parse failed: " << error << "\n";
        return 3;
    }

    const auto synthMidi = parseMidiAsset(inputs.synthMidi, error);
    if (!synthMidi.has_value()) {
        std::cerr << "Synth MIDI parse failed: " << error << "\n";
        return 3;
    }

    const auto bassWavInspection = parseWavAsset(inputs.bassWav, error);
    if (!bassWavInspection.has_value()) {
        std::cerr << "Bass WAV inspection failed: " << error << "\n";
        return 3;
    }

    const auto drumWavInspection = parseWavAsset(inputs.drumWav, error);
    if (!drumWavInspection.has_value()) {
        std::cerr << "Drum WAV inspection failed: " << error << "\n";
        return 3;
    }

    const auto synthWavInspection = parseWavAsset(inputs.synthWav, error);
    if (!synthWavInspection.has_value()) {
        std::cerr << "Synth WAV inspection failed: " << error << "\n";
        return 3;
    }

    auto bassPair = makePairContext(21, "Bass Audio", inputs.bassWav, 22, "Bass MIDI", bassMidi.value());
    auto synthPair = makePairContext(31, "Synth Audio", inputs.synthWav, 32, "Synth MIDI", synthMidi.value());
    const auto drumsContext = makeAudioContext(11, "Drum Audio", inputs.drumWav);

    const auto bassPairValidation = dawhermes::hermes::validateTrackContextForAudioMidiPair(bassPair);
    if (!bassPairValidation.ok) {
        std::cerr << "Bass pair validation failed: " << bassPairValidation.message << "\n";
        return 4;
    }

    const auto synthPairValidation = dawhermes::hermes::validateTrackContextForAudioMidiPair(synthPair);
    if (!synthPairValidation.ok) {
        std::cerr << "Synth pair validation failed: " << synthPairValidation.message << "\n";
        return 4;
    }

    const auto drumsContextValidation = dawhermes::hermes::validateTrackContextForDrums(drumsContext);
    if (!drumsContextValidation.ok) {
        std::cerr << "Drums context validation failed: " << drumsContextValidation.message << "\n";
        return 4;
    }

    const auto cacheRoot = dawhermes::hermes::hermesCacheRoot();
    const auto cacheBefore = collectHermesCacheDirectories(cacheRoot);

    dawhermes::hermes::EmbeddedHermesEngine engine;

    HermesDrumsOptions drumsOptions;
    drumsOptions.resultLayout = HermesResultLayout::singleDrumTrack;
    drumsOptions.profile = HermesDrumsProfile::balanced;
    drumsOptions.detectionMode = HermesDetectionMode::multiDetector;
    drumsOptions.targetMapping = dawhermes::hermes::HermesTargetMapping::generalMidi;
    drumsOptions.c1MidiNote = 36;
    drumsOptions.createEmptyEnabledLayers = false;

    HermesBassOptions bassOptions;
    bassOptions.resultTrackName = "Bass MIDI - Hermes Bass Repaired";

    HermesSyncOptions bassSyncOptions;
    bassSyncOptions.role = HermesSyncRole::bass;
    bassSyncOptions.preserveTempoMap = true;
    bassSyncOptions.resultTrackName = "Bass MIDI - Hermes Sync";

    HermesSyncOptions synthSyncOptions;
    synthSyncOptions.role = HermesSyncRole::synth;
    synthSyncOptions.preserveTempoMap = true;
    synthSyncOptions.resultTrackName = "Synth MIDI - Hermes Sync";

    const auto drumsExecution = executeTimedOperation([&]() {
        return engine.drumsMakeMidiFromWav(drumsContext, drumsOptions);
    });

    const auto bassRepairExecution = executeTimedOperation([&]() {
        return engine.bassMakeOrRepairMidiFromWav(bassPair, bassOptions);
    });

    const auto bassSyncExecution = executeTimedOperation([&]() {
        return engine.synchronizeMidiWithWav(bassPair, bassSyncOptions);
    });

    const auto synthSyncExecution = executeTimedOperation([&]() {
        return engine.synchronizeMidiWithWav(synthPair, synthSyncOptions);
    });

    const auto cacheAfter = collectHermesCacheDirectories(cacheRoot);
    const auto newCacheDirectories = collectNewDirectories(cacheBefore, cacheAfter);

    const bool operationsSuccessful =
        drumsExecution.result.isSuccess()
        && bassRepairExecution.result.isSuccess()
        && bassSyncExecution.result.isSuccess()
        && synthSyncExecution.result.isSuccess();

    const bool zeroNoteViolation =
        (drumsExecution.result.isSuccess() && (drumsExecution.generatedNoteCount == 0 || drumsExecution.hasZeroNoteTrack))
        || (bassRepairExecution.result.isSuccess() && (bassRepairExecution.generatedNoteCount == 0 || bassRepairExecution.hasZeroNoteTrack))
        || (bassSyncExecution.result.isSuccess() && (bassSyncExecution.generatedNoteCount == 0 || bassSyncExecution.hasZeroNoteTrack))
        || (synthSyncExecution.result.isSuccess() && (synthSyncExecution.generatedNoteCount == 0 || synthSyncExecution.hasZeroNoteTrack));

    const bool bassSyncTempoPreserved =
        bassSyncExecution.result.isSuccess()
        && tempoMapsEquivalent(bassMidi->selectedMetadata.tempoMap, bassSyncExecution.result.tempoMap);
    const bool synthSyncTempoPreserved =
        synthSyncExecution.result.isSuccess()
        && tempoMapsEquivalent(synthMidi->selectedMetadata.tempoMap, synthSyncExecution.result.tempoMap);

    const bool bassRepairLineageMatches = generatedMetadataLineageMatches(bassRepairExecution, bassMidi.value());
    const bool bassSyncLineageMatches = generatedMetadataLineageMatches(bassSyncExecution, bassMidi.value());
    const bool synthSyncLineageMatches = generatedMetadataLineageMatches(synthSyncExecution, synthMidi.value());

    const bool cacheCleanupOk = newCacheDirectories.empty();
    const bool overallSuccess =
        operationsSuccessful
        && !zeroNoteViolation
        && bassSyncTempoPreserved
        && synthSyncTempoPreserved
        && bassRepairLineageMatches
        && bassSyncLineageMatches
        && synthSyncLineageMatches
        && cacheCleanupOk;

    std::vector<std::string> reportWarnings;
    if (!operationsSuccessful) {
        reportWarnings.push_back("One or more Hermes operations failed.");
    }
    if (zeroNoteViolation) {
        reportWarnings.push_back("At least one successful operation produced zero-note output.");
    }
    if (!bassSyncTempoPreserved || !synthSyncTempoPreserved) {
        reportWarnings.push_back("Tempo map preservation check failed for at least one sync operation.");
    }
    if (!bassRepairLineageMatches || !bassSyncLineageMatches || !synthSyncLineageMatches) {
        reportWarnings.push_back("MIDI source metadata lineage check failed.");
    }
    if (!cacheCleanupOk) {
        reportWarnings.push_back("Hermes cache directory cleanup check failed.");
    }

    std::ostringstream report;
    report << "{\n";
    report << "  \"overallSuccess\": " << jsonBool(overallSuccess) << ",\n";
    report << "  \"warnings\": " << jsonStringArray(reportWarnings) << ",\n";
    report << "  \"inputs\": {\n";
    report << "    \"bassMidi\": " << quoted(normalizePathString(inputs.bassMidi)) << ",\n";
    report << "    \"bassWav\": " << quoted(normalizePathString(inputs.bassWav)) << ",\n";
    report << "    \"drumMidi\": " << quoted(normalizePathString(inputs.drumMidi)) << ",\n";
    report << "    \"drumWav\": " << quoted(normalizePathString(inputs.drumWav)) << ",\n";
    report << "    \"synthMidi\": " << quoted(normalizePathString(inputs.synthMidi)) << ",\n";
    report << "    \"synthWav\": " << quoted(normalizePathString(inputs.synthWav)) << "\n";
    report << "  },\n";
    report << "  \"midiMetadata\": {\n";
    report << "    \"bass\": " << serializeMidiAssetReport(bassMidi.value()) << ",\n";
    report << "    \"drums\": " << serializeMidiAssetReport(drumMidi.value()) << ",\n";
    report << "    \"synth\": " << serializeMidiAssetReport(synthMidi.value()) << "\n";
    report << "  },\n";
    report << "  \"wavMetadata\": {\n";
    report << "    \"bass\": " << serializeWavInspectionReport(inputs.bassWav, bassWavInspection.value()) << ",\n";
    report << "    \"drums\": " << serializeWavInspectionReport(inputs.drumWav, drumWavInspection.value()) << ",\n";
    report << "    \"synth\": " << serializeWavInspectionReport(inputs.synthWav, synthWavInspection.value()) << "\n";
    report << "  },\n";
    report << "  \"operations\": {\n";
    report << "    \"drumsExtraction\": " << serializeOperationReport(drumsExecution, false, false) << ",\n";
    report << "    \"bassRepair\": " << serializeOperationReport(bassRepairExecution, false, bassRepairLineageMatches) << ",\n";
    report << "    \"bassSync\": " << serializeOperationReport(bassSyncExecution, bassSyncTempoPreserved, bassSyncLineageMatches) << ",\n";
    report << "    \"synthSync\": " << serializeOperationReport(synthSyncExecution, synthSyncTempoPreserved, synthSyncLineageMatches) << "\n";
    report << "  },\n";
    report << "  \"checks\": {\n";
    report << "    \"operationsSuccessful\": " << jsonBool(operationsSuccessful) << ",\n";
    report << "    \"zeroNoteViolation\": " << jsonBool(zeroNoteViolation) << ",\n";
    report << "    \"bassSyncTempoPreserved\": " << jsonBool(bassSyncTempoPreserved) << ",\n";
    report << "    \"synthSyncTempoPreserved\": " << jsonBool(synthSyncTempoPreserved) << ",\n";
    report << "    \"bassRepairMetadataLineageMatches\": " << jsonBool(bassRepairLineageMatches) << ",\n";
    report << "    \"bassSyncMetadataLineageMatches\": " << jsonBool(bassSyncLineageMatches) << ",\n";
    report << "    \"synthSyncMetadataLineageMatches\": " << jsonBool(synthSyncLineageMatches) << ",\n";
    report << "    \"cacheCleanupOk\": " << jsonBool(cacheCleanupOk) << "\n";
    report << "  },\n";
    report << "  \"cache\": {\n";
    report << "    \"root\": " << quoted(normalizePathString(cacheRoot)) << ",\n";
    report << "    \"beforeCount\": " << cacheBefore.size() << ",\n";
    report << "    \"afterCount\": " << cacheAfter.size() << ",\n";
    report << "    \"newDirectories\": " << jsonStringArray(newCacheDirectories) << "\n";
    report << "  }\n";
    report << "}\n";

    std::string writeError;
    if (!writeReportFile(inputs.reportPath, report.str(), writeError)) {
        std::cerr << writeError << "\n";
        return 5;
    }

    std::cout << "[M2-REAL-ASSETS] Report=" << normalizePathString(inputs.reportPath) << "\n";
    std::cout << "[M2-REAL-ASSETS] OverallSuccess=" << (overallSuccess ? "true" : "false") << "\n";
    std::cout << "[M2-REAL-ASSETS] DrumsStatus=" << operationStatusToString(drumsExecution.result.status)
              << " Notes=" << drumsExecution.generatedNoteCount << "\n";
    std::cout << "[M2-REAL-ASSETS] BassRepairStatus=" << operationStatusToString(bassRepairExecution.result.status)
              << " Notes=" << bassRepairExecution.generatedNoteCount << "\n";
    std::cout << "[M2-REAL-ASSETS] BassSyncStatus=" << operationStatusToString(bassSyncExecution.result.status)
              << " Notes=" << bassSyncExecution.generatedNoteCount << "\n";
    std::cout << "[M2-REAL-ASSETS] SynthSyncStatus=" << operationStatusToString(synthSyncExecution.result.status)
              << " Notes=" << synthSyncExecution.generatedNoteCount << "\n";

    return overallSuccess ? 0 : 6;
}

juce::PluginDescription makeFakePluginDescription(
    juce::String name,
    int uniqueId)
{
    juce::PluginDescription description;
    description.name = std::move(name);
    description.descriptiveName = description.name;
    description.manufacturerName = "DAWHermes Tests";
    description.pluginFormatName = "VST3";
    description.category = "Instrument";
    description.fileOrIdentifier =
        "test://" + juce::String(uniqueId);
    description.uniqueId = uniqueId;
    description.isInstrument = true;
    description.numInputChannels = 0;
    description.numOutputChannels = 2;
    return description;
}

bool testVst3InstrumentAssignmentModel()
{
    ProjectModel project;
    const auto midiId =
        project.addTrack(TrackType::midi, "MIDI").id;
    const auto audioId =
        project.addTrack(TrackType::audio, "Audio").id;

    dawhermes::core::InstrumentAssignment assignment;
    assignment.kind = dawhermes::core::InstrumentKind::vst3;
    assignment.pluginIdentifier = "stable-id";
    assignment.pluginName = "Instrument";
    assignment.pluginManufacturer = "Maker";
    EXPECT_TRUE(project.setInstrumentAssignment(
        midiId,
        assignment));
    EXPECT_EQ(
        project.findTrackById(midiId)->instrument,
        assignment);
    EXPECT_TRUE(!project.setInstrumentAssignment(
        audioId,
        assignment));

    assignment.pluginIdentifier.clear();
    EXPECT_TRUE(!project.setInstrumentAssignment(
        midiId,
        assignment));
    EXPECT_TRUE(project.setInstrumentAssignment(
        midiId,
        {}));
    EXPECT_EQ(
        project.findTrackById(midiId)->instrument.kind,
        dawhermes::core::InstrumentKind::internalSynth);
    EXPECT_TRUE(
        project.findTrackById(midiId)
            ->instrument.pluginIdentifier.empty());
    return true;
}

bool testVst3CatalogFilteringAndOrdering()
{
    juce::Array<juce::PluginDescription> input;
    auto zebra = makeFakePluginDescription("Zebra", 30);
    auto alpha = makeFakePluginDescription("Alpha", 10);
    auto duplicate = alpha;
    duplicate.fileOrIdentifier = "another-local-path";
    auto effect = makeFakePluginDescription("Compressor", 20);
    effect.isInstrument = false;
    auto midiOnly = makeFakePluginDescription("MIDI Tool", 40);
    midiOnly.numOutputChannels = 0;
    auto vst2 = makeFakePluginDescription("Old", 50);
    vst2.pluginFormatName = "VST";
    input.add(zebra);
    input.add(alpha);
    input.add(duplicate);
    input.add(effect);
    input.add(midiOnly);
    input.add(vst2);

    const auto filtered =
        dawhermes::plugins::filterAndSortVst3Instruments(
            input);
    EXPECT_EQ(filtered.size(), static_cast<std::size_t>(2));
    EXPECT_EQ(filtered[0].name, juce::String("Alpha"));
    EXPECT_EQ(filtered[1].name, juce::String("Zebra"));
    return true;
}

bool testVst3CatalogStartupAndSettingsSafety()
{
    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3PluginCatalog catalog(
        temporarySettings.settings.get());
    EXPECT_TRUE(!catalog.scanStatus().running);
    EXPECT_EQ(
        catalog.formatManager().getNumFormats(),
        1);
    EXPECT_EQ(
        catalog.formatManager().getFormat(0)->getName(),
        juce::String("VST3"));
    EXPECT_TRUE(
        catalog.catalogFile().getFileName()
        == juce::String("vst3-instruments.xml"));
    EXPECT_TRUE(
        catalog.deadMansPedalFile().getFileName()
        == juce::String("vst3-scan-dead-man.txt"));
    EXPECT_TRUE(
        catalog.catalogFile().getParentDirectory()
        == catalog.deadMansPedalFile()
               .getParentDirectory());
    const auto repository =
        juce::File::getCurrentWorkingDirectory();
    EXPECT_TRUE(!catalog.catalogFile().isAChildOf(repository));
    EXPECT_TRUE(
        catalog.catalogFile().isAChildOf(
            temporarySettings.directory));
    return true;
}

bool testVst3DeadManRecoveryPlanning()
{
    const juce::StringArray candidates {
        "C:\\VST3\\Alpha.vst3",
        "C:\\VST3\\Beta.vst3",
        "C:\\VST3\\Gamma.vst3"
    };
    const juce::StringArray recoveryEntries {
        "c:/vst3/BETA.vst3",
        "C:\\VST3\\Beta.vst3",
        "C:\\Removed\\Stale.vst3",
        ""
    };

    const auto safeScan =
        dawhermes::plugins::prepareVst3ScanRecoveryPlan(
            candidates,
            recoveryEntries,
            false);
    EXPECT_EQ(
        safeScan.recoveredFailureCount,
        1);
    EXPECT_EQ(
        safeScan.staleRecoveryCount,
        1);
    EXPECT_EQ(
        safeScan.candidatesToScan.size(),
        2);
    EXPECT_TRUE(
        safeScan.candidatesToScan.contains(
            candidates[0]));
    EXPECT_TRUE(
        !safeScan.candidatesToScan.contains(
            candidates[1]));
    EXPECT_TRUE(
        safeScan.candidatesToScan.contains(
            candidates[2]));

    const auto deliberateRetry =
        dawhermes::plugins::prepareVst3ScanRecoveryPlan(
            candidates,
            recoveryEntries,
            true);
    EXPECT_EQ(
        deliberateRetry.recoveredFailureCount,
        1);
    EXPECT_EQ(
        deliberateRetry.staleRecoveryCount,
        1);
    EXPECT_EQ(
        deliberateRetry.candidatesToScan,
        candidates);
    return true;
}

bool testVst3PluginPositionInfo()
{
    dawhermes::plugins::PluginTransportPosition source;
    source.samplePosition = 48000;
    source.seconds = 1.0;
    source.ppqPosition = 2.5;
    source.bpm = 140.0;
    source.timeSignatureNumerator = 7;
    source.timeSignatureDenominator = 8;
    source.playing = true;
    source.looping = true;
    source.loopStartPpq = 2.0;
    source.loopEndPpq = 6.0;
    const auto info =
        dawhermes::plugins::makePluginPositionInfo(source);
    EXPECT_EQ(
        info.getTimeInSamples().orFallback(0),
        static_cast<std::int64_t>(48000));
    EXPECT_TRUE(std::abs(
        info.getTimeInSeconds().orFallback(0.0) - 1.0)
        < 0.0001);
    EXPECT_TRUE(std::abs(
        info.getPpqPosition().orFallback(0.0) - 2.5)
        < 0.0001);
    EXPECT_TRUE(std::abs(
        info.getBpm().orFallback(0.0) - 140.0)
        < 0.0001);
    EXPECT_TRUE(info.getIsPlaying());
    EXPECT_TRUE(info.getIsLooping());
    EXPECT_EQ(
        info.getTimeSignature()->numerator,
        7);
    EXPECT_EQ(
        info.getTimeSignature()->denominator,
        8);
    EXPECT_TRUE(std::abs(
        info.getLoopPoints()->ppqStart - 2.0)
        < 0.0001);
    EXPECT_TRUE(std::abs(
        info.getLoopPoints()->ppqEnd - 6.0)
        < 0.0001);
    return true;
}

bool testVst3IndependentRuntimeAndLatency()
{
    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3InstrumentHost host(
        temporarySettings.settings.get());
    host.prepareDevice(48000.0, 64);
    FakePluginState first;
    FakePluginState second;
    juce::String error;
    const auto description =
        makeFakePluginDescription("Independent", 101);
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        11,
        std::make_unique<FakeInstrumentInstance>(
            first,
            0),
        description,
        error));
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        22,
        std::make_unique<FakeInstrumentInstance>(
            second,
            5),
        description,
        error));
    EXPECT_EQ(
        host.activeInstanceCount(),
        static_cast<std::size_t>(2));
    EXPECT_EQ(host.maximumLatencySamples(), 5);
    EXPECT_EQ(first.prepareCount, 1);
    EXPECT_EQ(second.prepareCount, 1);

    FakePluginState invalidReplacement;
    auto invalidDescription =
        makeFakePluginDescription("Not Instrument", 102);
    invalidDescription.isInstrument = false;
    EXPECT_TRUE(!host.installPreparedInstanceForTesting(
        11,
        std::make_unique<FakeInstrumentInstance>(
            invalidReplacement,
            0),
        invalidDescription,
        error));
    EXPECT_TRUE(host.hasInstrument(11));
    EXPECT_EQ(
        host.activeInstanceCount(),
        static_cast<std::size_t>(2));

    FakePluginState failedPreparation;
    failedPreparation.throwOnPrepareCall = 1;
    EXPECT_TRUE(!host.installPreparedInstanceForTesting(
        11,
        std::make_unique<FakeInstrumentInstance>(
            failedPreparation,
            0,
            "Broken Replacement"),
        makeFakePluginDescription(
            "Broken Replacement",
            103),
        error));
    EXPECT_TRUE(host.hasInstrument(11));
    EXPECT_EQ(
        host.instrumentName(11),
        juce::String("Independent"));

    host.prepareDevice(44100.0, 32);
    EXPECT_EQ(first.prepareCount, 2);
    EXPECT_EQ(second.prepareCount, 2);
    EXPECT_EQ(
        host.instrumentName(11),
        juce::String("Independent"));

    dawhermes::plugins::PluginTransportPosition position;
    position.playing = true;
    position.bpm = 90.0;
    position.ppqPosition = 4.0;
    position.looping = true;
    position.loopStartPpq = 2.0;
    position.loopEndPpq = 6.0;
    host.beginAudioBlock(32, position);
    EXPECT_TRUE(host.addMidiEventFromAudioThread(
        11, true, 3, 60, 0.5f, 0));
    std::array<float, 32> left {};
    std::array<float, 32> right {};
    float* outputs[] { left.data(), right.data() };
    host.processAudioBlock(outputs, 2, 32, 1.0f);
    EXPECT_EQ(first.noteOnCount, 1);
    EXPECT_EQ(first.lastChannel, 3);
    EXPECT_EQ(first.lastVelocity, 64);
    EXPECT_EQ(second.noteOnCount, 0);
    EXPECT_TRUE(first.sawPlayHead);
    EXPECT_TRUE(std::abs(first.playHeadBpm - 90.0) < 0.0001);
    EXPECT_TRUE(first.playHeadLooping);
    for (int sample = 0; sample < 5; ++sample) {
        EXPECT_TRUE(std::abs(left[static_cast<std::size_t>(sample)])
                    < 0.00001f);
    }
    EXPECT_TRUE(std::abs(left[5]) > 0.1f);

    host.beginAudioBlock(8, position);
    EXPECT_TRUE(host.addMidiEventFromAudioThread(
        22, true, 7, 67, 1.0f, 0));
    std::array<float, 8> shortLeft {};
    std::array<float, 8> shortRight {};
    float* shortOutputs[] {
        shortLeft.data(),
        shortRight.data()
    };
    host.processAudioBlock(shortOutputs, 2, 8, 1.0f);
    EXPECT_EQ(second.noteOnCount, 1);
    EXPECT_EQ(second.lastChannel, 7);
    EXPECT_TRUE(first.active);
    EXPECT_TRUE(second.active);

    host.beginAudioBlock(8, position);
    host.resetAllFromAudioThread();
    host.processAudioBlock(
        shortOutputs,
        2,
        8,
        0.0f,
        false);
    EXPECT_TRUE(!first.active);
    EXPECT_TRUE(!second.active);
    EXPECT_TRUE(first.resetCount >= 16);
    EXPECT_TRUE(second.resetCount >= 16);

    const auto firstReleasesBeforeReplacement =
        first.releaseCount;
    FakePluginState replacement;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        11,
        std::make_unique<FakeInstrumentInstance>(
            replacement,
            0,
            "Replacement"),
        makeFakePluginDescription(
            "Replacement",
            104),
        error));
    EXPECT_EQ(
        host.instrumentName(11),
        juce::String("Replacement"));
    EXPECT_EQ(
        host.activeInstanceCount(),
        static_cast<std::size_t>(2));
    host.beginAudioBlock(1, position);
    host.collectRetiredRuntimes();
    EXPECT_TRUE(
        first.releaseCount
        > firstReleasesBeforeReplacement);

    host.useInternalSynth(11);
    EXPECT_TRUE(!host.hasInstrument(11));
    EXPECT_TRUE(host.hasInstrument(22));
    EXPECT_EQ(
        host.activeInstanceCount(),
        static_cast<std::size_t>(1));
    return true;
}

bool testVst3AudibilityAndExactBlockLengths()
{
    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3InstrumentHost host(
        temporarySettings.settings.get());
    host.prepareDevice(48000.0, 512);

    FakePluginState grouped;
    grouped.outputWhenInactive = true;
    grouped.outputLevel = 0.2f;
    FakePluginState independent;
    independent.outputWhenInactive = true;
    independent.outputLevel = 0.1f;
    juce::String error;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        2,
        std::make_unique<FakeInstrumentInstance>(
            grouped,
            0,
            "Grouped"),
        makeFakePluginDescription("Grouped", 401),
        error));
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        3,
        std::make_unique<FakeInstrumentInstance>(
            independent,
            0,
            "Independent"),
        makeFakePluginDescription("Independent", 402),
        error));

    ProjectModel project;
    const auto groupId =
        project.addTrack(TrackType::group, "Group").id;
    const auto groupedId =
        project.addTrack(
            TrackType::midi,
            "Grouped",
            groupId).id;
    const auto independentId =
        project.addTrack(
            TrackType::midi,
            "Independent").id;
    EXPECT_EQ(groupedId, static_cast<std::uint64_t>(2));
    EXPECT_EQ(independentId, static_cast<std::uint64_t>(3));
    EXPECT_TRUE(project.setTrackMuted(groupId, true));
    const auto groupRouting =
        dawhermes::core::createProjectRoutingState(
            project);

    dawhermes::plugins::PluginTransportPosition position;
    position.playing = true;
    std::array<float, 8> left {};
    std::array<float, 8> right {};
    float* outputs[] { left.data(), right.data() };
    EXPECT_TRUE(host.beginAudioBlock(
        8,
        position,
        &groupRouting));
    host.processAudioBlock(
        outputs,
        2,
        8,
        1.0f);
    for (const auto sample : left) {
        EXPECT_TRUE(std::abs(sample - 0.1f) < 0.00001f);
    }
    EXPECT_EQ(grouped.processCount, 1);
    EXPECT_EQ(independent.processCount, 1);

    EXPECT_TRUE(project.setTrackMuted(groupId, false));
    EXPECT_TRUE(project.setTrackSoloed(groupedId, true));
    const auto soloRouting =
        dawhermes::core::createProjectRoutingState(
            project);
    left.fill(0.0f);
    right.fill(0.0f);
    EXPECT_TRUE(host.beginAudioBlock(
        8,
        position,
        &soloRouting));
    host.processAudioBlock(
        outputs,
        2,
        8,
        1.0f);
    for (const auto sample : left) {
        EXPECT_TRUE(std::abs(sample - 0.2f) < 0.00001f);
    }

    FakePluginState lengthRecorder;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        4,
        std::make_unique<FakeInstrumentInstance>(
            lengthRecorder,
            0,
            "Length Recorder"),
        makeFakePluginDescription(
            "Length Recorder",
            403),
        error));
    const std::array<int, 5> lengths {
        1, 17, 64, 255, 512
    };
    std::array<float, 513> longLeft {};
    std::array<float, 513> longRight {};
    float* longOutputs[] {
        longLeft.data(),
        longRight.data()
    };
    for (const auto length : lengths) {
        EXPECT_TRUE(host.beginAudioBlock(
            length,
            position));
        host.processAudioBlock(
            longOutputs,
            2,
            length,
            1.0f);
    }
    EXPECT_EQ(
        lengthRecorder.processBlockSizes.size(),
        lengths.size());
    for (std::size_t index = 0;
         index < lengths.size();
         ++index) {
        EXPECT_EQ(
            lengthRecorder.processBlockSizes[index],
            lengths[index]);
    }
    const auto processCountBeforeOversized =
        lengthRecorder.processCount;
    EXPECT_TRUE(!host.beginAudioBlock(
        513,
        position));
    host.processAudioBlock(
        longOutputs,
        2,
        513,
        1.0f);
    EXPECT_EQ(
        lengthRecorder.processCount,
        processCountBeforeOversized);
    return true;
}

bool testVst3PluginDelayReset()
{
    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3InstrumentHost host(
        temporarySettings.settings.get());
    host.prepareDevice(1000.0, 64);
    FakePluginState delayed;
    delayed.active = true;
    delayed.outputLevel = 0.25f;
    FakePluginState latencyAnchor;
    latencyAnchor.outputLevel = 0.0f;
    juce::String error;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        1,
        std::make_unique<FakeInstrumentInstance>(
            delayed,
            0,
            "Delayed"),
        makeFakePluginDescription("Delayed", 411),
        error));
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        2,
        std::make_unique<FakeInstrumentInstance>(
            latencyAnchor,
            4,
            "Latency Anchor"),
        makeFakePluginDescription(
            "Latency Anchor",
            412),
        error));
    FakePluginState secondLatencyAnchor;
    secondLatencyAnchor.outputLevel = 0.0f;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        4,
        std::make_unique<FakeInstrumentInstance>(
            secondLatencyAnchor,
            4,
            "Second Latency Anchor"),
        makeFakePluginDescription(
            "Second Latency Anchor",
            414),
        error));

    dawhermes::plugins::PluginTransportPosition position;
    position.playing = true;
    std::array<float, 8> left {};
    std::array<float, 8> right {};
    float* outputs[] { left.data(), right.data() };
    EXPECT_TRUE(host.beginAudioBlock(2, position));
    host.processAudioBlock(outputs, 2, 2, 1.0f);
    EXPECT_TRUE(std::abs(left[0]) < 0.00001f);
    EXPECT_TRUE(std::abs(left[1]) < 0.00001f);

    delayed.active = false;
    left.fill(0.0f);
    right.fill(0.0f);
    EXPECT_TRUE(host.beginAudioBlock(4, position));
    host.resetAllFromAudioThread();
    host.processAudioBlock(outputs, 2, 4, 1.0f);
    for (int sample = 0; sample < 4; ++sample) {
        EXPECT_TRUE(std::abs(
            left[static_cast<std::size_t>(sample)])
            < 0.00001f);
    }

    delayed.active = true;
    EXPECT_TRUE(host.beginAudioBlock(2, position));
    host.processAudioBlock(outputs, 2, 2, 1.0f);
    FakePluginState addedRuntime;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        3,
        std::make_unique<FakeInstrumentInstance>(
            addedRuntime,
            0,
            "Added Runtime"),
        makeFakePluginDescription(
            "Added Runtime",
            413),
        error));
    delayed.active = false;
    left.fill(0.0f);
    right.fill(0.0f);
    EXPECT_TRUE(host.beginAudioBlock(4, position));
    host.processAudioBlock(outputs, 2, 4, 1.0f);
    for (int sample = 0; sample < 4; ++sample) {
        EXPECT_TRUE(std::abs(
            left[static_cast<std::size_t>(sample)])
            < 0.00001f);
    }

    delayed.active = true;
    EXPECT_TRUE(host.beginAudioBlock(2, position));
    host.processAudioBlock(outputs, 2, 2, 1.0f);
    delayed.active = false;
    host.useInternalSynth(2);
    left.fill(0.0f);
    right.fill(0.0f);
    EXPECT_TRUE(host.beginAudioBlock(4, position));
    host.processAudioBlock(outputs, 2, 4, 1.0f);
    for (int sample = 0; sample < 4; ++sample) {
        EXPECT_TRUE(std::abs(
            left[static_cast<std::size_t>(sample)])
            < 0.00001f);
    }
    return true;
}

bool testVst3LoopSegmentsAndPlayHead()
{
    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3InstrumentHost host(
        temporarySettings.settings.get());
    FakePluginState instrument;
    instrument.outputLevel = 0.2f;
    FakePluginState latencyAnchor;
    latencyAnchor.outputLevel = 0.0f;
    juce::String error;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        1,
        std::make_unique<FakeInstrumentInstance>(
            instrument,
            0,
            "Loop Recorder"),
        makeFakePluginDescription(
            "Loop Recorder",
            421),
        error));
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        99,
        std::make_unique<FakeInstrumentInstance>(
            latencyAnchor,
            4,
            "Latency Anchor"),
        makeFakePluginDescription(
            "Latency Anchor",
            422),
        error));

    ProjectModel project;
    const auto trackId =
        project.addTrack(
            TrackType::midi,
            "Loop MIDI").id;
    project.findTrackById(trackId)->midiNotes = {
        makeMidiNote(60, 100, 0.0, 1.0, 3)
    };
    auto snapshot =
        dawhermes::audio::createProjectPlaybackSnapshot(
            project);
    EXPECT_TRUE(snapshot.ok);

    dawhermes::audio::MidiAuditionEngine engine(&host);
    engine.prepareForOfflineTesting(1000.0);
    engine.setVolume(1.0f);
    engine.setProjectRoutingState(
        dawhermes::core::createProjectRoutingState(
            project));
    std::string playbackError;
    EXPECT_TRUE(engine.startPlayback(
        std::move(snapshot.snapshot),
        0.01,
        playbackError));
    engine.setTimelineLoop(
        dawhermes::core::TimelineLoopRange {
            0.02,
            0.04
        },
        true);

    std::array<float, 25> left {};
    std::array<float, 25> right {};
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        17);
    EXPECT_EQ(instrument.processCount, 2);
    EXPECT_EQ(
        instrument.processBlockSizes,
        std::vector<int>({ 10, 7 }));
    EXPECT_EQ(
        instrument.playHeadSamplePositions,
        std::vector<std::int64_t>({ 10, 10 }));
    EXPECT_EQ(
        instrument.noteOnOffsets,
        std::vector<int>({ 0, 0 }));
    for (const auto ppq : instrument.playHeadPpqs) {
        EXPECT_TRUE(std::abs(ppq - 0.02) < 0.0001);
    }
    for (int sample = 10; sample < 14; ++sample) {
        EXPECT_TRUE(std::abs(
            left[static_cast<std::size_t>(sample)])
            < 0.00001f);
    }
    EXPECT_TRUE(std::abs(left[14]) > 0.01f);

    instrument.processCount = 0;
    instrument.processBlockSizes.clear();
    instrument.playHeadSamplePositions.clear();
    instrument.playHeadPpqs.clear();
    instrument.noteOnOffsets.clear();
    engine.seekTo(0.01);
    left.fill(0.0f);
    right.fill(0.0f);
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        25);
    EXPECT_EQ(instrument.processCount, 3);
    EXPECT_EQ(
        instrument.processBlockSizes,
        std::vector<int>({ 10, 10, 5 }));
    EXPECT_EQ(
        instrument.playHeadSamplePositions,
        std::vector<std::int64_t>({ 10, 10, 10 }));
    EXPECT_EQ(
        instrument.noteOnOffsets,
        std::vector<int>({ 0, 0, 0 }));
    for (const auto ppq : instrument.playHeadPpqs) {
        EXPECT_TRUE(std::abs(ppq - 0.02) < 0.0001);
    }
    for (const auto start :
         std::array<int, 3> { 0, 10, 20 }) {
        for (int sample = start;
             sample < std::min(start + 4, 25);
             ++sample) {
            EXPECT_TRUE(std::abs(
                left[static_cast<std::size_t>(sample)])
                < 0.00001f);
        }
    }
    EXPECT_TRUE(std::abs(left[4]) > 0.01f);
    EXPECT_TRUE(std::abs(left[14]) > 0.01f);
    EXPECT_TRUE(std::abs(left[24]) > 0.01f);
    return true;
}

bool testVst3DryDelayDiscontinuities()
{
    const auto makePulseSnapshot = []() {
        dawhermes::audio::SelectionPlaybackSnapshot snapshot;
        dawhermes::audio::AudioStemPlaybackSnapshot stem;
        stem.sourceTrackId = 1;
        stem.sourceTrackName = "Pulse";
        stem.sourceSampleRate = 1000.0;
        stem.frameCount = 100;
        stem.channels = {
            std::vector<float>(100, 0.0f)
        };
        stem.channels[0][0] = 0.5f;
        snapshot.audioStems.push_back(std::move(stem));
        snapshot.durationSeconds = 0.1;
        return snapshot;
    };

    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3InstrumentHost host(
        temporarySettings.settings.get());
    FakePluginState latencyAnchor;
    latencyAnchor.outputLevel = 0.0f;
    juce::String error;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        99,
        std::make_unique<FakeInstrumentInstance>(
            latencyAnchor,
            4,
            "Latency Anchor"),
        makeFakePluginDescription(
            "Latency Anchor",
            431),
        error));
    dawhermes::audio::MidiAuditionEngine engine(&host);
    engine.prepareForOfflineTesting(1000.0);
    engine.setVolume(1.0f);
    dawhermes::core::ProjectRoutingState routing;
    routing.audibleTrackIds = { 1 };
    engine.setProjectRoutingState(std::move(routing));

    std::array<float, 8> left {};
    std::array<float, 8> right {};
    std::string playbackError;
    const auto expectSilent = [&]() {
        for (int sample = 0; sample < 4; ++sample) {
            if (std::abs(
                    left[static_cast<std::size_t>(sample)])
                >= 0.00001f) {
                return false;
            }
        }
        return true;
    };

    EXPECT_TRUE(engine.startPlayback(
        makePulseSnapshot(),
        playbackError));
    engine.renderOfflineForTesting(
        left.data(), right.data(), 2);
    engine.stop();
    engine.renderOfflineForTesting(
        left.data(), right.data(), 1);
    EXPECT_TRUE(engine.startPlayback(
        makePulseSnapshot(),
        playbackError));
    left.fill(0.0f);
    right.fill(0.0f);
    engine.renderOfflineForTesting(
        left.data(), right.data(), 4);
    EXPECT_TRUE(expectSilent());

    engine.stop();
    engine.renderOfflineForTesting(
        left.data(), right.data(), 1);
    EXPECT_TRUE(engine.startPlayback(
        makePulseSnapshot(),
        playbackError));
    engine.renderOfflineForTesting(
        left.data(), right.data(), 2);
    engine.seekTo(0.02);
    left.fill(0.0f);
    right.fill(0.0f);
    engine.renderOfflineForTesting(
        left.data(), right.data(), 4);
    EXPECT_TRUE(expectSilent());

    engine.stop();
    engine.renderOfflineForTesting(
        left.data(), right.data(), 1);
    EXPECT_TRUE(engine.startPlayback(
        makePulseSnapshot(),
        playbackError));
    engine.renderOfflineForTesting(
        left.data(), right.data(), 2);
    engine.pause();
    engine.renderOfflineForTesting(
        left.data(), right.data(), 1);
    EXPECT_TRUE(engine.resume(playbackError));
    left.fill(0.0f);
    right.fill(0.0f);
    engine.renderOfflineForTesting(
        left.data(), right.data(), 4);
    EXPECT_TRUE(expectSilent());

    engine.stop();
    engine.renderOfflineForTesting(
        left.data(), right.data(), 1);
    EXPECT_TRUE(engine.startPlayback(
        makePulseSnapshot(),
        playbackError));
    engine.renderOfflineForTesting(
        left.data(), right.data(), 2);
    FakePluginState replacement;
    replacement.outputLevel = 0.0f;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        99,
        std::make_unique<FakeInstrumentInstance>(
            replacement,
            4,
            "Replacement Anchor"),
        makeFakePluginDescription(
            "Replacement Anchor",
            432),
        error));
    left.fill(0.0f);
    right.fill(0.0f);
    engine.renderOfflineForTesting(
        left.data(), right.data(), 4);
    EXPECT_TRUE(expectSilent());
    return true;
}

bool testVst3DeviceReprepareFailureFallsBack()
{
    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3InstrumentHost host(
        temporarySettings.settings.get());
    host.prepareDevice(48000.0, 64);

    FakePluginState failing;
    failing.throwOnPrepareCall = 2;
    juce::String error;
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        42,
        std::make_unique<FakeInstrumentInstance>(
            failing,
            0,
            "Device Sensitive"),
        makeFakePluginDescription(
            "Device Sensitive",
            301),
        error));
    EXPECT_TRUE(host.hasInstrument(42));

    host.prepareDevice(44100.0, 128);
    EXPECT_TRUE(!host.hasInstrument(42));
    EXPECT_EQ(
        host.activeInstanceCount(),
        static_cast<std::size_t>(0));
    const auto failures =
        host.takeRuntimeFailures();
    EXPECT_EQ(
        failures.size(),
        static_cast<std::size_t>(1));
    EXPECT_EQ(
        failures.front().trackId,
        static_cast<std::uint64_t>(42));
    EXPECT_EQ(
        failures.front().instrumentName,
        juce::String("Device Sensitive"));
    EXPECT_TRUE(failures.front().reason.isNotEmpty());
    EXPECT_TRUE(host.takeRuntimeFailures().empty());
    return true;
}

bool testVst3WholeProjectRoutingAndFallback()
{
    TemporaryPluginSettings temporarySettings;
    dawhermes::plugins::Vst3InstrumentHost host(
        temporarySettings.settings.get());
    FakePluginState pluginState;
    juce::String error;
    const auto description =
        makeFakePluginDescription("Project Fake", 202);
    EXPECT_TRUE(host.installPreparedInstanceForTesting(
        1,
        std::make_unique<FakeInstrumentInstance>(
            pluginState,
            0),
        description,
        error));

    ProjectModel project;
    const auto pluginTrackId =
        project.addTrack(TrackType::midi, "Plugin").id;
    const auto internalTrackId =
        project.addTrack(TrackType::midi, "Internal").id;
    EXPECT_EQ(pluginTrackId, static_cast<std::uint64_t>(1));
    project.findTrackById(pluginTrackId)->midiNotes = {
        makeMidiNote(60, 100, 0.0, 2.0, 3)
    };
    project.findTrackById(internalTrackId)->midiNotes = {
        makeMidiNote(67, 90, 0.0, 2.0, 5)
    };
    dawhermes::core::InstrumentAssignment assignment;
    assignment.kind = dawhermes::core::InstrumentKind::vst3;
    assignment.pluginIdentifier =
        description.createIdentifierString().toStdString();
    assignment.pluginName = "Project Fake";
    EXPECT_TRUE(project.setInstrumentAssignment(
        pluginTrackId,
        assignment));

    auto snapshot =
        dawhermes::audio::createProjectPlaybackSnapshot(
            project);
    EXPECT_TRUE(snapshot.ok);
    dawhermes::audio::MidiAuditionEngine engine(&host);
    engine.prepareForOfflineTesting(1000.0);
    engine.setProjectRoutingState(
        dawhermes::core::createProjectRoutingState(project));
    std::string playbackError;
    EXPECT_TRUE(engine.startPlayback(
        std::move(snapshot.snapshot),
        playbackError));
    std::array<float, 64> left {};
    std::array<float, 64> right {};
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        static_cast<int>(left.size()));
    EXPECT_EQ(pluginState.noteOnCount, 1);
    EXPECT_EQ(pluginState.lastChannel, 3);
    EXPECT_EQ(
        engine.activeVoiceCountForTesting(),
        static_cast<std::size_t>(1));

    EXPECT_TRUE(project.setTrackMuted(pluginTrackId, true));
    engine.setProjectRoutingState(
        dawhermes::core::createProjectRoutingState(project));
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        1);
    EXPECT_TRUE(!pluginState.active);

    EXPECT_TRUE(project.setTrackMuted(pluginTrackId, false));
    engine.setProjectRoutingState(
        dawhermes::core::createProjectRoutingState(project));
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        1);
    EXPECT_TRUE(pluginState.active);

    const auto resetsBeforeLoop =
        pluginState.resetCount;
    engine.setTimelineLoop(
        dawhermes::core::TimelineLoopRange {
            0.0,
            0.1
        },
        true);
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        64);
    EXPECT_TRUE(pluginState.resetCount > resetsBeforeLoop);

    engine.pause();
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        1);
    EXPECT_TRUE(!pluginState.active);

    host.useInternalSynth(pluginTrackId);
    auto fallbackSnapshot =
        dawhermes::audio::createProjectPlaybackSnapshot(
            project);
    EXPECT_TRUE(fallbackSnapshot.ok);
    engine.stop();
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        1);
    EXPECT_TRUE(engine.startPlayback(
        std::move(fallbackSnapshot.snapshot),
        playbackError));
    engine.renderOfflineForTesting(
        left.data(),
        right.data(),
        1);
    EXPECT_EQ(
        engine.activeVoiceCountForTesting(),
        static_cast<std::size_t>(2));
    return true;
}

}  // namespace

int main(int argc, char* argv[])
{
    if (argc > 1 && std::string(argv[1]) == "--m2-real-assets") {
        return runM2RealAssetsMode(argc, argv);
    }

    const auto skipEmbeddedHermesIntegration =
        std::any_of(argv + 1, argv + argc, [](const char* argument) {
            return std::string(argument) == "--skip-embedded-hermes-integration";
        });

    struct NamedTest {
        const char* name;
        bool (*func)();
    };

    const std::vector<NamedTest> tests {
        { "Track creation", testTrackCreationAudioAndMidi },
        { "Audio source model compatibility", testAudioSourceAssignment },
        { "Direct audio import command policy", testDirectAudioImportCommandPolicy },
        { "Single audio import metadata/playback/history", testSingleAudioTrackImportMetadataPlaybackAndHistory },
        { "Batch audio import invalid-skip/selection/history", testBatchAudioTrackImportSkipsInvalidAndRestoresSelection },
        { "Unicode-safe audio import/playback/history", testUnicodeSafeAudioImportPlaybackAndHistory },
        { "MIDI note replacement", testMidiNoteReplacement },
        { "Stable track IDs", testStableTrackIds },
        { "Selection state", testSelectionState },
        { "Selection multi-toggle", testSelectionStateMultiToggle },
        { "Delete selected track", testDeleteSelectedTrack },
        { "Delete unselected track", testDeleteUnselectedTrack },
        { "Empty project safety", testEmptyProjectSafety },
        { "C1 range validation", testC1RangeValidation },
        { "BPM validation", testBpmValidation },
        { "Drums enum validation", testDrumsEnumsValidation },
        { "Drums track context validation", testDrumsTrackContextValidation },
        { "Main layout geometry", testMainLayoutGeometry },
        { "Panel layout state clamp/roundtrip", testPanelLayoutStateRoundTripAndClamping },
        { "Timeline viewport mapping and zoom", testTimelineViewportMappingAndZoom },
        { "MIDI time map bar and grid", testMidiTimeMapBarAndGridResolution },
        { "Timeline lanes and note culling", testTimelineLaneGeometryAndVisibleNoteCulling },
        { "MIDI comparison tolerance", testMidiComparisonToleranceClassification },
        { "MIDI note selection state stable IDs", testMidiNoteSelectionStateUsesStableIds },
        { "MIDI note marquee selection geometry", testMidiNoteMarqueeSelectionGeometry },
        { "MIDI note creation defaults/history", testMidiNoteCreationDefaultsAndHistory },
        { "MIDI note deletion history/cleanup", testMidiNoteDeletionHistoryAndSelectionCleanup },
        { "MIDI editing refresh regressions", testMidiEditingRefreshRegressions },
        { "MIDI note mouse move snap/clamp/history", testMidiNoteMouseMoveSnapClampAndHistory },
        { "MIDI note multi-move offsets/clamps", testMidiNoteMultiMoveOffsetsClampsAndClickedSelection },
        { "MIDI note move snap denominators", testMidiNoteMoveSnapDenominators },
        { "MIDI note resize snap/clamp/history", testMidiNoteResizeSnapClampAndHistory },
        { "MIDI note keyboard nudge behaviour", testMidiNoteKeyboardNudgeBehaviour },
        { "MIDI snap control editing grid behaviour", testMidiSnapControlEditingGridBehaviour },
        { "MIDI velocity editing behaviour", testMidiVelocityEditingBehaviour },
        { "MIDI quantize selected notes behaviour", testMidiQuantizeSelectedNotesBehaviour },
        { "MIDI playback event generation", testMidiPlaybackEventGenerationFromEditedNotes },
        { "MIDI playback tempo timing and ordering", testMidiPlaybackTempoTimingAndOrdering },
        { "MIDI playback snapshot and no-history behaviour", testMidiPlaybackSnapshotAndNoHistoryBehaviour },
        { "MIDI playback transport command state", testMidiPlaybackTransportCommandState },
        { "Selection playback MIDI/audio policies", testSelectionPlaybackMidiAudioPolicies },
        { "Selection playback WAV safety and timing", testSelectionPlaybackWavSafetyAndTiming },
        { "Selection playback decoded-audio budget", testSelectionPlaybackDecodedAudioBudget },
        { "Selection playback transport stop/panic state", testSelectionPlaybackTransportAndStopPanicState },
        { "Stopped playable-selection identity and seek preservation", testStoppedPlayableSelectionIdentityAndSeekPreservation },
        { "Stopped transport resynchronizes current selection", testStoppedTransportResynchronizesCurrentSelection },
        { "Transport pause resume seek and counter", testTransportPauseResumeSeekAndCounter },
        { "Playback tempo source priority and duration", testPlaybackTempoSourcePriorityAndDuration },
        { "Playback summary durations and audio-only tempo", testPlaybackSummaryDurationsAndAudioOnlyTempo },
        { "Audio device service state and formatting", testAudioDeviceServiceStateAndFormatting },
        { "Whole-project playback snapshot and tempo policy", testProjectPlaybackSnapshotAndTempoPolicy },
        { "Project Mute/Solo routing rules", testProjectMuteSoloRoutingRules },
        { "Timeline loop model and offline playback", testTimelineLoopModelAndOfflinePlayback },
        { "VST3 instrument assignment model", testVst3InstrumentAssignmentModel },
        { "VST3 catalog filtering and ordering", testVst3CatalogFilteringAndOrdering },
        { "VST3 catalog startup and settings safety", testVst3CatalogStartupAndSettingsSafety },
        { "VST3 dead-man recovery planning", testVst3DeadManRecoveryPlanning },
        { "VST3 plugin position info", testVst3PluginPositionInfo },
        { "VST3 independent runtime and latency", testVst3IndependentRuntimeAndLatency },
        { "VST3 audibility and exact block lengths", testVst3AudibilityAndExactBlockLengths },
        { "VST3 plugin delay reset", testVst3PluginDelayReset },
        { "VST3 Loop segments and playhead", testVst3LoopSegmentsAndPlayHead },
        { "VST3 dry delay discontinuities", testVst3DryDelayDiscontinuities },
        { "VST3 device reprepare failure fallback", testVst3DeviceReprepareFailureFallsBack },
        { "VST3 whole-project routing and fallback", testVst3WholeProjectRoutingAndFallback },
        { "WAV BPM octave candidate selection", testWavBpmOctaveCandidateSelection },
        { "WAV BPM detection and source safety", testWavBpmDetectionAndSourceSafety },
        { "WAV BPM cache reuse and invalidation", testWavBpmCacheReuseAndInvalidation },
        { "WAV BPM cache bounded LRU policy", testWavBpmCacheBoundedLruPolicy },
        { "WAV BPM newest request wins", testWavBpmNewestRequestWins },
        { "Asynchronous WAV BPM analysis and cache reuse", testAsynchronousWavBpmAnalysisAndCacheReuse },
        { "Transport viewport follow and seek visibility", testTransportViewportFollowAndSeekVisibility },
        { "MIDI track exporter basics and round trip", testMidiTrackExporterBasicsAndRoundTrip },
        { "MIDI track exporter tempo/signature/PPQ/ordering", testMidiTrackExporterTempoSignaturePpqAndOrdering },
        { "MIDI track exporter edited state regressions", testMidiTrackExporterEditedStateAndRegressions },
        { "Delete group removes children", testDeleteGroupTrackRemovesChildren },
        { "Stub Hermes not implemented", testStubEngineNotImplemented },
        { "Hermes command enablement", testHermesCommandEnablementAudioVsMidi },
        { "Hermes pair enablement", testHermesCommandEnablementValidAudioMidiPair },
        { "Pair context validation", testValidateAudioMidiPairContext },
        { "Hermes cache clear", testHermesCacheCreateAndClear },
        { "Hermes job runner serialization", testHermesJobRunnerSerializesJobs },
        { "MIDI note event validation", testMidiNoteEventValidation },
        { "Separate layout creates separate tracks", testSeparateLayoutCreatesSeparateMidiTracks },
        { "Grouped layout shared hierarchy", testGroupedLayoutCreatesSharedProjectHierarchy },
        { "Single layout creates one track", testSingleTrackLayoutCreatesOneMidiTrack },
        { "Failed apply leaves no partial result", testFailureLeavesNoPartialProjectResult },
        { "Enabled empty layer can create track", testEnabledEmptyLayerCanCreateTrack },
        { "Zero-note non-meaningful result rejected", testZeroNoteResultWithoutMeaningfulLayerIsRejected },
        { "Undo/redo restores Hermes result", testUndoRedoRestoresHermesResultWithoutReanalysis },
        { "Missing embedded runtime handled safely", testMissingEmbeddedRuntimeHandledSafely },
        { "Composer defaults and disabled probe", testComposerAssistantDefaultsAndDisabledProbe },
        { "Composer port validation", testComposerAssistantPortValidation },
        { "Embedded Hermes structured result insertion", testEmbeddedHermesStructuredResultAndInsertion }
    };

    int failed = 0;
    for (const auto& test : tests) {
        if (skipEmbeddedHermesIntegration
            && test.func == testEmbeddedHermesStructuredResultAndInsertion) {
            std::cout << "[SKIP] " << test.name
                      << " (explicit isolated-environment exclusion)\n";
            continue;
        }

        const bool passed = test.func();
        std::cout << "[" << (passed ? "PASS" : "FAIL") << "] " << test.name << "\n";
        if (!passed) {
            ++failed;
        }
    }

    if (failed != 0) {
        std::cout << "Failed tests: " << failed << "\n";
        return 1;
    }

    std::cout << "All tests passed.\n";
    return 0;
}
