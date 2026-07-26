# DAWHermes Architecture (Milestone 0)

## Why a new Windows repository

DAWHermes is a native Windows-first product. The existing `DAW-create-example` is a macOS Swift/SwiftUI prototype and remains a read-only reference for interaction ideas.

A direct Swift port would not satisfy the Windows-native tooling, deployment, and long-term embedded-Hermes direction required for this product. A dedicated repository allows clean Windows architecture and independent release management.

## Why JUCE + CMake

- JUCE provides cross-platform native desktop UI and audio/MIDI integration paths while still compiling to a normal Windows GUI executable.
- CMake enables deterministic dependency pinning and build automation without requiring Projucer.
- JUCE is pinned via FetchContent to immutable tag `8.0.13` to avoid moving-target dependency drift.

## Milestone 0 module boundaries

The codebase is split into explicit layers:

- `src/app` - application lifecycle, top-level window wiring, settings, logging bootstrap.
- `src/core` - in-memory project and track model plus selection/controller state.
- `src/ui` - JUCE views, menu bar, context menus, workspace layout, and dialog shells.
- `src/hermes` - neutral Hermes contracts, command availability rules, option validation, stub engine.
- `tests` - non-GUI behavioural and validation tests.
- `scripts` - configure/build/test/install/uninstall automation for Windows.

This separation keeps Hermes integration swappable and testable without rewriting UI code.

## Deliberate interaction behaviour

- Left click on a track only changes selection.
- Right click deliberately opens a context menu.
- Hermes dialogs are opened only from explicit user commands.
- No command popup is triggered by passive selection.

This mirrors expected desktop DAW ergonomics and avoids disruptive automation.

## Hermes integration direction

Milestone 0 uses `IHermesEngine` and `StubHermesEngine`.

- The UI calls a neutral interface.
- The stub always returns an honest `notImplemented` result.
- No fake MIDI is created.
- No success is claimed for real processing.

Future Hermes embedding will replace the stub inside the same boundary.

## No HTTP/localhost architecture

DAWHermes is intended to embed Hermes directly, not orchestrate it as a separate localhost service. This avoids dual-process UX, fragile transfer protocols, and duplicated project state.

## One DAW-level AI assistant

AI orchestration is a DAW-level concern. Hermes remains a processing subsystem and does not become a separate AI endpoint.

## Native install and launch model

The install script deploys a runnable Release app to `%LOCALAPPDATA%\DAWHermes\app` and creates normal Desktop and Start Menu shortcuts that point directly to `DAWHermes.exe`.

No PowerShell or terminal launcher is used for normal user launch.

## Current limitations (Milestone 0)

Implemented now:

- workspace shell and command surfaces;
- project/track model operations;
- Hermes option dialog shells and validation;
- stub Hermes command invocation;
- logging and local shortcut installation workflow.

Not implemented now:

- audio playback/recording/device setup;
- MIDI import/playback/piano-roll editing;
- real Hermes algorithms;
- Python embedding;
- AI APIs;
- ACE exchange;
- Cubase export.
