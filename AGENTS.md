# AGENTS.md

Operating rules for AI coding agents working in `DAWHermes`.

This file is the standing contract for future Codex/Copilot/assistant work. Prompts should contain milestone-specific requirements only; general workflow, safety and verification rules belong here.

## Product direction

DAWHermes is a native Windows DAW workbench for the user's music-production workflow.

Primary direction:

- Hermes MIDI/audio cleanup and synchronization are embedded in DAWHermes.
- DAWHermes has one DAW-level AI assistant.
- Reaper is only a quick audition/reference environment.
- Cubase is the final production and mix destination.
- Cubase-specific exchange is important, but it must not become the core architectural dependency.
- ACE Studio exchange is planned as file/workflow integration, not as a hidden hard dependency.

Do not redesign the project around external localhost services, browser automation, temporary scripts or manual helper applications.

## Current accepted baseline

`main` is the accepted stable line.

As of the latest accepted baseline, DAWHermes has:

- imported MIDI visualization in Timeline and Piano Roll;
- MIDI comparison overlay;
- editable MIDI notes in Piano Roll;
- stable note IDs;
- note selection and marquee selection;
- note creation, deletion, movement and resize;
- Snap, velocity editing and quantize;
- ProjectHistory Undo/Redo;
- selected-track MIDI export;
- basic internal MIDI audition playback with Play/Stop/Panic/Volume;
- shared Timeline/Piano Roll playhead for MIDI audition.

The internal synth is intentionally basic and functional. It is for auditioning pitch, timing, duration and velocity, not final sound quality.

The next milestone should be chosen explicitly by the user. Do not assume or start another milestone without a user request.

## Mandatory first action

Before inspecting source files or making any repository change, every agent must read this file.

Required first command in local work:

```powershell
Set-Location "C:\Users\godhimself2u\source\repos\DAWHermes"
Get-Content .\AGENTS.md
```

The final report must explicitly confirm that `AGENTS.md` was read before code changes and that its constraints were followed.

Respect every `AGENTS.md` found in any repository before editing that repository.

## Milestone workflow

Work in coherent, end-to-end milestones.

For a normal milestone:

1. Start from clean, up-to-date `main`.
2. Create a local milestone branch named `wip/<milestone-topic>`.
3. Implement the complete milestone from A to Z.
4. Add deterministic automated tests.
5. Run the required build/test/install checks.
6. Install the app for manual user acceptance.
7. Do not push and do not merge until the user manually accepts the installed workflow.
8. After acceptance, squash the milestone branch into one clean commit on `main`.
9. Push only `main`.
10. Leave the local WIP branch in place unless the user asks to delete it.

Large milestones may contain local WIP checkpoint commits on the WIP branch, especially before risky refactors, long-running agent sessions, or possible rate limits. These checkpoint commits must stay local and must not be pushed directly to `main`.

Prompts should not split one coherent milestone into many unrelated half-steps unless the user explicitly chooses that because of tool limits. The agent should continue through implementation, tests, installation and report without repeatedly asking for permission.

## Git safety

- Never discard unrelated user changes.
- Never use destructive `reset`, `clean`, forced checkout, branch deletion or force-push without explicit user permission.
- Do not rewrite published history.
- Do not push WIP branches unless the user explicitly asks.
- Do not commit or push when required tests fail.
- Do not silently continue from a dirty or ambiguous working tree.
- Use `git fetch origin --prune` and `git pull --ff-only origin main` for synchronization.
- Use `main` as the default branch and stable published line.
- Keep commits focused and factual.
- Prefer one squash commit per accepted milestone on `main`.
- Confirm local `main` and `origin/main` match after push.

Before final commit/push, verify no generated or personal files are included.

Never commit:

- build output;
- generated CMake/Visual Studio projects;
- installed executables;
- logs;
- caches;
- screenshots;
- temporary reports;
- exported MIDI/audio files;
- user settings;
- API keys or secrets;
- VS Code workspace storage;
- Copilot memory files;
- files from Downloads;
- real user music assets.

## User asset safety

The user's real validation assets live outside the repository and are read-only unless explicitly stated otherwise.

Known real assets include:

```text
C:\Users\godhimself2u\Downloads\Halas w rifcie Stems\Bass.mid
C:\Users\godhimself2u\Downloads\Halas w rifcie Stems\Bass.wav
C:\Users\godhimself2u\Downloads\Halas w rifcie Stems\Drum.mid
C:\Users\godhimself2u\Downloads\Halas w rifcie Stems\Drum.wav
C:\Users\godhimself2u\Downloads\Halas w rifcie Stems\Synth.mid
C:\Users\godhimself2u\Downloads\Halas w rifcie Stems\Synth.wav
```

Use these only for manual or read-only installed-app validation. Do not hardcode these paths into production code. Do not modify, overwrite, move, rename or commit these files. Tests that need files must create temporary synthetic test data in isolated temp folders.

## Required architecture

