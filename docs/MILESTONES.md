# DAWHermes Milestones

## Ordered plan

1. M0 - Windows foundation and local launcher.
2. M1 - Layout structure parity, file-backed audio tracks, embedded Hermes drums extraction, direct in-memory MIDI insertion, and Composer Assistant connector boundary/settings.
3. M1.1 - Resizable/persistent panel layout, reset-layout command, non-blocking Hermes drums processing, grouped hierarchy insertion, and enabled-empty-layer handling.
4. M2 - Real bass repair and MIDI/WAV synchronization (with explicit File -> Import MIDI as Track entry point, pair-selection validation, and real-assets verification).
5. M3 - MIDI import, timeline and piano roll editing.
6. M4 - Audio engine and device configuration.
7. M5 - VST3 hosting.
8. M6 - DAW-level AI assistant.
9. M7 - ACE Studio file exchange.
10. M8 - Cubase export.

No network-delivery milestone is defined in this plan.

## Current active scope

Current work targets Milestone 2 correction/completion only:

- preserve accepted Milestone 1.1 behavior;
- keep drums extraction stable while enabling real bass repair and real sync;
- ensure installed GUI exposes File -> Import MIDI as Track...;
- verify workflows with opt-in real-asset checks (`scripts/test-m2-real-assets.ps1`).
