# AGENTS.md

Rules for AI coding agents working in `DAWHermes`.

## Product direction

DAWHermes is a native Windows DAW workbench with embedded Hermes MIDI tools, one DAW-level AI assistant, ACE Studio file exchange, and export for Cubase.

Cubase is a final production destination. Do not make Cubase-specific automation a core architectural dependency.

## Current milestone

The current scope is Milestone 0: repository setup, Windows-native JUCE application shell, project/track model, deliberate context menus, Hermes option-dialog shells, tests, and local shortcut-based installation.

Do not implement later milestones unless the user explicitly changes the scope.

## Required architecture

- Use C++20, JUCE and CMake.
- Target Windows x64 first.
- Keep `app`, `core`, `ui`, and `hermes` responsibilities separated.
- The UI must depend on a neutral Hermes interface.
- Hermes must ultimately be embedded in DAWHermes.
- Do not design Hermes as an HTTP service, localhost server, manually launched helper application, or user-facing second program.
- Do not require MIDI or WAV file transfer between the DAW model, Hermes and the AI assistant.
- Do not put AI inside Hermes. There will be one AI assistant at DAW level.
- Heavy future Hermes work must never run inside an audio callback.
- Do not invent successful processing results when an engine is not implemented.

## UI behaviour

- Selecting a track must only select it.
- Never open dialogs, menus or tools automatically on track selection.
- Track context menus open only after deliberate right-click.
- Hermes dialogs open only after the user deliberately chooses a Hermes command.
- Avoid intrusive or repeated popups.
- Preserve keyboard and mouse usability expected from a desktop DAW.
- Test the application in a normal native Windows window.
- Do not substitute a browser, VS Code webview, terminal-only test, screenshot mockup or embedded preview for native-window testing.

## Normal application launch

- Normal use must never require Codex, VS Code, Visual Studio or an open terminal.
- Maintain a Release build and local installation workflow.
- Windows shortcuts must point directly to the installed GUI executable.
- Do not point user shortcuts into temporary or build directories.
- Do not make PowerShell or Command Prompt the visible launcher.
- Keep logs and settings under the user profile, not beside the executable.

## Reference repositories

The sibling repositories `DAW-create-example` and `midi-cleaner` are read-only references unless the user explicitly assigns work in those repositories.

Never modify them as a side effect of DAWHermes work.

Respect every `AGENTS.md` found in any repository before editing that repository.

## Dependencies and licensing

- Pin important dependencies to explicit versions or immutable commits.
- Do not track moving dependency branches.
- Do not add dependencies without explaining why they are necessary.
- Do not copy third-party code without preserving its licence obligations.
- Do not add a repository licence on the owner's behalf without an explicit licensing decision.
- Document JUCE and other third-party licensing separately.

## Git safety

- Never discard unrelated user changes.
- Never use destructive reset, clean, checkout or force-push commands without explicit permission.
- Do not commit build output, generated projects, logs, user files, secrets or API keys.
- Keep commits focused.
- Use `main` as the default branch.
- Run the required build and tests before committing or pushing.
- Do not push when required tests fail.
- Confirm the final working tree state after the push.

## Verification

For every behaviour change:

1. Build the affected targets.
2. Run automated tests.
3. Launch the native Release application when UI changed.
4. Verify the behaviour in a normal Windows desktop window.
5. Report exact commands, results, commit SHA and remaining limitations.

## Communication

- Make reasonable, grounded decisions without repeatedly asking the user.
- Do not claim a feature works unless it was tested.
- Clearly distinguish implemented behaviour from placeholders and future work.
- Keep reports factual and include the root cause when fixing defects.
