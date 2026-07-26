#include "hermes/EmbeddedHermesEngine.h"

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <pybind11/embed.h>

#include "hermes/HermesValidation.h"

namespace py = pybind11;

namespace dawhermes::hermes {

namespace {

constexpr const char* kPythonBridgeSource = R"PY(
from pathlib import Path

from midi_cleaner.drums.extract_audio import AudioDrumExtractionParameters, extract_drums_from_audio

_CLASS_DURATIONS_SEC = {
    "kick": 0.10,
    "snare_or_clap": 0.10,
    "hat": 0.06,
    "cymbal": 0.32,
    "crash_or_cymbal": 0.32,
    "tom_or_perc": 0.14,
    "unknown": 0.08,
}


def _coerce_track_name(hit):
    candidate = str(getattr(hit, "target_track_name", "") or "")
    if candidate:
        return candidate

    semantic = str(getattr(hit, "semantic_layer", "") or "")
    if semantic:
        return semantic

    return "Drums"


def _ordered_semantic_layers(report):
    ordered = []

    layer_target_notes = dict(getattr(report, "layer_target_notes", {}) or {})
    layer_track_names = dict(getattr(report, "layer_track_names", {}) or {})
    layer_counts = dict(getattr(report, "layer_counts", {}) or {})

    for layer_name in layer_target_notes.keys():
        name = str(layer_name)
        if name not in ordered:
            ordered.append(name)

    for layer_name in layer_track_names.keys():
        name = str(layer_name)
        if name not in ordered:
            ordered.append(name)

    for layer_name in layer_counts.keys():
        name = str(layer_name)
        if name not in ordered:
            ordered.append(name)

    return ordered


def _collect_layer_notes(report, channel):
    grouped = {}
    for hit in report.per_hit_summary:
        if not bool(getattr(hit, "accepted", False)):
            continue
        if bool(getattr(hit, "suppressed", False)):
            continue

        onset = getattr(hit, "accepted_onset_sec", None)
        if onset is None:
            onset = getattr(hit, "onset_sec", None)
        if onset is None:
            continue

        layer_name = str(getattr(hit, "semantic_layer", "") or "")
        if not layer_name:
            layer_name = str(getattr(hit, "layer_name", "") or "unknown")

        class_name = str(getattr(hit, "class_name", "unknown"))
        duration_sec = float(_CLASS_DURATIONS_SEC.get(class_name, 0.10))
        track_name = _coerce_track_name(hit)

        if layer_name not in grouped:
            grouped[layer_name] = {
                "track_name": track_name,
                "notes": [],
            }

        grouped[layer_name]["notes"].append(
            {
                "pitch": int(getattr(hit, "target_note", 36)),
                "velocity": int(getattr(hit, "velocity", 100)),
                "start_sec": float(onset),
                "duration_sec": duration_sec,
                "channel": int(channel),
            }
        )

    return grouped


def dawhermes_extract_drums(wav_path: str, options: dict):
    try:
        output_layout = str(options.get("output_layout", "separate-files"))
        write_empty_layers = bool(options.get("write_empty_layers", False))
        channel = int(options.get("channel", 10))

        params = AudioDrumExtractionParameters(
            output_file=None,
            target_map=str(options["target_map"]),
            output_layout=output_layout,
            profile=str(options["profile"]),
            detection_mode=str(options["detection_mode"]),
            c1_midi_note=int(options["c1_midi_note"]),
            write_empty_layers=write_empty_layers,
            dry_run=True,
        )

        report = extract_drums_from_audio(wav_file=Path(wav_path), params=params)

        single_track = output_layout == "single-track"
        aggregate_name = str(options.get("single_track_name", "Drums"))

        layer_track_names = {
            str(name): str(track)
            for name, track in dict(getattr(report, "layer_track_names", {}) or {}).items()
        }
        disabled_layers = {
            str(item) for item in (getattr(report, "disabled_layers", []) or [])
        }

        grouped = _collect_layer_notes(report, channel)
        ordered_layers = _ordered_semantic_layers(report)
        for layer_name in grouped.keys():
            if layer_name not in ordered_layers:
                ordered_layers.append(layer_name)

        tracks = []
        if single_track:
            all_notes = []
            for layer_name in ordered_layers:
                entry = grouped.get(layer_name)
                if entry:
                    all_notes.extend(entry["notes"])

