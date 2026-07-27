# DAWHermes

DAWHermes is a native Windows music-production workbench designed around embedded Hermes MIDI tools, a DAW-level AI assistant, ACE Studio exchange, and clean export for final production in Cubase.

The project is not intended to replace every Cubase mixing or mastering feature. Its focus is AI-assisted preparation, MIDI creation and correction, audio-to-MIDI workflows, and a direct visual editing environment.

## Current status

Milestone 3.1 extends Milestone 2 with visual timeline/piano-roll inspection and deterministic MIDI comparison.

Milestone 1.1 and Milestone 2 functional integration are complete and accepted.
Milestone 3.1 visual functionality is accepted and focuses on read-only visual analysis before editing features.

Currently implemented:

- native JUCE Windows application;
- workspace layout with top 3-column work area plus full-width bottom MIDI panel;
- draggable workspace separators (left/center, center/right, top/bottom split);
- persistent workspace panel sizes with View -> Reset Panel Layout;
- audio, MIDI, and group track models with file-backed audio source assignment;
- File -> Import MIDI as Track... with note-bearing-track selection;
- timeline ruler with bar labels and selectable beat grid (1/4, 1/8, 1/16, 1/32);
- timeline lanes aligned to track-list ordering and row geometry;
- static waveform thumbnails for audio tracks in timeline lanes (display only);
- piano keyboard + piano-roll note visualization with shared horizontal viewport;
- note hover diagnostics (pitch, velocity, start, duration, channel);
- View -> Compare Selected MIDI Tracks mode with color-coded delta legend (read-only comparison);
- track selection and deletion;
- deliberate right-click context menus;
- Hermes menu, option dialogs and validation;
- neutral Hermes engine interface with embedded Python runtime implementation;
- real Hermes workflow: Drums -> Make MIDI from WAV;
- real Hermes workflow: Bass -> Repair MIDI against WAV;
- real Hermes workflow: Synchronize MIDI with WAV;
- serialized non-blocking background execution for drums, bass repair, and MIDI/WAV sync;
- direct MIDI-note insertion into DAWHermes project model (no user-visible intermediate MIDI files);
- MIDI source-metadata retention for imported and generated tracks;
- grouped multitrack drum output inserted as a real group track with child MIDI tracks;
- enabled-empty-layer handling for drum extraction options (enabled empty layers can be created when requested);
- single-operation Undo/Redo for inserted Hermes drum MIDI tracks and groups without re-running analysis;
- successful bass/sync Hermes job-directory cleanup with failure diagnostics retained in cache;
- Composer Assistant connector boundary with safe settings and manual probe;
- automated model and validation tests;
- Release build scripts;
- local installation and Windows shortcuts;
- application logging.

Not implemented yet:

- audio playback or recording;
- MIDI note editing workflow;
- Hermes set/fix BPM workflow;
- VST3 hosting;
- AI model connection;
- ACE Studio exchange;
- Cubase export.

Known limitation:

Milestone 3.1 visual functionality is accepted.

The current Timeline and Piano Roll styling is intentionally functional rather than final.
Visual polish, spacing, colours and presentation will be revisited near the end of DAWHermes development.

Actual MIDI note editing actions (create/move/resize/delete) remain deferred to Milestone 3.2.

## Related projects

Reference implementations:

- `DariuszPiskorowski/DAW-create-example` - macOS DAW prototype and UI/function reference.
- `DariuszPiskorowski/midi-cleaner` - current Hermes MIDI Fidelity Engine implementation used by embedded drums extraction.

These projects remain separate and unchanged during Milestone 2 work.

## Technology

- C++20
- JUCE 8.0.13
- CMake 3.22+
- MSVC
- Windows x64

## Normal launch

Normal use does not require Codex, VS Code, Visual Studio, PowerShell or Command Prompt.

After local installation, launch DAWHermes from either:

- the `DAWHermes` Desktop shortcut;
- the Windows Start Menu entry named `DAWHermes`.

Both shortcuts point directly to the installed native GUI executable under:

`%LOCALAPPDATA%\DAWHermes\app`

No terminal window should appear.

## Developer setup

Required:

- Git;
- CMake 3.22 or newer;
- Visual Studio or Build Tools with Desktop development with C++;
- CPython 3.11 (required for embedded Hermes build/runtime);
- PowerShell;
- internet access during the first JUCE dependency configuration.

For embedded Hermes drums extraction, provide a local `midi-cleaner` clone by either:

- setting environment variable `DAWHERMES_HERMES_REPO` to the clone path; or
- keeping it at a discovered default path (`../midi-cleaner` from working dir, or `%USERPROFILE%\source\repos\midi-cleaner`).

Configure:

```powershell
.\scripts\configure.ps1
```

Build Release:

```powershell
.\scripts\build-release.ps1
```

Run tests:

```powershell
.\scripts\test.ps1
```

Run opt-in Milestone 2 real-assets verification (embedded Hermes path, hash and cache checks):

```powershell
.\scripts\test-m2-real-assets.ps1 `
	-BassMidi <path> -BassWav <path> `
	-DrumMidi <path> -DrumWav <path> `
	-SynthMidi <path> -SynthWav <path>
```

Install locally:

```powershell
.\scripts\install-local.ps1
```

After installation, close the terminal and use the Desktop or Start Menu shortcut for normal launches.

Uninstall the local application while preserving settings and logs:

```powershell
.\scripts\uninstall-local.ps1
```

## User data

Application logs are stored under:

`%LOCALAPPDATA%\DAWHermes\logs`

User settings are stored under the user profile and are not kept beside the executable.

## Hermes interaction design

Hermes commands are available through the application menu and deliberate track right-click context menus.

Selecting a track never opens a dialog automatically.

Initial command structure:

```text
File
└── Import MIDI as Track...

Hermes
├── Drums
│   ├── Make MIDI from WAV...
│   └── Drum Mapping...
├── Bass
│   └── Repair MIDI against WAV...
├── Synchronize MIDI with WAV...
└── Set / Fix BPM...
```

Command availability for bass/sync requires a selected audio+MIDI pair, non-empty MIDI notes, and an existing audio source file.

Composer Assistant connector defaults in Milestone 1:

- disabled by default;
- host `100.126.75.32`;
- port `3456`;
- no startup connection attempt.

## Architecture

The application is divided into:

- `app` - lifecycle and application wiring;
- `core` - project and track models;
- `ui` - JUCE desktop interface;
- `hermes` - neutral integration contracts and engine implementation;
- `tests` - automated non-GUI verification;
- `scripts` - build, test and local installation.

Hermes is intended to become an embedded part of DAWHermes, not a separate localhost service or second user-facing application.

## Licensing

No project licence has been selected yet.

JUCE and all other third-party dependencies retain their own licensing terms. Do not assume that the absence of a project licence changes third-party obligations.

## Development rules

Read `AGENTS.md` before making changes.
