# DAWHermes Architecture (Milestone 5.1)

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
- `src/audio` - central application-lifetime device service, deterministic
  whole-project playback snapshots/timing, preloaded WAV stem data, safe
  source-rate conversion, internal synth/mixer, and callback loop handling.
- `src/plugins` - persistent VST3 instrument catalog and crash recovery,
  application-lifetime per-track plugin runtimes, JUCE playhead delivery,
  editor windows, and bounded latency layout publication.
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
  every selected audio track with a readable imported WAV;
- WAV data is decoded in bounded blocks directly into final immutable
  mono/stereo channel storage, without a second full-file decoded copy;
- one snapshot has a 512 MiB aggregate decoded-WAV budget; tracks that would
  exceed it are skipped with concise non-modal status while smaller valid
  selected tracks remain playable;
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
- bounded WAV tempo analysis compares local autocorrelation peaks with their
  half/double-tempo alternatives using beat-grid and intermediate-onset
  evidence; an insufficient octave score margin remains unconfident;
- its 128-entry path/size/mtime LRU session cache runs outside the callback,
  while source audio always plays at original speed;
- project-model source paths are UTF-8 and are converted explicitly to native
  Windows/JUCE paths at file boundaries.

Milestone 4.1 replaces selection audition with central project playback:

- `MainApplication` owns one `AudioDeviceService`, which owns the only
  `AudioDeviceManager` and the only registered project audio callback;
- saved JUCE device state is persisted through `ApplicationProperties`, with a
  one-shot default-output fallback and usable no-device state;
- `SelectionPlaybackModel` retains its accepted regression APIs and adds
  deterministic whole-project builders that aggregate all non-empty MIDI and
  readable WAV tracks;
- project tempo uses the first explicit MIDI map, otherwise the first confident
  readable WAV result, otherwise 120 BPM; conflicts use the first map and emit
  a diagnostic;
- full immutable content snapshots are separated from small live
  `ProjectRoutingState` snapshots so Mute/Solo does not decode or rebuild media;
- hierarchical Mute/Solo lives in `core/TrackRouting` and is not part of
  `ProjectHistory`;
- beat-coordinate loop normalization lives in `core/TimelineLoop`; prepared
  loop seconds, event cursor, and active-at-start notes are published before
  the callback consumes them;
- callback wrap directly repositions the common project clock, WAV sampling,
  MIDI event cursor, and reconstructed voices;
- snapshot, routing, and loop owners are retired on the message thread, never
  destructed by the realtime callback.

Milestone 5.1 extends that one callback with VST3 instruments:

- only the JUCE VST3 host format is compiled; VST2, AU, AAX, LV2 and LADSPA
  hosting remain disabled;
- the catalog loads from normal DAWHermes settings without scanning at startup,
  and a deliberate background scanner publishes a replacement only after a
  successful complete scan;
- `Track` stores only an `InstrumentAssignment` descriptor while the
  application-lifetime host owns processors and editor windows;
- each MIDI track receives an independent plugin instance and only its own
  channel-preserving MIDI events;
- atomically published runtime registries keep creation, replacement and
  destruction outside the callback;
- plugin MIDI/audio scratch buffers and compensation delay lines are bounded
  and prepared before callback use;
- plugins receive sample, seconds, PPQ, BPM, time-signature, playing and loop
  position through JUCE `AudioPlayHead`;
- the maximum active plugin latency is published outside the callback, delaying
  lower-latency plugins, WAV stems and the Internal Audition Synth to match;
- plugin editor windows are message-thread owned and limited to one per track;
- device restart reprepares existing instances without creating another audio
  manager or project callback.

Direct WAV import is a prepared batch operation:

- the native File chooser supplies one or more source paths;
- `AudioTrackImporter` validates mono/stereo WAV metadata without decoding on
  the audio callback and prepares filename-derived names plus absolute paths;
- one core `ImportAudioTracksCommand` creates and selects all valid tracks as a
  single `ProjectHistory` transaction;
- imported tracks retain sample rate, channel count, duration, frame count, bit
  depth, and source size metadata;
- Timeline waveform thumbnails, selection playback duration, and asynchronous
  BPM analysis consume the imported source path through their existing paths;
- Undo removes the batch and Redo recreates names, paths, and metadata;
- WAV sources remain in place and are never copied or modified.

For successful bass and sync operations, temporary Hermes cache job directories are deleted immediately. Failed operations preserve diagnostics in cache.

`StubHermesEngine` remains in the codebase for tests and for explicit placeholder behavior.

## Build and distribution boundary (Infrastructure CI.1)

The authoritative final build boundary is a clean GitHub-hosted
`windows-2022` x64 runner, not the user's music PC and not a self-hosted runner.
It configures one Release-only `build-ci` tree with Python 3.11 x64 and Visual
Studio 2022, compiles and runs the deterministic tests first, then builds the
application incrementally in the same tree with one worker.

`DAWHERMES_ENABLE_LTCG=OFF` is a permanent safety constraint. Generated project
inspection requires explicit `/GL-`, normal Release optimization and
`/LTCG:OFF`, and rejects active `/GL` or `/LTCG`. The resulting executable is
validated from its PE header as AMD64 without loading it.

The hosted boundary packages only runnable application files plus safe build
metadata, a complete SHA-256 manifest and a standalone installer. Runtime and
diagnostic artifacts are separate. No source tree, compiler output, user data,
private endpoints, plug-ins, licences, catalogs or settings cross into the
runtime artifact. Dependency/build caching is deliberately absent from the
clean CI.1 baseline.

The artifact installer verifies metadata, hashes and x64 architecture before
copying the precompiled runtime to `%LOCALAPPDATA%\DAWHermes\app`. It preserves
settings stored outside that directory and creates direct native shortcuts.
Hosted verification does not launch the GUI, exercise physical audio hardware
or scan commercial VST3 instruments; installed native smoke and explicit user
manual acceptance remain separate required boundaries.

## No HTTP/localhost architecture

DAWHermes is intended to embed Hermes directly, not orchestrate it as a separate localhost service. This avoids dual-process UX, fragile transfer protocols, and duplicated project state.

## One DAW-level AI assistant

AI orchestration is a DAW-level concern. Hermes remains a processing subsystem and does not become a separate AI endpoint.

## Native install and launch model

The install script deploys a runnable Release app to `%LOCALAPPDATA%\DAWHermes\app` and creates normal Desktop and Start Menu shortcuts that point directly to `DAWHermes.exe`.

No PowerShell or terminal launcher is used for normal user launch.

## Current limitations (Milestone 5.1)

Milestone 2 functionality is complete.
Milestone 3.1 visual functionality is accepted.

Implemented now:

- workspace shell and command surfaces;
- direct multi-WAV audio-track import and MIDI import into the track model;
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

- recording, input monitoring, track arming, or mixer controls;
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
- Milestones 3.2 and 3.3 are accepted and published. Milestone 3.4 is complete
  and manually accepted. Timeline editing, controller lanes, copy/paste, and
  Cubase-specific exchange remain deferred.
- M4.1 playback is audition-grade: it adds central device setup,
  whole-project playback, Mute/Solo, and looping, but no time stretching, beat
  warping, mixer, effects, recording, or production instrument hosting.
- M4.1 is complete and manually accepted.
- M5.1 VST3 instrument hosting is complete and manually accepted. It does not
  add effects, buses, automation, presets, project
  persistence, freeze, bounce or recording.
