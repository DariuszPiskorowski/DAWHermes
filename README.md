# DAWHermes

DAWHermes is a native Windows music-production workbench designed around embedded Hermes MIDI tools, a DAW-level AI assistant, ACE Studio exchange, and clean export for final production in Cubase.

The project is not intended to replace every Cubase mixing or mastering feature. Its focus is AI-assisted preparation, MIDI creation and correction, audio-to-MIDI workflows, synchronized audio/MIDI inspection, and a direct visual editing environment.

## User manual

The standalone beginner manual is available at:

**[DAWHermes User Manual](manual/README.md)**

The manual is written in English for musicians rather than developers. It explains what each current feature is for, when to use it, how to operate it, what to expect, and which future workflows are not implemented yet.

## Current status

The stable application is complete, published and manually accepted through **Milestone 4.1**.

Current accepted milestones include:

- M1.1: persistent resizable workspace, embedded Hermes background processing and grouped drum results;
- M2: real Hermes bass repair and MIDI/WAV synchronization;
- M3.1: Timeline, waveform, Piano Roll and MIDI comparison views;
- M3.2: MIDI note editing and selected-track MIDI export;
- M3.3: functional MIDI audition;
- M3.4: synchronized MIDI/WAV playback, transport and WAV BPM analysis;
- M4.1: central audio-device configuration, whole-project playback, live hierarchical Mute/Solo and Timeline Loop work region.

Currently implemented:

- native JUCE Windows application;
- workspace layout with top three-column work area plus full-width bottom MIDI panel;
- draggable and persistent workspace separators with **View -> Reset Panel Layout**;
- **File -> Import Audio as Track...** for one or more in-place WAV sources;
- **File -> Import MIDI as Track...** with note-bearing-track selection;
- **File -> Export Selected MIDI Track...** for the selected non-empty MIDI track;
- Timeline ruler, beat grid, aligned track lanes and static WAV waveform thumbnails;
- Piano Roll note visualization and editing with stable note IDs;
- note selection, marquee selection, creation, deletion, movement, resize and keyboard nudging;
- Snap, velocity editing and quantization;
- read-only comparison of exactly two selected MIDI tracks;
- one application-lifetime JUCE audio-device service with saved settings and safe fallback;
- **Audio Settings...**, **Test Output**, **Restart Audio Device** and **Audio Device Status...**;
- synchronized whole-project MIDI and imported-WAV playback independent of selection;
- Play, Pause/resume, Stop, five-second seeking, Master Volume, BPM, counter and shared Timeline/Piano Roll playheads;
- live hierarchical track/group Mute and Solo;
- visible beat-coordinate Timeline Loop creation, resize, move, clear and callback-level wrapping;
- Unicode-safe Windows WAV paths;
- bounded WAV BPM estimation with half-time/double-time candidate handling and a 128-entry session cache;
- 512 MiB aggregate decoded-audio limit per immutable playback snapshot;
- Hermes menu, options, validation and embedded processing;
- real Hermes workflow: **Drums -> Make MIDI from WAV...**;
- real Hermes workflow: **Bass -> Repair MIDI against WAV...**;
- real Hermes workflow: **Synchronize MIDI with WAV...**;
- direct insertion of Hermes MIDI results into the DAWHermes project;
- grouped multitrack drum output and enabled-empty-layer handling;
- Undo/Redo for MIDI edits, imported audio batches and stored Hermes results;
- Composer Assistant compatibility connector settings and explicit manual reachability probe;
- Release build scripts, local installation, Windows shortcuts and application logging.

Not implemented yet:

- recording, input monitoring or track arming;
- mixer faders, pan, effects, sends or buses;
- Hermes Set / Fix BPM processing;
- VST3 hosting;
- full Composer Assistant music generation and MIDI insertion;
- project save/load format;
- audio clip movement, cutting, time stretching or beat warping;
- ACE Studio exchange;
- Cubase-specific exchange/export;
- full-project audio rendering or stem export.

Known limitations:

- the internal MIDI synth is deliberately simple and intended only for pitch, timing, duration and velocity audition;
- the current Timeline and Piano Roll styling is functional rather than final;
- audio tracks currently reference WAV files at their original paths and begin at project time zero;
- MIDI export is selected-track only;
- DAWHermes is not yet a final production mixer or mastering environment.

## Related projects

Reference implementations:

- `DariuszPiskorowski/DAW-create-example` - macOS DAW prototype and UI/function reference.
- `DariuszPiskorowski/midi-cleaner` - Hermes MIDI Fidelity Engine implementation used by embedded processing.

