# DAWHermes Architecture (Milestone 3.4)

## Why a new Windows repository

DAWHermes is a native Windows-first product. The existing `DAW-create-example` is a macOS Swift/SwiftUI prototype and remains a read-only reference for interaction ideas.

A direct Swift port would not satisfy the Windows-native tooling, deployment, and long-term embedded-Hermes direction required for this product. A dedicated repository allows clean Windows architecture and independent release management.

## Why JUCE + CMake

- JUCE provides cross-platform native desktop UI and audio/MIDI integration paths while still compiling to a normal Windows GUI executable.
- CMake enables deterministic dependency pinning and build automation without requiring Projucer.
- JUCE is pinned via FetchContent to immutable tag `8.0.13` to avoid moving-target dependency drift.

## Module boundaries

The codebase is split into explicit layers:

- `src/app` - application lifecycle, top-level window wiring, settings, logging bootstrap.
- `src/core` - in-memory project and track model, selection/controller state, deterministic layout geometry, timeline viewport/time-map logic, piano-roll geometry, and MIDI comparison model.
- `src/audio` - deterministic selection playback snapshots/timing, preloaded WAV
  stem data, safe source-rate conversion, and the internal default-device
  audition synth/mixer.
- `src/midi` - testable MIDI file export utilities that transform project MIDI tracks into standard MIDI files without depending on UI dialogs.
- `src/ui` - JUCE views, menu bar, context menus, timeline/piano-roll rendering, option dialogs, and MIDI/WAV import parsing utilities.
- `src/hermes` - neutral Hermes contracts, command availability rules, option validation, embedded Python engine, and connector boundaries.
- `tests` - non-GUI behavioural and validation tests.
- `scripts` - configure/build/test/install/uninstall automation for Windows.

This separation keeps Hermes integration swappable and testable without rewriting UI code.

## Deliberate interaction behaviour

- Left click on a track only changes selection.
- Right click deliberately opens a context menu.
- Hermes dialogs are opened only from explicit user commands.
- No command popup is triggered by passive selection.

This mirrors expected desktop DAW ergonomics and avoids disruptive automation.

## Hermes integration in Milestone 3.1

Milestone 3.1 keeps the Milestone 2 `IHermesEngine` integration unchanged while adding read-only visual analysis surfaces.

- The UI continues to call only the neutral interface.
- Embedded Python uses pybind11::embed and CPython 3.11.
- `midi-cleaner` drums, bass, and synchronization workflows are called directly in-process.
- Returned hit data is converted to DAWHermes MIDI notes in memory.
- No user-visible intermediate MIDI files are written in DAWHermes for this workflow.
- Hermes processing is run on a serialized background worker so the message thread remains responsive.
- Project-model mutation still happens on the message thread when background processing completes.
- Set/Fix BPM still returns explicit not-implemented status.

Milestone 3.1 adds:

- shared horizontal beat viewport consumed by timeline ruler, timeline lanes, and piano roll;
- time-signature aware bar/ruler calculation sourced from imported MIDI metadata with fallback defaults;
- deterministic MIDI comparison classification (`unchanged`, `timingAdjusted`, `velocityAdjusted`, `pitchChanged`, `added`, `removed`);
- color-coded visual compare overlay and summary legend.

Accepted Milestone 3.2 adds direct in-memory MIDI note editing and selected-track MIDI export:

- selected note IDs are stored as stable project note IDs, not vector indices;
- MIDI edits mutate `Track::midiNotes` through core editing helpers and `ProjectHistory` commands;
- selected-track export uses `src/midi/MidiTrackExporter` to write the current edited in-memory notes through JUCE MIDI facilities;
- export preserves pitch, velocity, channel, start beat, duration, note-off timing, track name, PPQ, tempo map, and time signatures where available;
- export does not re-read or modify the original source MIDI file and does not create Undo/Redo history entries.

Accepted Milestone 3.3 adds selected-track MIDI audition:

- Play captures an immutable snapshot of the primary selected track's edited `Track::midiNotes`;
- beat timing is converted through the imported tempo map, with 120 BPM fallback;
- a low-gain internal polyphonic sine synth writes directly to the system default audio output;
- the audio callback consumes immutable events and atomics only, without mutating `ProjectModel` or `ProjectHistory`;
- Stop, Panic, and shutdown silence all voices;
- Timeline and Piano Roll display a UI-timer-driven playhead;
- no VST, WAV playback, recording, mixer, temporary audio file, or external audio process is involved.

Milestone 3.4 extends the same device and callback into selected-stem audition:

- `SelectionPlaybackModel` chooses one primary selected non-empty MIDI track and
  every selected audio track with a readable assigned WAV;