            all_notes.sort(key=lambda item: (item["start_sec"], item["pitch"], item["velocity"]))
            if all_notes:
                tracks.append(
                    {
                        "name": aggregate_name,
                        "semantic_layer": "drums",
                        "enabled": True,
                        "empty": False,
                        "notes": all_notes,
                    }
                )
        else:
            for layer_name in ordered_layers:
                entry = grouped.get(layer_name)
                track_name = layer_track_names.get(layer_name, layer_name)
                notes = []
                if entry:
                    track_name = str(entry.get("track_name") or track_name)
                    notes = list(entry.get("notes") or [])

                notes.sort(key=lambda item: (item["start_sec"], item["pitch"], item["velocity"]))

                enabled = layer_name not in disabled_layers
                include = bool(notes)
                if not include and write_empty_layers and enabled:
                    include = True

                if not include:
                    continue

                tracks.append(
                    {
                        "name": track_name,
                        "semantic_layer": layer_name,
                        "enabled": enabled,
                        "empty": len(notes) == 0,
                        "notes": notes,
                    }
                )

        accepted_onsets = int(getattr(report, "accepted_onset_count", 0))
        return {
            "status": "success",
            "message": f"Extracted {accepted_onsets} drum hits.",
            "accepted_onset_count": accepted_onsets,
            "bpm_used": float(getattr(report, "bpm_used", 120.0)),
            "warnings": [str(item) for item in (getattr(report, "warnings", []) or [])],
            "tracks": tracks,
        }
    except Exception as exc:
        return {
            "status": "error",
            "message": str(exc),
            "accepted_onset_count": 0,
            "bpm_used": 120.0,
            "warnings": [],
            "tracks": [],
        }
)PY";

std::string getEnvironmentValue(const char* name)
{
#ifdef _WIN32
    char* value = nullptr;
    std::size_t length = 0;
    if (_dupenv_s(&value, &length, name) != 0 || value == nullptr) {
        return {};
    }

    std::string output(value);
    free(value);
    return output;
#else
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return {};
    }

    return std::string(value);
#endif
}

bool isExistingDir(const std::filesystem::path& path)
{
    std::error_code ec;
    return std::filesystem::exists(path, ec) && std::filesystem::is_directory(path, ec);
}

std::filesystem::path normalizedAbsolute(const std::filesystem::path& path)
{
    std::error_code ec;
    const auto absolute = std::filesystem::absolute(path, ec);
    if (ec) {
        return path;
    }

    return absolute.lexically_normal();
}

std::string pathToUtf8(const std::filesystem::path& path)
{
    return path.string();
}

const char* resultLayoutToken(HermesResultLayout layout)
{
    switch (layout) {
    case HermesResultLayout::separateMidiTracks:
        return "separate-files";
    case HermesResultLayout::groupedMultitrack:
        return "multitrack";
    case HermesResultLayout::singleDrumTrack:
        return "single-track";
    default:
        return "separate-files";
    }
}

const char* profileToken(HermesDrumsProfile profile)
{
    switch (profile) {
    case HermesDrumsProfile::conservative:
        return "conservative";
    case HermesDrumsProfile::balanced:
        return "balanced";
    case HermesDrumsProfile::sensitive:
        return "sensitive";
    default:
        return "balanced";
    }
}

const char* detectionToken(HermesDetectionMode mode)
{
    switch (mode) {
    case HermesDetectionMode::multiDetector:
        return "multi-detector";
    case HermesDetectionMode::global:
        return "global";
    default:
        return "multi-detector";
    }
}

std::string targetMapToken(HermesTargetMapping mapping)
{
    switch (mapping) {
    case HermesTargetMapping::ujamKandy:
        return "ujam-candy";
    case HermesTargetMapping::generalMidi:
        return "gm";
    case HermesTargetMapping::sitala:
        return "sitala";
    case HermesTargetMapping::custom:
    default:
        return {};
    }
}

std::string buildGroupsMessage(std::size_t trackCount, std::size_t noteCount)
{
    std::ostringstream stream;
    stream << "Embedded Hermes generated " << noteCount << " notes across " << trackCount
           << " MIDI tracks.";
    return stream.str();
}

}  // namespace

struct EmbeddedHermesEngine::PythonState {
    std::unique_ptr<py::scoped_interpreter> interpreter;
    py::object extractFunction;
};

EmbeddedHermesEngine::EmbeddedHermesEngine() = default;

EmbeddedHermesEngine::~EmbeddedHermesEngine() = default;

