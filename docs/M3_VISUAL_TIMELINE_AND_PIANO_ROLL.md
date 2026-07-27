# Milestone 3.1: Visual Timeline, Piano Roll, and MIDI Comparison

## Scope

Milestone 3.1 adds visual inspection tools for timeline and MIDI content while preserving Milestone 2 Hermes functionality.

Milestone 3.1 visual functionality is accepted.

This milestone is read-only:

- no note editing;
- no playback/transport integration changes;
- no audio-engine changes.

## Implemented in this milestone

1. Shared horizontal beat viewport (`core/TimelineViewport`) with:
   - deterministic beat<->pixel mapping;
   - zoom in/out;
   - horizontal scrolling;
   - fit-to-content support.
2. Time-map resolution (`core/MidiTimeMap`) with fallback chain:
   - selected MIDI metadata;
   - project MIDI metadata;
   - defaults (tempo + 4/4 signature).
3. Timeline lane geometry and piano-roll note culling (`core/TimelineGeometry`).
4. Deterministic MIDI note comparison model (`core/MidiComparisonModel`) with categories:
   - unchanged;
   - timing adjusted;
   - velocity adjusted;
   - pitch changed;
   - added;
   - removed.
5. UI components:
   - `ui/TimeRulerView`;
   - `ui/TimelineView`;
   - `ui/PianoKeyboardView`;
   - `ui/PianoRollView`;
   - `ui/MidiComparisonLegend`.
6. Main workspace integration in `ui/MainComponent`:
   - replacement of timeline/MIDI placeholders with live components;
   - beat-grid selector (`1/4`, `1/8`, `1/16`, `1/32`);
   - horizontal zoom/scroll/fit controls;
   - pitch zoom/fit controls;
   - `View -> Compare Selected MIDI Tracks` command (requires exactly two MIDI tracks);
   - note hover diagnostics in piano roll.
7. Import metadata extension:
   - `MidiImportParser` now captures and stores MIDI time-signature events.

## Validation

Automated:

- `scripts/test.ps1` must pass, including new deterministic tests for viewport/time-map/geometry/comparison.

Build and install:

- `scripts/build-release.ps1`
- `scripts/install-local.ps1`

Manual visual acceptance (required before commit/push):

1. Launch installed `DAWHermes.exe` from Start Menu or Desktop shortcut.
2. Confirm timeline ruler/grid and lane rendering.
3. Confirm piano keyboard/piano-roll rendering and hover diagnostics.
4. Select two MIDI tracks and enable compare mode in View menu.
5. Confirm legend counts and color overlays update as expected.

## Out of scope (deferred)

Deferred to Milestone 3.2 or later:

- create/move/resize/delete MIDI notes;
- playback synchronization and transport coupling to timeline/piano roll;
- edit-history granularity for visual MIDI edits.

## Known visual boundary

Milestone 3.1 visual functionality is accepted.

The current Timeline and Piano Roll styling is intentionally functional rather than final.
Visual polish, spacing, colours and presentation will be revisited near the end of DAWHermes development.

This is not a defect and does not block Milestone 3.1 completion.

## Follow-up implementation checkpoint

Milestone 3.2 is manually accepted and published.

M3.2 builds on these visual surfaces with direct piano-roll MIDI editing and selected-track MIDI export. The M3.1 comparison surface remains read-only. M3.3 adds selected-track MIDI audition while WAV playback, Timeline editing, controller lanes, copy/paste, and Cubase-specific exchange remain deferred.

See `docs/M3_2_MIDI_EDITING.md` for the local M3.2 implementation notes.
