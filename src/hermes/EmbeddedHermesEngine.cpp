#include "hermes/EmbeddedHermesEngine.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include <pybind11/embed.h>

#include "hermes/HermesCache.h"
#include "hermes/HermesValidation.h"

namespace py = pybind11;

namespace dawhermes::hermes {

namespace {

constexpr const char* kPythonBridgeSourcePart1 = R"PY(
from pathlib import Path
import json

import mido

from midi_cleaner.drums.extract_audio import AudioDrumExtractionParameters, extract_drums_from_audio
from midi_cleaner.midi.importer import import_midi_candidate

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


def _clamp(value, lo, hi):
    return max(lo, min(hi, value))


def _to_int(value, default):
    try:
        return int(value)
    except Exception:
        return int(default)


def _to_float_or_none(value):
    if value is None:
        return None
    try:
        return float(value)
    except Exception:
        return None


def _tempo_payload(document):
    payload = []
    for event in list(getattr(document, "tempo_map", []) or []):
        payload.append(
            {
                "tick": int(getattr(event, "tick", 0)),
                "tempo_us_per_beat": int(getattr(event, "tempo_us_per_beat", 500000)),
                "sec": float(getattr(event, "sec", 0.0)),
            }
        )

    if not payload:
        payload.append({"tick": 0, "tempo_us_per_beat": 500000, "sec": 0.0})

    payload.sort(key=lambda item: (item["tick"], item["tempo_us_per_beat"]))
    return payload


def _notes_payload(document):
    notes = []
    for note in list(getattr(document, "notes", []) or []):
        notes.append(
            {
                "pitch": int(getattr(note, "pitch_midi", 60)),
                "velocity": int(getattr(note, "velocity", 100)),
                "channel": int(getattr(note, "channel", 0) or 0),
                "start_tick": int(getattr(note, "start_tick", 0)),
                "end_tick": int(getattr(note, "end_tick", 1)),
                "duration_ticks": int(getattr(note, "duration_ticks", 1)),
            }
        )

    return notes


def _load_json(path: Path):
    return json.loads(path.read_text(encoding="utf-8"))


def _resolve_working_midi_path(project_dir: Path, output_files: dict):
    candidate = output_files.get("working_best_midi")
    if candidate:
        return Path(candidate)

    report_path = output_files.get("working_export_report")
    if report_path:
        report_data = _load_json(Path(report_path))
        for item in list(report_data.get("exported_files", []) or []):
            if str(item.get("role", "")).upper() == "WORKING":
                path = item.get("path")
                if path:
                    return Path(path)

    fallback = project_dir / "midi" / "working" / "working.mid"
    if fallback.exists():
        return fallback

    raise RuntimeError("Working MIDI output file was not found in process_stem output.")


def _build_tempo_abs_events(tempo_map):
    events = []
    normalized = []
    for event in list(tempo_map or []):
        tick = _to_int(event.get("tick", 0), 0)
        tempo = _to_int(event.get("tempo_us_per_beat", 500000), 500000)
        tempo = max(1, tempo)
        normalized.append((max(0, tick), tempo))

    if not normalized:
        normalized = [(0, 500000)]

    normalized.sort(key=lambda item: item[0])
    for tick, tempo in normalized:
        events.append((tick, 0, mido.MetaMessage("set_tempo", tempo=int(tempo), time=0)))

    return events


def _build_note_abs_events(notes):
    events = []
    for note in list(notes or []):
        pitch = _clamp(_to_int(note.get("pitch", 60), 60), 0, 127)
        velocity = _clamp(_to_int(note.get("velocity", 100), 100), 1, 127)
        start_tick = max(0, _to_int(note.get("start_tick", 0), 0))
        end_tick = max(start_tick + 1, _to_int(note.get("end_tick", start_tick + 1), start_tick + 1))
        channel_1_based = _clamp(_to_int(note.get("channel", 1), 1), 1, 16)
        channel = channel_1_based - 1

        events.append(
            (
                start_tick,
                2,
                mido.Message("note_on", note=pitch, velocity=velocity, channel=channel, time=0),
            )
        )
        events.append(
            (
                end_tick,
                1,
                mido.Message("note_off", note=pitch, velocity=0, channel=channel, time=0),
            )
        )

