# UI Reference Map (Milestones 3.1-4.1)

This document captures the structural UI alignment and visual-analysis surfaces active in Milestone 3.1.

Milestone 3.1 visual functionality is accepted.

## Reference intent

The macOS reference repository (`DAW-create-example`) is used as a read-only interaction and structure reference.

DAWHermes does not copy Swift/SwiftUI code. It mirrors high-level layout semantics in native JUCE.

## Structural mapping

DAWHermes layout is organized as:

1. Menu bar (top)
2. Transport/status strip (under menu)
3. Main workspace row with 3 columns:
   - left: tracks panel (header + track list)
   - center: timeline workspace (controls + ruler + lanes)
   - right: AI assistant panel
4. Bottom full-width row:
   - piano editor workspace (controls + keyboard + piano roll + compare legend)
5. Status strip (bottom)

This matches the requested "top 3 columns + bottom full-width MIDI" structure.

## Resizable workspace splitters

Milestone 1.1 adds three draggable separators inside the workspace area:

1. left/center column separator;
2. center/right column separator;
3. top-row/bottom-row separator.

Panel ratios are persisted to user settings and restored on launch.

`View -> Reset Panel Layout` restores default ratios.

## Deterministic geometry extraction

Layout geometry and panel-state sanitization are extracted into `core/MainLayoutGeometry` and consumed by `ui/MainComponent`.

Timeline/piano-roll behaviour is extracted into reusable core units:

- `core/TimelineViewport` for shared horizontal beat viewport mapping and zoom/scroll/fit;
- `core/MidiTimeMap` for tempo/signature fallback resolution and bar/grid helpers;
- `core/TimelineGeometry` for lane and pitch-space geometry/culling;
- `core/MidiComparisonModel` for deterministic note-delta classification.

Benefits:

- deterministic and testable sizing behavior;
- no hidden ad-hoc geometry logic inside view code;
- easier parity checks against future reference updates.

## Interaction behavior retained

Milestone 3.1 keeps deliberate desktop interaction rules:

- track selection does not auto-open tools;
- context menu opens only on right-click;
- Hermes dialogs open only after explicit command selection.

## Milestone 3.1 visual surfaces

Center timeline workspace:

- beat-grid selector: `1/4`, `1/8`, `1/16`, `1/32`;
- horizontal zoom in/out + fit controls;
- horizontal scroll control;
- time ruler with bar markers;
- lanes aligned to track-list order and row height;
- waveform thumbnails for audio-track lanes;
- mini note bars for MIDI-track lanes.

Bottom piano workspace:

- pitch zoom controls + fit-notes command;
- compact velocity editor for selected MIDI notes;
- piano keyboard at left;
- piano roll with shared horizontal viewport;
- hover diagnostics per note (pitch/velocity/start/duration/channel);
- compare legend and color-coded overlay when compare mode is enabled.

Accepted Milestone 3.2 editing surfaces:

- Snap toggle near the grid selector;
- editable primary-track MIDI note selection and marquee selection;
- double-click creation in empty piano-roll space;
- Delete/Backspace and Edit -> Delete Selected Notes;
- mouse movement and right-edge duration resize;
- arrow-key grid/semitone/octave nudging;
- Edit -> Quantize Selected Notes to Grid;
- File -> Export Selected MIDI Track... for the selected non-empty MIDI track.

Accepted Milestone 3.3 audition surfaces:

- compact Play, Stop, Panic, and Volume controls in the transport strip;
- Play enabled only for the primary selected non-empty MIDI track;
- orange playback playhead in Timeline and Piano Roll;
- no Record control and no audio-track playback.

Accepted Milestone 3.4 audition surfaces:

- `File -> Import Audio as Track...` opens the native multi-file WAV chooser;
- each valid WAV creates a filename-named audio track, stores source metadata,
  connects its Timeline waveform, and participates in one batch Undo/Redo;
- unreadable batch members are skipped with one non-modal aggregate status;
- compact `<<`, `Play`, `Pause`, `Stop`, `>>`, counter, BPM, `Panic`, and Master
  Volume controls are present in the transport strip;
- one selected MIDI track and all selected readable imported WAV tracks can play
  together;
- audio-only and MIDI-only selection remain playable;
- Pause/resume preserves the immutable playback selection and current position;
- changing the stopped playable selection resets its position to zero, while
  refreshing the same selection preserves an intentional stopped seek;
- `<<` and `>>` seek by 5 seconds and clamp to the selection bounds;
- Stop silences MIDI/WAV, resets the counter to zero, hides the playhead, and
  preserves the horizontal viewport; Pause preserves the visible playhead;
- Panic and Master Volume affect MIDI and WAV;
- skipped unreadable audio is reported in the existing non-modal status strip;
- the BPM readout prefers explicit MIDI tempo, then confidently detected WAV
  tempo, then the 120 BPM audition fallback;
- Timeline and Piano Roll retain one shared orange playhead and viewport, with
  threshold-based follow during active playback;
- no Record, mixer, mute/solo, looping, or audio-editing
  controls are added.

Implemented Milestone 4.1 surfaces (awaiting manual acceptance):

- top-level `Audio` menu with `Audio Settings...`, `Test Output`,
  `Restart Audio Device`, and `Audio Device Status...`;
- concise current-device summary in the bottom status area;
- transport order `<<`, `Play`, `Pause`, `Stop`, `>>`, `Loop`, counter, BPM;
- Play targets all playable project tracks, independent of selection;
- accessible `M` and `S` buttons on every track and group row;
- translucent beat-coordinate loop range across ruler and Timeline lanes;
- ruler click seeks, drag creates/resizes/moves, and right-click explicitly
  offers `Clear Loop Range`;
- Loop remains visible when disabled and repeats inside the audio callback;
- no Record, arming, monitoring, mixer, VST, or audio-edit controls.

## Compare mode

`View -> Compare Selected MIDI Tracks` is enabled only when exactly two MIDI tracks are selected.

Comparison is read-only and visual only. It does not modify MIDI notes.

## Visual boundary

The current Timeline and Piano Roll styling is intentionally functional rather than final.
Visual polish, spacing, colours and presentation will be revisited near the end of DAWHermes development.

MIDI note editing is accepted Milestone 3.2 functionality.
Milestone 3.4 is complete and manually accepted.
Milestone 4.1 is implemented and awaiting manual acceptance.
M3.4 adds selected imported-WAV playback alongside the M3.3 internal MIDI synth.
Its asynchronous cached BPM detector changes beat/time mapping only; it does not
change WAV speed. M3.4 does not add time stretching, final sample-accurate
mixing, Timeline editing, controller lanes, copy/paste, or Cubase-specific
exchange.
