# DAWHermes

DAWHermes is a native Windows music-production workbench designed around embedded Hermes MIDI tools, a DAW-level AI assistant, ACE Studio exchange, and clean export for final production in Cubase.

The project is not intended to replace every Cubase mixing or mastering feature. Its focus is AI-assisted preparation, MIDI creation and correction, audio-to-MIDI workflows, and a direct visual editing environment.

## Current status

Milestone 0 establishes the Windows-native application foundation.

Currently implemented:

- native JUCE Windows application;
- basic DAW workspace shell;
- audio and MIDI track models;
- track selection and deletion;
- deliberate right-click context menus;
- Hermes menu and option-dialog shells;
- neutral Hermes engine interface;
- honest stub implementation;
- automated model and validation tests;
- Release build scripts;
- local installation and Windows shortcuts;
- application logging.

Not implemented yet:

- audio playback or recording;
- MIDI import, playback or editing;
- real Hermes processing;
- embedded Python runtime;
- VST3 hosting;
- AI model connection;
- ACE Studio exchange;
- Cubase export.

## Related projects

Reference implementations:

- `DariuszPiskorowski/DAW-create-example` - macOS DAW prototype and UI/function reference.
- `DariuszPiskorowski/midi-cleaner` - current Hermes MIDI Fidelity Engine implementation.

These projects remain separate and unchanged during Milestone 0.

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
- PowerShell;
- internet access during the first JUCE dependency configuration.

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
Hermes
├── Drums
│   ├── Make MIDI from WAV...
│   └── Drum Mapping...
├── Bass
│   └── Make / Repair MIDI from WAV...
├── Synchronize MIDI with WAV...
└── Set / Fix BPM...
```

Milestone 0 provides the interface and validation only. It does not claim to perform real Hermes processing.

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