    return events


def dawhermes_write_midi_adapter(
    midi_path: str,
    notes: list,
    ticks_per_beat: int,
    tempo_map: list,
    track_name: str,
):
    output_path = Path(midi_path)
    output_path.parent.mkdir(parents=True, exist_ok=True)

    ticks = max(1, int(ticks_per_beat))
    midi = mido.MidiFile(type=1, ticks_per_beat=ticks)
    track = mido.MidiTrack()
    midi.tracks.append(track)
    track.append(mido.MetaMessage("track_name", name=str(track_name or "DAWHermes Source"), time=0))

    events = []
    events.extend(_build_tempo_abs_events(tempo_map))
    events.extend(_build_note_abs_events(notes))
    events.sort(key=lambda item: (item[0], item[1]))

    previous_tick = 0
    for tick, _order, message in events:
        message.time = max(0, int(tick) - previous_tick)
        previous_tick = int(tick)
        track.append(message)

    midi.save(str(output_path))
    return {
        "status": "success",
        "midi_path": str(output_path),
        "note_count": len(list(notes or [])),
        "ticks_per_beat": ticks,
    }


def _load_report(path_string: str):
    path = Path(path_string)
    if not path.exists() or not path.is_file():
        return {}
    return _load_json(path)


def _best_iteration(iterative_report: dict):
    best_index = int(iterative_report.get("best_iteration_index", 0) or 0)
    if best_index <= 0:
        return {}

    iterations = list(iterative_report.get("iterations", []) or [])
    for item in iterations:
        if int(item.get("iteration_index", 0) or 0) == best_index:
            return item

    return {}


def dawhermes_run_bass_pipeline(
    input_midi_path: str,
    input_wav_path: str,
    project_dir: str,
    options: dict,
):
    try:
        from midi_cleaner.pipeline.process_stem import PipelineProcessParameters, process_stem_pipeline

        source = str(options.get("source", "dawhermes") or "dawhermes")
        layer = str(options.get("layer", "bass") or "bass")

        params = PipelineProcessParameters(
            track_name_prefix=str(options.get("track_name_prefix", "Hermes") or "Hermes"),
            enable_ai_pattern_completion=False,
        )

        pipeline_report = process_stem_pipeline(
            input_midi=Path(input_midi_path),
            input_wav=Path(input_wav_path),
            source=source,
            layer=layer,
            project_dir=Path(project_dir),
            params=params,
        )

        output_files = dict(getattr(pipeline_report, "output_files", {}) or {})
        warnings = list(getattr(pipeline_report, "warnings", []) or [])

        working_midi_path = _resolve_working_midi_path(Path(project_dir), output_files)
        note_document, import_report = import_midi_candidate(
            working_midi_path,
            source=source,
            layer=layer,
        )
        warnings.extend(list(getattr(import_report, "warnings", []) or []))

        input_note_count = 0
        midi_import_report_path = output_files.get("midi_import_report")
        if midi_import_report_path:
            midi_import_report = _load_report(midi_import_report_path)
            input_note_count = int(midi_import_report.get("note_count", 0) or 0)

        merged_count = 0
        inserted_count = 0
        split_count = 0
        iterative_report_path = output_files.get("iterative_repair_report")
        if iterative_report_path:
            iterative_report = _load_report(iterative_report_path)
            best = _best_iteration(iterative_report)
            merged_count = int(best.get("merge_count", 0) or 0)
            inserted_count = int(best.get("insert_count", 0) or 0)
            split_count = int(best.get("split_count", 0) or 0)
            warnings.extend(list(iterative_report.get("warnings", []) or []))

        removed_or_muted_count = 0
        working_export_report_path = output_files.get("working_export_report")
        if working_export_report_path:
            working_export_report = _load_report(working_export_report_path)
            removed_or_muted_count = int(working_export_report.get("rejected_note_count", 0) or 0)
            warnings.extend(list(working_export_report.get("warnings", []) or []))

        notes = _notes_payload(note_document)
        message = f"Bass repair produced {len(notes)} note(s)."

