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
6. M4 - Audio engine and device configuration:
	- M4.1 central device configuration, whole-project playback, Mute/Solo,
	  and Timeline Loop.
7. M5 - VST3 hosting:
	- M5.1 per-MIDI-track VST3 instrument hosting.
8. M6 - DAW-level AI assistant.
9. M7 - ACE Studio file exchange.
10. M8 - Cubase export.

No network-delivery milestone is defined in this plan.

## Infrastructure milestones

### Infrastructure CI.1 - GitHub-hosted Windows x64 Release verification and downloadable artifact

Status: **CI.1 implemented and awaiting manual workflow/artifact acceptance.**

- one clean GitHub-hosted `windows-2022` x64 Release build tree;
- exact Python 3.11 x64, Visual Studio 2022, JUCE 8.0.13 and pybind11 v3.0.4;
- complete deterministic Release suite followed by the incremental Release
  application build in the same tree;
- single-worker CMake, MSBuild and compiler execution;
- enforced `DAWHERMES_ENABLE_LTCG=OFF`, `/GL-`, normal Release optimization and
  `/LTCG:OFF` verification;
- deterministic AMD64 PE-header verification;
- hash-verified downloadable runtime and separate diagnostic-log artifacts;
- installation of the downloaded artifact without local compilation;
- retained installed native smoke and user manual validation on the real music
  computer;
- no product feature, project persistence, commercial VST3, licence, private
  endpoint or real user music asset added to hosted CI.

## Current milestone scope

Milestone 5.1 is complete and manually accepted:

- JUCE 8.0.13 VST3 host support only, with VST2, AU, AAX and other host
  formats explicitly disabled;
- cached instrument-only catalog in the normal user settings area, deliberate
  scan/rescan, stable identifiers, deterministic ordering, filtering,
  cancellation between candidates and dead-man crash recovery;
- one Internal Audition Synth or one independent VST3 instance per MIDI track;
- asynchronous transactional assignment, one editor window per assigned
  track, and safe fallback when an instance cannot be prepared;
- per-track channel/velocity/note routing integrated into the single M4.1
  device callback without doubling the internal synth;
- Mute/Solo, Loop, Pause, Stop, seek, device restart, master volume and
  whole-project MIDI/WAV synchronization retained;
- safe per-track Internal Audition Synth fallback when a runtime cannot
  reprepare for a changed audio device;
- JUCE plugin playhead delivery and bounded latency compensation across VST3,
  internal-synth and WAV sources;
- deterministic fake-processor coverage without requiring installed plugins
  or physical audio hardware.

Milestone 4.1 is complete and manually accepted:

- one application-lifetime audio-device manager and project callback;
- explicit Audio settings, test output, restart, and status commands;
- complete-project immutable MIDI/WAV playback independent of selection;
- live hierarchical Mute/Solo routing;
- beat-coordinate Timeline loop editing and callback-boundary wrapping;
- safe saved-device restore/default fallback/no-device behavior;
- preserved 512 MiB decode budget, Unicode paths, BPM safeguards, and all
  accepted M0-M3.4 behavior.

Milestone 3.4 is complete and manually accepted:

- preserve accepted Milestones 2, 3.1, 3.2, and 3.3;
- import one or more WAV files from File -> Import Audio as Track..., creating
  named tracks, waveform sources, stored metadata, selection, and one batch
  Undo/Redo transaction;
- `Play` auditions one primary selected MIDI track plus all selected
  readable imported WAV tracks;
- preserve MIDI-only playback and allow audio-only playback;
- preload immutable mono/stereo WAV data and safely handle 44.1/48 kHz
  source/device-rate differences within a 512 MiB aggregate snapshot budget;
- share transport time zero and the Timeline/Piano Roll playhead across MIDI and audio;
- provide Pause/resume, a current/total counter, clamped 5-second seeking, and
  threshold-based shared playhead follow;
- resolve BPM from explicit MIDI tempo metadata, then cached asynchronous WAV
  detection with a 128-entry LRU session bound, then a 120 BPM fallback;
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
Milestone 3.4 is complete and manually accepted.
Milestone 4.1 is complete and manually accepted.
