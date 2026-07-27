# Milestone 3.2: MIDI Editing and Selected-Track Export

## Status

Milestone 3.2 is implemented locally on the WIP branch and awaits user manual acceptance.

This document describes the current implementation checkpoint. It does not mark the whole M3 series as manually accepted.

## Implemented Editing Features

- Stable project note IDs for imported, generated, and newly created MIDI notes.
- Editable primary-track note selection in the Piano Roll.
- Ctrl-click additive/toggle selection.
- Empty-space selection clear.
- Marquee note selection with replace and additive modes.
- Double-click note creation in empty piano-roll space.
- Delete/Backspace and Edit -> Delete Selected Notes.
- Mouse movement of selected notes in time and pitch.
- Right-edge duration resize.
- Arrow-key nudging:
  - Left/Right by active grid step;
  - Up/Down by semitone;
  - Shift+Up/Down by octave.
- Snap toggle near the grid selector.
- Compact velocity editor for selected notes.
- Edit -> Quantize Selected Notes to Grid.
- File -> Export Selected MIDI Track... for the selected non-empty MIDI track.

## Undo and Redo

MIDI note mutations use the existing `ProjectHistory` architecture.

The following actions create one history transaction when they change note data:

- creation;
- deletion;
- mouse move;
- duration resize;
- keyboard nudge;
- velocity edit;
- quantize.

Selection changes, marquee selection, Snap toggle changes, view changes, and MIDI export do not create history transactions.

Hermes result Undo/Redo remains separate fallback behaviour and Redo does not rerun Hermes analysis.

## Export Behaviour

Selected-track export is implemented by `src/midi/MidiTrackExporter`.

The exporter:

- uses the edited in-memory `Track::midiNotes`;
- does not re-read the source `.mid`;
- writes standard MIDI through JUCE MIDI facilities;
- preserves pitch, velocity, channel, start beat, duration, and note-off timing;
- preserves track name;
- preserves PPQ, tempo map, MIDI file type, and time signatures where source metadata is available;
- falls back to PPQ `960`, MIDI type `1`, default tempo, and 4/4 if metadata is missing;
- emits deterministic event ordering;
- emits note-off events before note-on events at the same tick where needed;
- excludes internal DAWHermes stable note IDs from exported MIDI metadata;
- rejects empty MIDI tracks gracefully.

## Source-File Safety

Editing and export operate on DAWHermes project memory. Source MIDI and WAV files assigned to tracks are not modified by note editing or selected-track MIDI export.

Automated export tests write only to isolated temporary directories and remove their temp output. Exported MIDI files are not committed.

## Out of Scope

Not implemented in this checkpoint:

- playback;
- Play/Stop/Record;
- audio engine;
- Timeline editing;
- controller lanes;
- copy/paste;
- VST hosting;
- project save/load;
- clip, multitrack, stem, or Cubase-specific exchange/export.

## Current Limitations

- Piano Roll styling remains functional and intentionally not final.
- Export is selected-track only.
- Quantize has no strength, swing, or groove controls.
- Velocity editing is a compact selected-note control, not a full velocity lane.
- M3.2 still needs user manual acceptance in the installed native Windows app.