HermesOperationResult EmbeddedHermesEngine::drumsMakeMidiFromWav(
    const HermesTrackContext& context,
    const HermesDrumsOptions& options)
{
    const auto contextValidation = validateTrackContextForDrums(context);
    if (!contextValidation.ok) {
        return HermesOperationResult::invalidInput(contextValidation.message);
    }

    const auto optionsValidation = validateDrumsOptions(options);
    if (!optionsValidation.ok) {
        return HermesOperationResult::invalidInput(optionsValidation.message);
    }

    const auto mapToken = targetMapToken(options.targetMapping);
    if (mapToken.empty()) {
        return HermesOperationResult::invalidInput(
            "Custom drum mapping preset is not yet wired for embedded extraction.");
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureRuntimeReady()) {
        return unavailableResult("Drums -> Make MIDI from WAV");
    }

    try {
        py::gil_scoped_acquire acquire;

        py::dict optionsDict;
        optionsDict["output_layout"] = resultLayoutToken(options.resultLayout);
        optionsDict["profile"] = profileToken(options.profile);
        optionsDict["detection_mode"] = detectionToken(options.detectionMode);
        optionsDict["target_map"] = mapToken;
        optionsDict["c1_midi_note"] = options.c1MidiNote;
        optionsDict["write_empty_layers"] = options.createEmptyEnabledLayers;
        optionsDict["channel"] = 10;
        optionsDict["single_track_name"] = "Drums";

        const auto payload = pythonState_->extractFunction(context.audioSourcePath, optionsDict).cast<py::dict>();
        const auto status = payload["status"].cast<std::string>();
        if (status != "success") {
            return HermesOperationResult::unavailable(payload["message"].cast<std::string>());
        }

        const auto bpmUsed = payload["bpm_used"].cast<double>();
        const auto beatsPerSecond = std::max(0.001, bpmUsed / 60.0);

        std::vector<HermesGeneratedMidiTrack> generated;
        std::size_t totalNotes = 0;
        const auto tracks = payload["tracks"].cast<py::list>();
        for (const auto& trackObject : tracks) {
            const auto trackDict = trackObject.cast<py::dict>();
            HermesGeneratedMidiTrack generatedTrack;
            generatedTrack.trackName = trackDict["name"].cast<std::string>();
            generatedTrack.semanticLayer = generatedTrack.trackName;
            generatedTrack.enabledLayer = true;
            generatedTrack.emptyLayer = false;

            if (trackDict.contains(py::str("semantic_layer"))) {
                generatedTrack.semanticLayer = trackDict["semantic_layer"].cast<std::string>();
            }

            if (trackDict.contains(py::str("enabled"))) {
                generatedTrack.enabledLayer = trackDict["enabled"].cast<bool>();
            }

            if (trackDict.contains(py::str("empty"))) {
                generatedTrack.emptyLayer = trackDict["empty"].cast<bool>();
            }

            const auto notes = trackDict["notes"].cast<py::list>();
            generatedTrack.notes.reserve(notes.size());
            for (const auto& noteObject : notes) {
                const auto noteDict = noteObject.cast<py::dict>();

                const auto startSec = noteDict["start_sec"].cast<double>();
                const auto durationSec = noteDict["duration_sec"].cast<double>();
                core::MidiNote midiNote;
                midiNote.pitch = std::clamp(noteDict["pitch"].cast<int>(), 0, 127);
                midiNote.velocity = std::clamp(noteDict["velocity"].cast<int>(), 1, 127);
                midiNote.startBeat = std::max(0.0, startSec * beatsPerSecond);
                midiNote.durationBeats = std::max(0.01, durationSec * beatsPerSecond);
                midiNote.channel = std::clamp(noteDict["channel"].cast<int>(), 1, 16);

                generatedTrack.notes.push_back(midiNote);
            }

            if (generatedTrack.notes.empty()) {
                generatedTrack.emptyLayer = true;
            }

            totalNotes += generatedTrack.notes.size();
            generated.push_back(std::move(generatedTrack));
        }

        std::vector<std::string> warnings;
        const auto pyWarnings = payload["warnings"].cast<py::list>();
        warnings.reserve(pyWarnings.size());
        for (const auto& warning : pyWarnings) {
            warnings.push_back(warning.cast<std::string>());
        }

        return HermesOperationResult::success(
            buildGroupsMessage(generated.size(), totalNotes),
            options.resultLayout,
            std::move(generated),
            bpmUsed,
            std::move(warnings));
    } catch (const std::exception& ex) {
        return HermesOperationResult::unavailable(
            std::string("Embedded Hermes execution failed: ") + ex.what());
    }
}

