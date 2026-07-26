# DAWHermes Architecture (Milestone 1.1)

## Why a new Windows repository

DAWHermes is a native Windows-first product. The existing `DAW-create-example` is a macOS Swift/SwiftUI prototype and remains a read-only reference for interaction ideas.

A direct Swift port would not satisfy the Windows-native tooling, deployment, and long-term embedded-Hermes direction required for this product. A dedicated repository allows clean Windows architecture and independent release management.

## Why JUCE + CMake

- JUCE provides cross-platform native desktop UI and audio/MIDI integration paths while still compiling to a normal Windows GUI executable.
- CMake enables deterministic dependency pinning and build automation without requiring Projucer.
- JUCE is pinned via FetchContent to immutable tag `8.0.13` to avoid moving-target dependency drift.

## Milestone 1 module boundaries

The codebase is split into explicit layers:

- `src/app` - application lifecycle, top-level window wiring, settings, logging bootstrap.
- `src/core` - in-memory project and track model plus selection/controller state and extracted deterministic layout geometry.
- `src/ui` - JUCE views, menu bar, context menus, workspace rendering, and option dialogs.
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

## Hermes integration in Milestone 1.1

Milestone 1 keeps `IHermesEngine` as the UI boundary and introduces `EmbeddedHermesEngine` for real in-process drums extraction.

- The UI continues to call only the neutral interface.
- Embedded Python uses pybind11::embed and CPython 3.11.
- `midi-cleaner` drums extraction is called directly in-process.
- Returned hit data is converted to DAWHermes MIDI notes in memory.
- No user-visible intermediate MIDI files are written in DAWHermes for this workflow.
- Drums processing is run on a serialized background worker so the message thread remains responsive.
- Project-model mutation still happens on the message thread when background processing completes.
- Non-M1 Hermes commands still return explicit not-implemented status.

`StubHermesEngine` remains in the codebase for tests and for explicit placeholder behavior.

## No HTTP/localhost architecture

DAWHermes is intended to embed Hermes directly, not orchestrate it as a separate localhost service. This avoids dual-process UX, fragile transfer protocols, and duplicated project state.

## One DAW-level AI assistant

AI orchestration is a DAW-level concern. Hermes remains a processing subsystem and does not become a separate AI endpoint.

## Native install and launch model

The install script deploys a runnable Release app to `%LOCALAPPDATA%\DAWHermes\app` and creates normal Desktop and Start Menu shortcuts that point directly to `DAWHermes.exe`.

No PowerShell or terminal launcher is used for normal user launch.

## Current limitations (Milestone 1)

Implemented now:

- workspace shell and command surfaces;
- file-backed audio source assignment and minimal MIDI note storage in tracks;
- extracted top-row-3-columns + full-width-bottom MIDI layout geometry;
- draggable splitter geometry with persisted layout state and reset command;
- embedded Hermes drums extraction and in-memory MIDI insertion;
- grouped multitrack insertion as a real hierarchy (group track + child MIDI tracks);
- enabled-empty-layer aware insertion for drums extraction;
- transactional Hermes result insertion with rollback on validation failure;
- single-operation Undo/Redo that removes/restores Hermes-generated tracks/groups without re-running embedded analysis;
- Hermes option dialogs and validation;
- Composer Assistant safe connector boundary and settings/probe UI;
- logging and local shortcut installation workflow.

Not implemented now:

- audio playback/recording/device setup;
- MIDI import/playback/piano-roll editing;
- Hermes bass/sync/BPM workflows;
- AI APIs;
- ACE exchange;
- Cubase export.
