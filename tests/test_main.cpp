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
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "core/MainLayoutGeometry.h"
#include "core/MidiComparisonModel.h"
#include "core/MidiNoteEditing.h"
#include "core/MidiNoteSelectionState.h"
#include "core/MidiTimeMap.h"
#include "core/ProjectController.h"
#include "core/ProjectHistory.h"
#include "core/ProjectModel.h"
#include "core/SelectionState.h"
#include "core/TimelineGeometry.h"
#include "core/TimelineViewport.h"
#include "core/Track.h"
#include "audio/MidiAuditionEngine.h"
#include "audio/MidiPlaybackModel.h"
#include "audio/SelectionPlaybackModel.h"
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
#include "ui/MidiImportParser.h"

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
        false).playEnabled);
    selection.selectTrack(emptyMidiId);
    selection.toggleTrack(groupId);
    EXPECT_TRUE(!dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        false).playEnabled);
    selection.selectTrack(midiId);
    auto ready = dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        false);
    EXPECT_TRUE(ready.playEnabled);
    EXPECT_TRUE(!ready.stopEnabled);
    selection.selectTrack(audioId);
    ready = dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        false);
    EXPECT_TRUE(ready.playEnabled);
    const auto playing = dawhermes::audio::selectionTransportCommandState(
        project,
        selection,
        true);
    EXPECT_TRUE(!playing.playEnabled);
    EXPECT_TRUE(playing.stopEnabled);
    EXPECT_TRUE(playing.panicEnabled);

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

    dawhermes::audio::SelectionPlaybackState state;
    state.start(std::make_shared<const dawhermes::audio::SelectionPlaybackSnapshot>(
        snapshot));
    EXPECT_TRUE(state.isPlaying());
    EXPECT_TRUE(state.hasPreparedPlayback());
    EXPECT_TRUE(state.snapshot()->midi.has_value());
    EXPECT_EQ(state.snapshot()->audioStems.size(), static_cast<std::size_t>(1));
    state.stop();
    EXPECT_TRUE(!state.isPlaying());
    EXPECT_TRUE(!state.hasPreparedPlayback());
    EXPECT_TRUE(state.snapshot() == nullptr);

    state.start(std::make_shared<const dawhermes::audio::SelectionPlaybackSnapshot>(
        std::move(snapshot)));
    EXPECT_TRUE(state.isPlaying());
    state.panic();
    EXPECT_TRUE(!state.isPlaying());
    EXPECT_TRUE(!state.hasPreparedPlayback());
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
    EXPECT_EQ(document->noteBearingTracks.size(), static_cast<std::size_t>(1));
    const auto& candidate = document->noteBearingTracks.front();
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

    EXPECT_TRUE(controller.assignAudioSourceToTrack(audio.id, wavFixture.string()));

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

    EXPECT_TRUE(controller.assignAudioSourceToTrack(audioId, wavFixture.string()));
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

}  // namespace

int main(int argc, char* argv[])
{
    if (argc > 1 && std::string(argv[1]) == "--m2-real-assets") {
        return runM2RealAssetsMode(argc, argv);
    }

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
        { "Selection playback transport stop/panic state", testSelectionPlaybackTransportAndStopPanicState },
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