        return {
            "status": "success",
            "message": message,
            "ticks_per_beat": int(getattr(note_document, "ticks_per_beat", 960) or 960),
            "tempo_map": _tempo_payload(note_document),
            "notes": notes,
            "warnings": warnings,
            "working_midi_file": str(working_midi_path),
            "pipeline_report_file": str(output_files.get("pipeline_report", "")),
            "statistics": {
                "input_note_count": input_note_count,
                "output_note_count": len(notes),
                "merged_count": merged_count,
                "inserted_count": inserted_count,
                "split_count": split_count,
                "removed_or_muted_count": removed_or_muted_count,
            },
        }
    except Exception as exc:
        return {
            "status": "error",
            "message": str(exc),
            "ticks_per_beat": 960,
            "tempo_map": [{"tick": 0, "tempo_us_per_beat": 500000, "sec": 0.0}],
            "notes": [],
            "warnings": [],
            "statistics": {
                "input_note_count": 0,
                "output_note_count": 0,
                "merged_count": 0,
                "inserted_count": 0,
                "split_count": 0,
                "removed_or_muted_count": 0,
            },
        }


def dawhermes_run_midi_sync(
    input_midi_path: str,
    input_wav_path: str,
    output_midi_path: str,
    options: dict,
):
    try:
        from midi_cleaner.midi.sync_with_audio import MidiSyncWithAudioParameters, sync_midi_with_wav

        source = str(options.get("source", "dawhermes") or "dawhermes")
        layer = str(options.get("layer", "bass") or "bass")
        bpm_override = _to_float_or_none(options.get("bpm_override"))

        params = MidiSyncWithAudioParameters(
            source=source,
            layer=layer,
            bpm_override=bpm_override,
        )

        report, _aligned_document, _alignment_report = sync_midi_with_wav(
            input_midi=Path(input_midi_path),
            input_wav=Path(input_wav_path),
            output_midi=Path(output_midi_path),
            params=params,
        )

        note_document, import_report = import_midi_candidate(
            Path(output_midi_path),
            source=source,
            layer=layer,
        )

        warnings = list(getattr(report, "warnings", []) or [])
        warnings.extend(list(getattr(import_report, "warnings", []) or []))

        notes = _notes_payload(note_document)
        message = f"Synchronization produced {len(notes)} note(s)."