These repositories remain separate and are read-only references during DAWHermes work unless explicitly assigned otherwise.

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
- CPython 3.11 for the embedded Hermes build/runtime;
- PowerShell;
- internet access during the first JUCE dependency configuration.

For embedded Hermes processing, provide a local `midi-cleaner` clone by either:

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

For a bounded diagnostic Release build with explicit single-job project and compiler parallelism plus persistent resource, text, transcript and MSBuild binary logs under the ignored `build\diagnostics` directory:

```powershell
.\scripts\build-release.ps1 -Diagnostic -ParallelJobs 1
```

Diagnostic mode builds only the `DAWHermes` Release target. It does not run tests, install files or launch the application.

The `DAWHERMES_ENABLE_LTCG` CMake option defaults to `OFF`. The supported local scripts explicitly configure `OFF`, and `build-release.ps1` refuses to build unless the effective cache value is `DAWHERMES_ENABLE_LTCG=OFF`. This prevents the normal local build and install workflow from starting the `/GL`/`/LTCG` path that repeatedly froze Windows.

After an already verified Release build, `install-local.ps1 -SkipBuild` installs that artifact without initiating another build and still verifies the no-LTCG cache setting. `-BuildDirectory` can select a fresh verified tree instead of reusing an older local Release tree.

To use a fresh isolated no-LTCG diagnostic tree:

```powershell
.\scripts\configure.ps1 `
    -BuildDirectory build\diagnostic-variants\no-ltcg `
    -EnableLtcg OFF

.\scripts\build-release.ps1 `
    -Diagnostic `
    -ParallelJobs 1 `
    -BuildDirectory build\diagnostic-variants\no-ltcg
```

The no-LTCG configuration omits JUCE's recommended LTO flags, disables Release interprocedural optimization, compiles participating DAWHermes targets with `/GL-`, and links the final executable with `/LTCG:OFF`. Generated projects, outputs and logs remain under the ignored `build` directory.

Never run a local `/GL` or `/LTCG` build. LTCG experiments are allowed only in a dedicated future CI diagnostic workflow explicitly requested by the user.

Run tests:

```powershell
.\scripts\test.ps1
```

The standard test script skips the environment-dependent embedded Hermes integration test while running the deterministic suite. Run that integration explicitly only in a prepared Hermes environment:

```powershell
.\scripts\test.ps1 -IncludeEmbeddedHermesIntegration
```

Run opt-in Milestone 2 real-assets verification:

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

Current command structure:

```text
File
├── Import Audio as Track...
└── Import MIDI as Track...

Hermes
├── Drums
│   ├── Make MIDI from WAV...
│   └── Drum Mapping...
├── Bass
│   └── Repair MIDI against WAV...
├── Synchronize MIDI with WAV...
└── Set / Fix BPM...             [not integrated yet]
```

Command availability for bass repair and synchronization requires a selected audio/MIDI pair, non-empty MIDI notes and an existing audio source file.

The basic audio workflow is:

```text
File
-> Import Audio as Track...
-> select one or more WAV files
-> tracks and waveforms appear automatically
-> Play
```

Play auditions the complete playable project. Selection chooses editing and processing targets. Use row `M`/`S` controls to change live audibility and drag the Timeline ruler to define a range for the transport `Loop` toggle.

Each valid WAV remains referenced at its original absolute path. A multi-file import creates one track per valid file and is one Undo/Redo transaction. Unreadable files are skipped with one aggregate status; source files are never copied, converted or modified.

Composer Assistant connector behaviour currently remains conservative:

- disabled by default;
- configurable host, port and timeout;
- no startup connection attempt;
- explicit manual reachability probe only;
- no music generation or project insertion yet.

## Architecture

The application is divided into:

- `app` - lifecycle and application wiring;
- `core` - project and track models;
- `audio` - central device ownership, immutable project playback preparation, WAV import/BPM analysis, live routing and callback transport rendering;
- `midi` - testable selected-track MIDI export;
- `ui` - JUCE desktop interface;
- `hermes` - neutral integration contracts and embedded engine implementation;
- `tests` - automated non-GUI verification;
- `scripts` - build, test and local installation.

Hermes is embedded as a DAWHermes processing subsystem. Composer Assistant remains the single planned DAW-level AI system.

## Licensing

No project licence has been selected yet.

JUCE and all other third-party dependencies retain their own licensing terms. Do not assume that the absence of a project licence changes third-party obligations.

## Development rules

Read `AGENTS.md` before making changes.