HermesOperationResult EmbeddedHermesEngine::bassMakeOrRepairMidiFromWav(
    const HermesTrackContext&,
    const HermesBassOptions&)
{
    return HermesOperationResult::notImplemented(
        "Bass workflow is not part of Milestone 1 scope in DAWHermes.");
}

HermesOperationResult EmbeddedHermesEngine::synchronizeMidiWithWav(
    const HermesTrackContext&,
    const HermesSyncOptions&)
{
    return HermesOperationResult::notImplemented(
        "MIDI/WAV synchronization is scheduled for a later milestone.");
}

HermesOperationResult EmbeddedHermesEngine::setOrFixBpm(
    const HermesTrackContext&,
    const HermesBpmOptions&)
{
    return HermesOperationResult::notImplemented(
        "Set/Fix BPM is scheduled for a later milestone.");
}

bool EmbeddedHermesEngine::ensureRuntimeReady()
{
    if (runtimeReady_ && pythonState_ != nullptr) {
        return true;
    }

    const auto midiCleanerRoot = resolveMidiCleanerRoot();
    if (midiCleanerRoot.empty()) {
        unavailableReason_ =
            "midi-cleaner repository was not found. Set DAWHERMES_HERMES_REPO to a valid clone path.";
        runtimeReady_ = false;
        pythonState_.reset();
        return false;
    }

    try {
        pythonState_ = std::make_unique<PythonState>();
        pythonState_->interpreter = std::make_unique<py::scoped_interpreter>();

        py::gil_scoped_acquire acquire;
        py::module_ sys = py::module_::import("sys");
        py::list sysPath = sys.attr("path");

        const auto rootPath = normalizedAbsolute(std::filesystem::path(midiCleanerRoot));
        const auto sourcePath = rootPath / "src";
        const auto windowsSitePackages = rootPath / ".venv" / "Lib" / "site-packages";
        const auto posixSitePackages = rootPath / ".venv" / "lib" / "site-packages";

        if (isExistingDir(sourcePath)) {
            sysPath.append(pathToUtf8(sourcePath));
        }

        if (isExistingDir(windowsSitePackages)) {
            sysPath.append(pathToUtf8(windowsSitePackages));
        }

        if (isExistingDir(posixSitePackages)) {
            sysPath.append(pathToUtf8(posixSitePackages));
        }

        py::dict globals;
        py::exec(kPythonBridgeSource, globals);
        pythonState_->extractFunction = globals["dawhermes_extract_drums"];

        runtimeReady_ = true;
        unavailableReason_.clear();
        return true;
    } catch (const std::exception& ex) {
        runtimeReady_ = false;
        unavailableReason_ = std::string("Failed to initialize embedded Python runtime: ") + ex.what();
        pythonState_.reset();
        return false;
    }
}

std::string EmbeddedHermesEngine::resolveMidiCleanerRoot() const
{
    const auto fromEnv = getEnvironmentValue("DAWHERMES_HERMES_REPO");
    if (!fromEnv.empty()) {
        const auto path = normalizedAbsolute(std::filesystem::path(fromEnv));
        if (isExistingDir(path / "src" / "midi_cleaner")) {
            return pathToUtf8(path);
        }
    }

    const auto cwdCandidate = normalizedAbsolute(std::filesystem::path("..") / "midi-cleaner");
    if (isExistingDir(cwdCandidate / "src" / "midi_cleaner")) {
        return pathToUtf8(cwdCandidate);
    }

    const auto userProfile = getEnvironmentValue("USERPROFILE");
    if (!userProfile.empty()) {
        const auto userCandidate = normalizedAbsolute(
            std::filesystem::path(userProfile) / "source" / "repos" / "midi-cleaner");
        if (isExistingDir(userCandidate / "src" / "midi_cleaner")) {
            return pathToUtf8(userCandidate);
        }
    }

    return {};
}

HermesOperationResult EmbeddedHermesEngine::unavailableResult(const std::string& commandName) const
{
    std::string message = unavailableReason_;
    if (message.empty()) {
        message = "Embedded Hermes runtime is unavailable.";
    }

    return HermesOperationResult::unavailable(commandName + " unavailable: " + message);
}

}  // namespace dawhermes::hermes