        return {
            "status": "success",
            "message": message,
            "ticks_per_beat": int(getattr(note_document, "ticks_per_beat", 960) or 960),
            "tempo_map": _tempo_payload(note_document),
            "notes": notes,
            "warnings": warnings,
            "output_midi_file": str(output_midi_path),
            "statistics": {
                "input_note_count": int(getattr(report, "note_count", len(notes)) or len(notes)),
                "output_note_count": len(notes),
                "aligned_count": int(getattr(report, "aligned_count", 0) or 0),
                "keep_original_count": int(getattr(report, "keep_original_count", 0) or 0),
                "review_timing_count": int(getattr(report, "review_timing_count", 0) or 0),
                "no_audio_evidence_count": int(getattr(report, "no_audio_evidence_count", 0) or 0),
            },
        }
    except Exception as exc:
        return {
            "status": "error",
            "message": str(exc),
            "ticks_per_beat": 960,
            "tempo_map": [{"tick": 0, "tempo_us_per_beat": 500000, "sec": 0.0}],
            "notes": [],
            "warnings": [],
            "statistics": {
                "input_note_count": 0,
                "output_note_count": 0,
                "aligned_count": 0,
                "keep_original_count": 0,
                "review_timing_count": 0,
                "no_audio_evidence_count": 0,
            },
        }
)PY";

constexpr const char* kPythonBridgeSourcePart2 = R"PY(
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

std::string syncRoleToken(HermesSyncRole role)
{
    switch (role) {
    case HermesSyncRole::bass:
        return "bass";
    case HermesSyncRole::drums:
        return "drums";
    case HermesSyncRole::synth:
        return "synth";
    case HermesSyncRole::guitar:
        return "guitar";
    case HermesSyncRole::other:
    default:
        return "other";
    }
}

std::string buildGroupsMessage(std::size_t trackCount, std::size_t noteCount)
{
    std::ostringstream stream;
    stream << "Embedded Hermes generated " << noteCount << " notes across " << trackCount
           << " MIDI tracks.";
    return stream.str();
}

int beatToTick(double beatPosition, int ticksPerQuarterNote)
{
    const auto clampedBeat = std::max(0.0, beatPosition);
    return static_cast<int>(std::llround(clampedBeat * static_cast<double>(std::max(1, ticksPerQuarterNote))));
}

int normalizeChannelFromImporter(int importerChannel)
{
    int channel = importerChannel;
    if (channel >= 0 && channel <= 15) {
        channel += 1;
    }

    return std::clamp(channel, 1, 16);
}

double tempoUsToBpm(int microsecondsPerQuarterNote)
{
    if (microsecondsPerQuarterNote <= 0) {
        return 0.0;
    }

    return 60000000.0 / static_cast<double>(microsecondsPerQuarterNote);
}

std::vector<core::MidiTempoEvent> parseTempoMapPayload(const py::list& tempoPayload, int ticksPerQuarterNote)
{
    std::vector<core::MidiTempoEvent> tempoMap;
    tempoMap.reserve(tempoPayload.size());

    const auto safeTicksPerQuarter = std::max(1, ticksPerQuarterNote);
    for (const auto& eventObject : tempoPayload) {
        const auto eventDict = eventObject.cast<py::dict>();
        const auto tick = std::max(0, eventDict["tick"].cast<int>());
        const auto tempoUs = std::max(1, eventDict["tempo_us_per_beat"].cast<int>());

        core::MidiTempoEvent event;
        event.beatPosition = static_cast<double>(tick) / static_cast<double>(safeTicksPerQuarter);
        event.microsecondsPerQuarterNote = tempoUs;
        tempoMap.push_back(event);
    }

    if (tempoMap.empty()) {
        tempoMap.push_back(core::MidiTempoEvent {});
    }

    return tempoMap;
}

std::vector<core::MidiNote> parseNotesPayload(const py::list& notesPayload, int ticksPerQuarterNote)
{
    std::vector<core::MidiNote> notes;
    notes.reserve(notesPayload.size());

    const auto safeTicksPerQuarter = std::max(1, ticksPerQuarterNote);
    const auto minDurationBeats = 1.0 / static_cast<double>(safeTicksPerQuarter);

    for (const auto& noteObject : notesPayload) {
        const auto noteDict = noteObject.cast<py::dict>();
        const auto startTick = std::max(0, noteDict["start_tick"].cast<int>());
        const auto endTick = std::max(startTick + 1, noteDict["end_tick"].cast<int>());
        const auto durationTicks = std::max(1, endTick - startTick);

        core::MidiNote note;
        note.pitch = std::clamp(noteDict["pitch"].cast<int>(), 0, 127);
        note.velocity = std::clamp(noteDict["velocity"].cast<int>(), 1, 127);
        note.channel = normalizeChannelFromImporter(noteDict["channel"].cast<int>());
        note.startBeat = static_cast<double>(startTick) / static_cast<double>(safeTicksPerQuarter);
        note.durationBeats = std::max(
            minDurationBeats,
            static_cast<double>(durationTicks) / static_cast<double>(safeTicksPerQuarter));

        notes.push_back(note);
    }

    return notes;
}

std::vector<std::string> parseWarningsPayload(const py::dict& payload)
{
    if (!payload.contains(py::str("warnings"))) {
        return {};
    }

    std::vector<std::string> warnings;
    const auto warningsList = payload["warnings"].cast<py::list>();
    warnings.reserve(warningsList.size());
    for (const auto& warning : warningsList) {
        warnings.push_back(warning.cast<std::string>());
    }

    return warnings;
}

std::size_t parseUnsignedStatistic(const py::dict& statsPayload, const char* key)
{
    if (!statsPayload.contains(py::str(key))) {
        return 0;
    }

    const auto value = statsPayload[key].cast<long long>();
    return value < 0 ? 0 : static_cast<std::size_t>(value);
}

core::MidiSourceMetadata buildGeneratedMetadata(
    const core::MidiSourceMetadata& sourceMetadata,
    const std::string& generatedTrackName,
    int ticksPerQuarterNote,
    const std::vector<core::MidiTempoEvent>& tempoMap,
    const std::vector<core::MidiNote>& generatedNotes)
{
    core::MidiSourceMetadata metadata = sourceMetadata;
    metadata.sourceTrackName = generatedTrackName;
    metadata.ticksPerQuarterNote = std::max(1, ticksPerQuarterNote);
    metadata.tempoMap = tempoMap;
    metadata.noteCount = generatedNotes.size();
    metadata.channelsUsed.clear();
    metadata.approximateDurationBeats = 0.0;
    for (const auto& note : generatedNotes) {
        if (std::find(metadata.channelsUsed.begin(), metadata.channelsUsed.end(), note.channel)
            == metadata.channelsUsed.end()) {
            metadata.channelsUsed.push_back(note.channel);
        }

        metadata.approximateDurationBeats = std::max(
            metadata.approximateDurationBeats,
            note.startBeat + note.durationBeats);
    }

    std::sort(metadata.channelsUsed.begin(), metadata.channelsUsed.end());
    metadata.origin = core::MidiTrackOrigin::generated;
    return metadata;
}

void writeSuccessMarkerFile(const std::filesystem::path& jobDirectory)
{
    std::error_code ec;
    const auto markerPath = jobDirectory / ".success";
    std::ofstream marker(markerPath, std::ios::out | std::ios::trunc);
    if (!marker.good()) {
        return;
    }

    marker << "ok\n";
    marker.close();
    std::filesystem::last_write_time(markerPath, std::filesystem::file_time_type::clock::now(), ec);
}

}  // namespace

