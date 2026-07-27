# DAWHermes Milestones

## Ordered plan

1. M0 - Windows foundation and local launcher.
2. M1 - Layout structure parity, file-backed audio tracks, embedded Hermes drums extraction, direct in-memory MIDI insertion, and Composer Assistant connector boundary/settings.
3. M1.1 - Resizable/persistent panel layout, reset-layout command, non-blocking Hermes drums processing, grouped hierarchy insertion, and enabled-empty-layer handling.
4. M2 - Real bass repair and MIDI/WAV synchronization (with explicit File -> Import MIDI as Track entry point, pair-selection validation, and real-assets verification).
5. M3 - Timeline/piano-roll milestone:
	- M3.1 visual timeline + piano roll + MIDI comparison (no editing);
	- M3.2 MIDI editing workflow.
6. M4 - Audio engine and device configuration.
7. M5 - VST3 hosting.
8. M6 - DAW-level AI assistant.
9. M7 - ACE Studio file exchange.
10. M8 - Cubase export.

No network-delivery milestone is defined in this plan.

## Current active scope

Current WIP work targets Milestone 3.2 MIDI editing and selected-track MIDI export:

- preserve accepted Milestone 2 and Milestone 3.1 functionality;
- edit MIDI notes directly in the in-memory project model;
- provide note selection, marquee selection, creation, deletion, movement, duration resize, keyboard nudging, Snap, velocity editing, and quantize selected notes;
- export the selected non-empty MIDI track to a standard MIDI file from the edited in-memory state;
- keep playback, Timeline editing, controller lanes, copy/paste, VST hosting, and Cubase-specific exchange out of scope.

Milestone 3.1 visual functionality is accepted.
Milestone 3.2 is implemented locally on the WIP branch and still awaits user manual acceptance.
