# DAWHermes Milestones

## Ordered plan

1. M0 - Windows foundation and local launcher.
2. M1 - Layout structure parity, file-backed audio tracks, embedded Hermes drums extraction, direct in-memory MIDI insertion, and Composer Assistant connector boundary/settings.
3. M1.1 - Resizable/persistent panel layout, reset-layout command, non-blocking Hermes drums processing, grouped hierarchy insertion, and enabled-empty-layer handling.
4. M2 - Real bass repair and MIDI/WAV synchronization (with explicit File -> Import MIDI as Track entry point, pair-selection validation, and real-assets verification).
5. M3 - Timeline/piano-roll milestone:
	- M3.1 visual timeline + piano roll + MIDI comparison (no editing);
	- M3.2 MIDI editing workflow;
	- M3.3 selected-track MIDI audition playback;
	- M3.4 selected audio-stem playback synchronized with MIDI audition.
6. M4 - Audio engine and device configuration.
7. M5 - VST3 hosting.
8. M6 - DAW-level AI assistant.
9. M7 - ACE Studio file exchange.
10. M8 - Cubase export.

No network-delivery milestone is defined in this plan.

## Current milestone scope

Core Milestone 3.4 WAV/MIDI playback is manually accepted. Its transport
completion is implemented for installed native acceptance:

- preserve accepted Milestones 2, 3.1, 3.2, and 3.3;
- import one or more WAV files from File -> Import Audio as Track..., creating
  named tracks, waveform sources, stored metadata, selection, and one batch
  Undo/Redo transaction;
- `Play` auditions one primary selected MIDI track plus all selected
  readable imported WAV tracks;
- preserve MIDI-only playback and allow audio-only playback;
- preload immutable mono/stereo WAV data and safely handle 44.1/48 kHz
  source/device-rate differences;
- share transport time zero and the Timeline/Piano Roll playhead across MIDI and audio;
- provide Pause/resume, a current/total counter, clamped 5-second seeking, and
  threshold-based shared playhead follow;
- resolve BPM from explicit MIDI tempo metadata, then cached asynchronous WAV
  detection, then a 120 BPM fallback;
- apply Stop, Panic, and Master Volume to both sources;
- skip missing/unreadable audio without modal errors;
- keep WAV at original speed with no stretching, warping, mixer, recording,
  or audio editing.

Milestone 3.3 MIDI audition playback is complete:

- preserve accepted Milestone 2, 3.1, and 3.2 functionality;
- audition the primary selected non-empty MIDI track from an immutable edited-note snapshot;
- provide Play, Stop, Panic, safe volume, and Timeline/Piano Roll playheads;
- use an internal simple synth and the default system audio output;
- keep WAV/audio-track playback, recording, Timeline editing, VST hosting, mixing, and Cubase/Reaper sync out of scope.

Milestone 3.1 visual functionality is accepted.
Milestone 3.2 is accepted and published.
Milestone 3.3 is manually accepted and published.
Milestone 3.4 core WAV/MIDI playback is manually accepted. Transport completion
awaits the user's final installed native acceptance.