struct EmbeddedHermesEngine::PythonState {
    std::unique_ptr<py::scoped_interpreter> interpreter;
    py::object extractFunction;
    py::object writeMidiAdapterFunction;
    py::object bassPipelineFunction;
    py::object syncFunction;
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

        std::vector<std::string> warnings = parseWarningsPayload(payload);

        auto result = HermesOperationResult::success(
            buildGroupsMessage(generated.size(), totalNotes),
            options.resultLayout,
            std::move(generated),
            bpmUsed,
            std::move(warnings),
            HermesOperationKind::drumsExtraction);
        result.sourceAudioTrackId = context.trackId;
        result.statistics.outputNoteCount = totalNotes;
        return result;
    } catch (const std::exception& ex) {
        return HermesOperationResult::unavailable(
            std::string("Embedded Hermes execution failed: ") + ex.what());
    }
}

HermesOperationResult EmbeddedHermesEngine::bassMakeOrRepairMidiFromWav(
    const HermesAudioMidiPairContext& context,
    const HermesBassOptions& options)
{
    const auto contextValidation = validateTrackContextForAudioMidiPair(context);
    if (!contextValidation.ok) {
        return HermesOperationResult::invalidInput(contextValidation.message);
    }

    const auto optionsValidation = validateBassOptions(options);
    if (!optionsValidation.ok) {
        return HermesOperationResult::invalidInput(optionsValidation.message);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureRuntimeReady()) {
        return unavailableResult("Bass -> Repair MIDI against WAV");
    }

    std::string cacheError;
    const auto jobDirectory = createHermesJobDirectory("bass_repair", cacheError);
    if (!cacheError.empty() || jobDirectory.empty()) {
        return HermesOperationResult::unavailable(
            "Bass -> Repair MIDI against WAV unavailable: " + cacheError);
    }

    const auto adapterMidiPath = jobDirectory / "input" / "adapter_source.mid";
    std::error_code ec;
    std::filesystem::create_directories(adapterMidiPath.parent_path(), ec);

    try {
        py::gil_scoped_acquire acquire;

        py::list notePayload;
        notePayload.attr("clear")();
        for (const auto& note : context.midiNotes) {
            py::dict noteDict;
            noteDict["pitch"] = note.pitch;
            noteDict["velocity"] = note.velocity;
            noteDict["channel"] = std::clamp(note.channel, 1, 16);
            noteDict["start_tick"] = beatToTick(note.startBeat, context.midiSourceMetadata.ticksPerQuarterNote);
            noteDict["end_tick"] = beatToTick(
                note.startBeat + note.durationBeats,
                context.midiSourceMetadata.ticksPerQuarterNote);
            notePayload.append(noteDict);
        }

        py::list tempoPayload;
        tempoPayload.attr("clear")();
        for (const auto& tempo : context.midiSourceMetadata.tempoMap) {
            py::dict tempoDict;
            tempoDict["tick"] = beatToTick(tempo.beatPosition, context.midiSourceMetadata.ticksPerQuarterNote);
            tempoDict["tempo_us_per_beat"] = tempo.microsecondsPerQuarterNote;
            tempoPayload.append(tempoDict);
        }

        if (tempoPayload.empty()) {
            py::dict defaultTempo;
            defaultTempo["tick"] = 0;
            defaultTempo["tempo_us_per_beat"] = 500000;
            tempoPayload.append(defaultTempo);
        }

        pythonState_->writeMidiAdapterFunction(
            pathToUtf8(adapterMidiPath),
            notePayload,
            context.midiSourceMetadata.ticksPerQuarterNote,
            tempoPayload,
            context.midiTrack.trackName);

        py::dict runOptions;
        runOptions["source"] = "dawhermes";
        runOptions["layer"] = "bass";
        runOptions["track_name_prefix"] = "Hermes";

        const auto payload = pythonState_->bassPipelineFunction(
                                 pathToUtf8(adapterMidiPath),
                                 context.audioTrack.audioSourcePath,
                                 pathToUtf8(jobDirectory),
                                 runOptions)
                                 .cast<py::dict>();

        if (payload["status"].cast<std::string>() != "success") {
            return HermesOperationResult::unavailable(
                payload["message"].cast<std::string>()
                + " Diagnostics retained in " + pathToUtf8(jobDirectory));
        }

        const auto ticksPerQuarterNote = std::max(1, payload["ticks_per_beat"].cast<int>());
        const auto tempoMap = parseTempoMapPayload(payload["tempo_map"].cast<py::list>(), ticksPerQuarterNote);
        const auto notes = parseNotesPayload(payload["notes"].cast<py::list>(), ticksPerQuarterNote);
        const auto warnings = parseWarningsPayload(payload);

        const auto resultTrackName = options.resultTrackName.empty()
                                         ? context.midiTrack.trackName + " - Hermes Bass Repaired"
                                         : options.resultTrackName;

        HermesGeneratedMidiTrack generatedTrack;
        generatedTrack.trackName = resultTrackName;
        generatedTrack.semanticLayer = "bass_repair";
        generatedTrack.enabledLayer = true;
        generatedTrack.emptyLayer = notes.empty();
        generatedTrack.notes = notes;

        generatedTrack.midiSourceMetadata = buildGeneratedMetadata(
            context.midiSourceMetadata,
            resultTrackName,
            ticksPerQuarterNote,
            tempoMap,
            notes);

        py::dict statisticsPayload;
        if (payload.contains(py::str("statistics"))) {
            statisticsPayload = payload["statistics"].cast<py::dict>();
        }

        HermesOperationStatistics statistics;
        statistics.inputNoteCount = parseUnsignedStatistic(statisticsPayload, "input_note_count");
        statistics.outputNoteCount = parseUnsignedStatistic(statisticsPayload, "output_note_count");
        statistics.mergedCount = parseUnsignedStatistic(statisticsPayload, "merged_count");
        statistics.insertedCount = parseUnsignedStatistic(statisticsPayload, "inserted_count");
        statistics.splitCount = parseUnsignedStatistic(statisticsPayload, "split_count");
        statistics.removedOrMutedCount = parseUnsignedStatistic(statisticsPayload, "removed_or_muted_count");

        auto result = HermesOperationResult::success(
            payload["message"].cast<std::string>(),
            HermesResultLayout::separateMidiTracks,
            { generatedTrack },
            tempoUsToBpm(tempoMap.front().microsecondsPerQuarterNote),
            warnings,
            HermesOperationKind::bassRepair);
        result.sourceAudioTrackId = context.audioTrack.trackId;
        result.sourceMidiTrackId = context.midiTrack.trackId;
        result.resultName = resultTrackName;
        result.ticksPerQuarterNote = ticksPerQuarterNote;
        result.tempoMap = tempoMap;
        result.statistics = statistics;

        writeSuccessMarkerFile(jobDirectory);

        std::error_code cleanupEc;
        std::filesystem::remove_all(jobDirectory, cleanupEc);
        if (cleanupEc) {
            result.warnings.push_back(
                "Successful Hermes cache cleanup failed for: " + pathToUtf8(jobDirectory));
        }

        return result;
    } catch (const std::exception& ex) {
        return HermesOperationResult::unavailable(
            std::string("Embedded Hermes bass pipeline failed: ") + ex.what()
            + " Diagnostics retained in " + pathToUtf8(jobDirectory));
    }
}