- WAV data is decoded before playback into immutable mono/stereo sample storage;
- the callback maps one shared transport clock to MIDI events and WAV source
  frames, using linear interpolation when device and source sample rates differ;
- WAV time zero and MIDI transport time zero are shared, while MIDI continues to
  use its tempo map and WAV remains at original speed;
- Master Volume, Stop, Panic, device stop, and shutdown affect both synth and WAV;
- group tracks, empty tracks, non-selected tracks, and visual comparison ghosts
  contribute no playback;
- missing/unreadable WAV sources are skipped with concise non-modal status;
- no callback file I/O, model/selection/history mutation, Hermes, Python,
  second audio-device manager, or external helper process is introduced.
- retired decoded stem buffers are reclaimed outside the audio callback.
- one shared transport state owns stopped/playing/paused mode, current time,
  selection duration, and the immutable playback snapshot;
- Play, Pause/resume, Stop, Panic, and clamped 5-second seek operate on that
  shared state without creating history entries;
- resume and seek publish precomputed MIDI cursors and reconstruct only notes
  active at the target time, rather than scanning model data in the callback;
- Timeline and Piano Roll use the same authoritative transport position and
  horizontal viewport, with threshold-based follow during active playback;
- tempo resolution prefers explicit imported MIDI tempo metadata, then a
  confident asynchronously detected WAV tempo, then a 120 BPM audition fallback;
- bounded WAV tempo analysis and its path/size/mtime session cache run outside
  the callback, while source audio always plays at original speed.

For successful bass and sync operations, temporary Hermes cache job directories are deleted immediately. Failed operations preserve diagnostics in cache.

`StubHermesEngine` remains in the codebase for tests and for explicit placeholder behavior.

## No HTTP/localhost architecture

DAWHermes is intended to embed Hermes directly, not orchestrate it as a separate localhost service. This avoids dual-process UX, fragile transfer protocols, and duplicated project state.

## One DAW-level AI assistant

AI orchestration is a DAW-level concern. Hermes remains a processing subsystem and does not become a separate AI endpoint.

## Native install and launch model

The install script deploys a runnable Release app to `%LOCALAPPDATA%\DAWHermes\app` and creates normal Desktop and Start Menu shortcuts that point directly to `DAWHermes.exe`.

No PowerShell or terminal launcher is used for normal user launch.

## Current limitations (Milestone 3.4)

Milestone 2 functionality is complete.
Milestone 3.1 visual functionality is accepted.

Implemented now:

- workspace shell and command surfaces;
- file-backed audio source assignment and MIDI import into track model;
- extracted top-row-3-columns + full-width-bottom MIDI layout geometry;
- draggable splitter geometry with persisted layout state and reset command;
- embedded Hermes drums extraction, bass repair, and MIDI/WAV synchronization;
- visual timeline lanes with waveform thumbnails and beat grid/ruler;
- visual piano keyboard + piano-roll renderer with note hover diagnostics;
- compare-mode overlay for two selected MIDI tracks;
- grouped multitrack insertion as a real hierarchy (group track + child MIDI tracks);
- enabled-empty-layer aware insertion for drums extraction;
- transactional Hermes result insertion with rollback on validation failure;
- single-operation Undo/Redo that removes/restores Hermes-generated tracks/groups without re-running embedded analysis;
- Hermes option dialogs and validation;
- Composer Assistant safe connector boundary and settings/probe UI;
- logging and local shortcut installation workflow.

Not implemented now:

- recording or advanced device setup UI;
- Hermes set/fix BPM workflow;
- AI APIs;
- ACE exchange;
- Cubase export.
- Cubase-specific exchange/export.

Note: Milestone 2 includes minimal MIDI import for source-track creation and pairing workflows, but does not include full timeline/piano-roll editing.

Additional Milestone 3.1 boundaries:

- timeline ruler/lanes and piano roll share one horizontal beat viewport state;
- waveform drawing is static visualization only and not a playback transport surface;
- current Timeline and Piano Roll styling is intentionally functional rather than final, with visual polish deferred until near project end.
- Milestones 3.2 and 3.3 are accepted and published. Core Milestone 3.4
  WAV/MIDI playback is manually accepted; the transport completion is installed
  for final user acceptance. Timeline editing, controller lanes, copy/paste,
  and Cubase-specific exchange remain deferred.
- M3.4 playback is audition-grade: it includes Pause/resume, a counter,
  5-second seeking, playhead follow, and BPM resolution, but no time stretching,
  beat warping, looping, mixer, effects, direct WAV import, or final
  sample-accurate DAW mixing.
