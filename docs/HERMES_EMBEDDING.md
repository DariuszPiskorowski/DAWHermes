# Hermes Embedding (Milestone 2)

Milestone 2 provides in-process Hermes execution for these real workflows:

- Drums -> Make MIDI from WAV
- Bass -> Repair MIDI against WAV
- Synchronize MIDI with WAV

## Architecture

DAWHermes keeps a neutral engine boundary (`IHermesEngine`) and now provides:

- `EmbeddedHermesEngine` for real in-process execution;
- `StubHermesEngine` for non-implemented commands and test-safe fallback behavior.

The UI still depends only on `IHermesEngine`.

Milestone 2 executes drums, bass, and sync commands on a serialized background worker to avoid blocking the UI thread.

## Runtime stack

- pybind11::embed pinned to `v3.0.4`
- CPython `3.11` (exact) required at CMake configure/build time
- `midi-cleaner` Python code imported directly in the embedded runtime

Milestone 2 runtime is not a private packaged Python bundle. It currently depends on:

- machine-wide CPython 3.11 installation;
- sibling/local `midi-cleaner` repository checkout;
- `midi-cleaner` Python dependencies (typically under `midi-cleaner/.venv`).

No Hermes CLI subprocess and no localhost Hermes service are used for this path.

## Repository discovery

`EmbeddedHermesEngine` resolves `midi-cleaner` using this order:

1. `DAWHERMES_HERMES_REPO` environment variable
2. sibling path `../midi-cleaner` (relative to current working directory)
3. `%USERPROFILE%/source/repos/midi-cleaner`

The repository must contain `src/midi_cleaner`.

## Python path setup

The embedded runtime prepends these paths when available:

- `<midi-cleaner>/src`
- `<midi-cleaner>/.venv/Lib/site-packages`
- `<midi-cleaner>/.venv/lib/site-packages`

This allows reuse of the local midi-cleaner environment without spawning external processes.

## Drums/Bass/Sync data flow

1. User selects audio track with assigned WAV source.
2. For pair workflows (bass/sync), the user selects an audio+MIDI pair with valid imported MIDI metadata.
3. DAWHermes validates Hermes options and track context.
4. Embedded Python calls:
	- `extract_drums_from_audio(...)` for drums,
	- `process_stem_pipeline(...)` for bass repair,
	- `sync_midi_with_wav(...)` for MIDI/WAV synchronization.
5. Returned data is converted to DAWHermes MIDI notes in memory.
6. Grouped multitrack drum results create a real group track with child MIDI tracks.
7. New tracks are created directly in the DAW project model.

Before insertion, DAWHermes validates generated MIDI events (pitch, velocity, start, duration, channel). If validation fails, no partial track insertion is kept.

Inserted Hermes results are grouped as one logical operation so Undo removes the full generated set and Redo restores it without re-running embedded extraction.

For successful bass/sync operations, DAWHermes deletes the temporary Hermes cache job directory immediately after result extraction. Failed operations preserve diagnostics in cache and include the cache location in the failure message.

When `createEmptyEnabledLayers` is enabled in the drums options, enabled semantic layers with no notes can still be represented as empty MIDI tracks. Disabled empty layers are not created.

When extraction returns zero notes and no meaningful enabled-empty layers, DAWHermes reports a validation-style failure instead of claiming success.

No user-visible intermediate MIDI files are produced by DAWHermes in these workflows.

## Current Milestone 2 limits

- set/fix BPM remains non-implemented in DAWHermes and reports honest status.