HermesOperationResult EmbeddedHermesEngine::synchronizeMidiWithWav(
    const HermesAudioMidiPairContext& context,
    const HermesSyncOptions& options)
{
    const auto contextValidation = validateTrackContextForAudioMidiPair(context);
    if (!contextValidation.ok) {
        return HermesOperationResult::invalidInput(contextValidation.message);
    }

    const auto optionsValidation = validateSyncOptions(options);
    if (!optionsValidation.ok) {
        return HermesOperationResult::invalidInput(optionsValidation.message);
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureRuntimeReady()) {
        return unavailableResult("Synchronize MIDI with WAV");
    }

    std::string cacheError;
    const auto jobDirectory = createHermesJobDirectory("midi_sync", cacheError);
    if (!cacheError.empty() || jobDirectory.empty()) {
        return HermesOperationResult::unavailable("Synchronize MIDI with WAV unavailable: " + cacheError);
    }

    const auto adapterMidiPath = jobDirectory / "input" / "adapter_source.mid";
    const auto outputMidiPath = jobDirectory / "output" / "synced.mid";

    std::error_code ec;
    std::filesystem::create_directories(adapterMidiPath.parent_path(), ec);
    std::filesystem::create_directories(outputMidiPath.parent_path(), ec);

    try {
        py::gil_scoped_acquire acquire;

        py::list notePayload;
        notePayload.attr("clear")();
        for (const auto& note : context.midiNotes) {
            py::dict noteDict;
            noteDict["pitch"] = note.pitch;
            noteDict["velocity"] = note.velocity;
            noteDict["channel"] = std::clamp(note.channel, 1, 16);
            noteDict["start_tick"] = beatToTick(note.startBeat, context.midiSourceMetadata.ticksPerQuarterNote);
            noteDict["end_tick"] = beatToTick(
                note.startBeat + note.durationBeats,
                context.midiSourceMetadata.ticksPerQuarterNote);
            notePayload.append(noteDict);
        }

        py::list tempoPayload;
        tempoPayload.attr("clear")();
        for (const auto& tempo : context.midiSourceMetadata.tempoMap) {
            py::dict tempoDict;
            tempoDict["tick"] = beatToTick(tempo.beatPosition, context.midiSourceMetadata.ticksPerQuarterNote);
            tempoDict["tempo_us_per_beat"] = tempo.microsecondsPerQuarterNote;
            tempoPayload.append(tempoDict);
        }

        if (tempoPayload.empty()) {
            py::dict defaultTempo;
            defaultTempo["tick"] = 0;
            defaultTempo["tempo_us_per_beat"] = 500000;
            tempoPayload.append(defaultTempo);
        }

        pythonState_->writeMidiAdapterFunction(
            pathToUtf8(adapterMidiPath),
            notePayload,
            context.midiSourceMetadata.ticksPerQuarterNote,
            tempoPayload,
            context.midiTrack.trackName);

        py::dict runOptions;
        runOptions["source"] = "dawhermes";
        runOptions["layer"] = syncRoleToken(options.role);
        if (!options.preserveTempoMap && options.bpmOverride.has_value()) {
            runOptions["bpm_override"] = options.bpmOverride.value();
        } else {
            runOptions["bpm_override"] = py::none();
        }

        const auto payload = pythonState_->syncFunction(
                                 pathToUtf8(adapterMidiPath),
                                 context.audioTrack.audioSourcePath,
                                 pathToUtf8(outputMidiPath),
                                 runOptions)
                                 .cast<py::dict>();

        if (payload["status"].cast<std::string>() != "success") {
            return HermesOperationResult::unavailable(
                payload["message"].cast<std::string>()
                + " Diagnostics retained in " + pathToUtf8(jobDirectory));
        }

        const auto ticksPerQuarterNote = std::max(1, payload["ticks_per_beat"].cast<int>());
        const auto tempoMap = parseTempoMapPayload(payload["tempo_map"].cast<py::list>(), ticksPerQuarterNote);
        const auto notes = parseNotesPayload(payload["notes"].cast<py::list>(), ticksPerQuarterNote);
        const auto warnings = parseWarningsPayload(payload);

        const auto resultTrackName = options.resultTrackName.empty()
                                         ? context.midiTrack.trackName + " - Hermes Synced"
                                         : options.resultTrackName;

        HermesGeneratedMidiTrack generatedTrack;
        generatedTrack.trackName = resultTrackName;
        generatedTrack.semanticLayer = "midi_sync";
        generatedTrack.enabledLayer = true;
        generatedTrack.emptyLayer = notes.empty();
        generatedTrack.notes = notes;

        generatedTrack.midiSourceMetadata = buildGeneratedMetadata(
            context.midiSourceMetadata,
            resultTrackName,
            ticksPerQuarterNote,
            tempoMap,
            notes);

        py::dict statisticsPayload;
        if (payload.contains(py::str("statistics"))) {
            statisticsPayload = payload["statistics"].cast<py::dict>();
        }

        HermesOperationStatistics statistics;
        statistics.inputNoteCount = parseUnsignedStatistic(statisticsPayload, "input_note_count");
        statistics.outputNoteCount = parseUnsignedStatistic(statisticsPayload, "output_note_count");
        statistics.alignedCount = parseUnsignedStatistic(statisticsPayload, "aligned_count");
        statistics.keepOriginalCount = parseUnsignedStatistic(statisticsPayload, "keep_original_count");
        statistics.reviewTimingCount = parseUnsignedStatistic(statisticsPayload, "review_timing_count");
        statistics.noAudioEvidenceCount = parseUnsignedStatistic(statisticsPayload, "no_audio_evidence_count");

        auto result = HermesOperationResult::success(
            payload["message"].cast<std::string>(),
            HermesResultLayout::separateMidiTracks,
            { generatedTrack },
            tempoUsToBpm(tempoMap.front().microsecondsPerQuarterNote),
            warnings,
            HermesOperationKind::midiWavSynchronization);
        result.sourceAudioTrackId = context.audioTrack.trackId;
        result.sourceMidiTrackId = context.midiTrack.trackId;
        result.resultName = resultTrackName;
        result.ticksPerQuarterNote = ticksPerQuarterNote;
        result.tempoMap = tempoMap;
        result.statistics = statistics;

        writeSuccessMarkerFile(jobDirectory);

        std::error_code cleanupEc;
        std::filesystem::remove_all(jobDirectory, cleanupEc);
        if (cleanupEc) {
            result.warnings.push_back(
                "Successful Hermes cache cleanup failed for: " + pathToUtf8(jobDirectory));
        }

        return result;
    } catch (const std::exception& ex) {
        return HermesOperationResult::unavailable(
            std::string("Embedded Hermes sync workflow failed: ") + ex.what()
            + " Diagnostics retained in " + pathToUtf8(jobDirectory));
    }
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

        // Insert local midi-cleaner paths at the front so compatible pinned packages
        // in its virtual environment are used ahead of global site-packages.
        if (isExistingDir(posixSitePackages)) {
            sysPath.attr("insert")(0, pathToUtf8(posixSitePackages));
        }

        if (isExistingDir(windowsSitePackages)) {
            sysPath.attr("insert")(0, pathToUtf8(windowsSitePackages));
        }

        if (isExistingDir(sourcePath)) {
            sysPath.attr("insert")(0, pathToUtf8(sourcePath));
        }

        py::dict globals;
        const auto bridgeSource = std::string(kPythonBridgeSourcePart1) + std::string(kPythonBridgeSourcePart2);
        py::exec(bridgeSource, globals);
        pythonState_->extractFunction = globals["dawhermes_extract_drums"];
        pythonState_->writeMidiAdapterFunction = globals["dawhermes_write_midi_adapter"];
        pythonState_->bassPipelineFunction = globals["dawhermes_run_bass_pipeline"];
        pythonState_->syncFunction = globals["dawhermes_run_midi_sync"];

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
