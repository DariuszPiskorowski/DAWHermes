# Milestone 2: Bass Repair and MIDI/WAV Sync

## Scope

Milestone 2 adds real embedded Hermes processing for:

- `Bass -> Repair MIDI against WAV...`
- `Synchronize MIDI with WAV...`

while preserving accepted Milestone 1.1 behaviors (splitters, grouped drum hierarchy, enabled-empty-layer handling, and non-blocking Hermes execution).

Milestone 1.1 is complete.
Milestone 2 functional integration is complete and manually accepted.

## User-visible behavior

### MIDI import entry point

MIDI import is exposed from the File menu as:

- `File -> Import MIDI as Track...`

Behavior:

- only note-bearing source tracks are offered for import;
- when multiple note-bearing tracks exist, user explicitly selects one;
- default selection is the first note-bearing candidate;
- metadata-only or note-empty tracks are not imported.

This is a minimal Milestone 2 import path. Full visual MIDI editing and piano-roll/timeline workflows remain Milestone 3 scope.

### Pair-command availability

Bass and sync commands require a selected audio+MIDI pair where:

- selected audio track has a non-empty source path;
- selected audio source file exists on disk;
- selected MIDI track has note content;
- selected MIDI track carries valid source metadata.

Bass repair requires WAV + MIDI.
Synchronization requires WAV + MIDI.

### Selection and context menu behavior

- selecting a track only selects it;
- right-click on an unselected track selects only that track before opening context menu;
- right-click on an already selected track preserves current multi-selection.

## Embedded processing details

- `EmbeddedHermesEngine` runs bass and sync via `midi-cleaner` inside the process.
- Processing runs on serialized `HermesJobRunner` background execution.
- Source tracks are preserved.
- Repaired/synchronized results are inserted as new MIDI tracks.
- Result tracks retain source lineage metadata (source file path/name, source track index, PPQ, tempo map, note/channel summary).
- Undo/Redo restores stored Hermes results without reanalysis.
- For successful bass/sync jobs, temporary Hermes job directories are deleted immediately.
- For failed jobs, diagnostics are retained and cache location is reported in the failure message.

## Automated verification

### Standard compile/test

```powershell
.\scripts\configure.ps1
.\scripts\test.ps1
.\scripts\build-release.ps1
```

### Real-assets verification (opt-in)

```powershell
.\scripts\test-m2-real-assets.ps1 `
  -BassMidi <path> -BassWav <path> `
  -DrumMidi <path> -DrumWav <path> `
  -SynthMidi <path> -SynthWav <path>
```

This script runs release `DAWHermesTests.exe` in a dedicated real-assets mode and writes:

- engine report: `build/m2-real-assets-report.json`
- audit report: `build/m2-real-assets-audit.json`

Audit checks include:

- pre/post SHA-256 integrity for all six input files;
- no unexpected new outputs in monitored Downloads directories;
- no residual Hermes cache job directories from successful runs;
- operation status, note counts, warnings, tempo-preservation checks, and metadata-lineage checks.

The real-assets verification run uses supplied read-only Bass/Drum/Synth WAV+MIDI pairs.
Those personal source assets are not repository dependencies and are not committed.

## Acceptance status

Manual GUI acceptance for Milestone 2 functional integration passed on the installed application build.

Known limitation:

Milestone 2 functional integration is accepted.
Musical-quality comparison of original, repaired and synchronized MIDI remains pending visual piano-roll/timeline support in Milestone 3.

## Remaining out of scope

Milestone 2 does not implement:

- `Set / Fix BPM...` real engine path;
- piano-roll editing/audio playback engine;
- VST hosting, AI integration, ACE exchange, or Cubase export.
