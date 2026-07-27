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

Current work targets Milestone 3.1 visual inspection/comparison only:

- preserve accepted Milestone 2 functionality and Hermes behaviour;
- replace timeline/MIDI placeholders with real visual components;
- provide deterministic shared viewport, ruler/grid, piano-roll rendering, and note hover diagnostics;
- provide compare mode for two selected MIDI tracks in the View menu;
- keep workflow read-only (no MIDI note editing or playback changes in this milestone).

Milestone 3.1 visual functionality is accepted.
Milestone 3.2 remains the next stage for actual MIDI editing workflows.