- Use C++20, JUCE and CMake.
- Target Windows x64 first.
- Keep `app`, `core`, `ui`, `hermes`, `midi` and `audio` responsibilities separated.
- The UI must depend on neutral project/application interfaces, not directly on hidden external processes.
- Hermes must ultimately be embedded in DAWHermes.
- Do not design Hermes as an HTTP service, localhost server, manually launched helper application or user-facing second program.
- Do not require MIDI or WAV file transfer between the DAW model, Hermes and the AI assistant as a normal user workflow.
- Do not put AI inside Hermes. There will be one AI assistant at DAW level.
- Heavy Hermes or AI work must never run inside an audio callback.
- Audio callbacks must not mutate `ProjectModel`, selection or `ProjectHistory`.
- Prefer immutable snapshots for playback/export style operations.
- Do not invent successful processing results when an engine is not implemented.

## UI behaviour

- Selecting a track must only select it.
- Never open dialogs, menus or tools automatically on track selection.
- Track context menus open only after deliberate right-click.
- Hermes dialogs open only after the user deliberately chooses a Hermes command.
- Avoid intrusive or repeated popups.
- Preserve keyboard and mouse usability expected from a desktop DAW.
- Test UI changes in a normal native Windows desktop window.
- Do not substitute a browser, VS Code webview, terminal-only test, screenshot mockup or embedded preview for native-window testing.
- The app must be usable through the installed executable/shortcut, not only from a developer terminal.

Accepted M3.2 behaviour:

- Arrow keys nudge selected MIDI notes; they do not scroll the Piano Roll viewport.
- A ghost track is a comparison-only read-only overlay and must not be editable.

## Normal application launch

- Normal use must never require Codex, VS Code, Visual Studio or an open terminal.
- Maintain a Release build and local installation workflow.
- Windows shortcuts must point directly to the installed GUI executable.
- Do not point user shortcuts into temporary or build directories.
- Do not make PowerShell or Command Prompt the visible launcher.
- Keep logs and settings under the user profile, not beside the executable.

Required shortcut target after installation:

```text
C:\Users\godhimself2u\AppData\Local\DAWHermes\app\DAWHermes.exe
```

## Build and verification

For every behaviour change, run the relevant full verification sequence unless the user explicitly asks for a smaller diagnostic step.

Standard local verification:

```powershell
.\scripts\configure.ps1
.\scripts\test.ps1
.\scripts\build-release.ps1
cmake --build build --config RelWithDebInfo --target DAWHermes
.\scripts\install-local.ps1
```

`cmake` must work through the normal user `Path`. If plain `cmake` fails, diagnose the active process `Path`; do not silently hide the issue by using an absolute path except as a temporary fallback.

For UI changes:

1. Build affected targets.
2. Run automated tests.
3. Build Release.
4. Install locally.
5. Launch the installed native Windows app.
6. Verify the behaviour in a normal desktop window.
7. Leave the app ready for user manual acceptance when needed.

Do not claim human/manual acceptance unless the user personally tested and accepted the installed app.

## Testing policy

- Add deterministic tests for every new core behaviour.
- Keep tests independent of physical audio hardware where possible.
- Do not rely on real user music assets in automated tests.
- Prefer synthetic temp files for MIDI/WAV round-trip tests.
- Regression tests must protect accepted Hermes, MIDI editing, export and audition workflows.
- GitHub Actions are optional and should be used only for checks that need a clean environment or would be too heavy locally.

## Reference repositories

Sibling repositories such as `DAW-create-example` and `midi-cleaner` are read-only references unless the user explicitly assigns work in those repositories.

Never modify them as a side effect of DAWHermes work.

When referencing `midi-cleaner`, preserve its production API boundaries rather than copying unrelated internals without need.

## Dependencies and licensing

- Pin important dependencies to explicit versions or immutable commits.
- Do not track moving dependency branches.
- Do not add dependencies without explaining why they are necessary.
- Do not copy third-party code without preserving licence obligations.
- Do not add a repository licence on the owner's behalf without an explicit licensing decision.
- Document JUCE and other third-party licensing separately.

## Crash, restart and rate-limit recovery

If the computer restarts, VS Code/Codex crashes, or a session hits rate limits:

1. Do not restart implementation from scratch.
2. Inspect the current branch, HEAD, status and diff.
3. Preserve recovered work.
4. If meaningful uncommitted work exists, create a local WIP checkpoint commit on the current WIP branch.
5. Do not push the checkpoint unless the user explicitly asks.
6. Continue from the checkpoint only after verifying tests/build state.

Useful recovery commands:

```powershell
git branch --show-current
git status --short --branch
git log --oneline --decorate -n 12
git diff --stat
git diff --check
git rev-parse HEAD
git rev-parse origin/main
```

## Communication and reports

- Make reasonable, grounded decisions without repeatedly asking the user.
- Do not claim a feature works unless it was tested.
- Clearly distinguish implemented behaviour from placeholders and future work.
- Keep reports factual and concise.
- Include exact commands/results for tests and builds.
- Include commit SHA, branch, push status and final working-tree status.
- Include root cause when fixing defects.
- Do not produce long essays when a short result report is enough.

Preferred final report shape:

```text
Result: <what changed>
Tests: <commands and pass/fail>
Install/native launch: <pass/fail>
Commit: <sha/message or not committed>
Push: <pushed/not pushed>
Status: <clean/dirty and branch>
Limitations: <only important accepted limitations>
```
