#include <cmath>
#include <cstdint>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "core/ProjectController.h"
#include "core/ProjectModel.h"
#include "core/SelectionState.h"
#include "core/Track.h"
#include "hermes/HermesCommandAvailability.h"
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
using dawhermes::hermes::HermesOperationStatus;
using dawhermes::hermes::StubHermesEngine;

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

bool testStubEngineNotImplemented()
{
    StubHermesEngine engine;
    const auto result = engine.drumsMakeMidiFromWav(
        dawhermes::hermes::HermesTrackContext { 1, "Audio Track 1", TrackType::audio },
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

}  // namespace

int main()
{
    struct NamedTest {
        const char* name;
        bool (*func)();
    };

    const std::vector<NamedTest> tests {
        { "Track creation", testTrackCreationAudioAndMidi },
        { "Stable track IDs", testStableTrackIds },
        { "Selection state", testSelectionState },
        { "Delete selected track", testDeleteSelectedTrack },
        { "Delete unselected track", testDeleteUnselectedTrack },
        { "Empty project safety", testEmptyProjectSafety },
        { "C1 range validation", testC1RangeValidation },
        { "BPM validation", testBpmValidation },
        { "Drums enum validation", testDrumsEnumsValidation },
        { "Stub Hermes not implemented", testStubEngineNotImplemented },
        { "Hermes command enablement", testHermesCommandEnablementAudioVsMidi }
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
