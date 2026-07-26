#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <vector>

#include "core/MainLayoutGeometry.h"
#include "core/ProjectController.h"
#include "core/ProjectModel.h"
#include "core/SelectionState.h"
#include "core/Track.h"
#include "hermes/HermesCommandAvailability.h"
#include "hermes/ComposerAssistantConnector.h"
#include "hermes/EmbeddedHermesEngine.h"
#include "hermes/HermesProjectResult.h"
#include "hermes/HermesTypes.h"
#include "hermes/HermesValidation.h"
#include "hermes/StubHermesEngine.h"

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
using dawhermes::hermes::HermesGeneratedMidiTrack;
using dawhermes::hermes::HermesOperationResult;
using dawhermes::hermes::HermesOperationStatus;
using dawhermes::hermes::StubHermesEngine;

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

MidiNote makeMidiNote(int pitch, int velocity, double startBeat, double durationBeats, int channel = 10)
{
    return MidiNote { pitch, velocity, startBeat, durationBeats, channel };
}

HermesGeneratedMidiTrack makeGeneratedTrack(
    std::string name,
    std::vector<MidiNote> notes,
    std::string semanticLayer = {},
    bool enabledLayer = true,
    bool emptyLayer = false)
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

    const auto& audio = controller.addTrack(TrackType::audio);
    controller.selectTrack(audio.id);

    EXPECT_EQ(
        dawhermes::hermes::getHermesCommandAvailability(
            HermesCommand::drumsMakeMidiFromWav,
            project,
            selection),
        HermesCommandAvailability::requiresAudioFile);

    EXPECT_TRUE(controller.assignAudioSourceToTrack(audio.id, "C:/tmp/drums.wav"));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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
    EXPECT_TRUE(controller.assignAudioSourceToTrack(source.id, wavFixture.string()));

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

}  // namespace

int main()
{
    struct NamedTest {
        const char* name;
        bool (*func)();
    };

    const std::vector<NamedTest> tests {
        { "Track creation", testTrackCreationAudioAndMidi },
        { "Audio source assignment", testAudioSourceAssignment },
        { "MIDI note replacement", testMidiNoteReplacement },
        { "Stable track IDs", testStableTrackIds },
        { "Selection state", testSelectionState },
        { "Delete selected track", testDeleteSelectedTrack },
        { "Delete unselected track", testDeleteUnselectedTrack },
        { "Empty project safety", testEmptyProjectSafety },
        { "C1 range validation", testC1RangeValidation },
        { "BPM validation", testBpmValidation },
        { "Drums enum validation", testDrumsEnumsValidation },
        { "Drums track context validation", testDrumsTrackContextValidation },
        { "Main layout geometry", testMainLayoutGeometry },
        { "Panel layout state clamp/roundtrip", testPanelLayoutStateRoundTripAndClamping },
        { "Delete group removes children", testDeleteGroupTrackRemovesChildren },
        { "Stub Hermes not implemented", testStubEngineNotImplemented },
        { "Hermes command enablement", testHermesCommandEnablementAudioVsMidi },
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
