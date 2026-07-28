# Milestone 3.4: Audio Stem Playback with Synchronized MIDI Audition

## Purpose and Status

Core Milestone 3.4 WAV/MIDI playback has been manually accepted. The completed
transport controls are implemented for a new installed-app acceptance pass.

It allows an assigned WAV stem to be heard with the current edited MIDI state so
Hermes repair and synchronization results can be judged against the source audio.
Milestone 3.3 MIDI-only audition remains supported.

## Play Policy

`Play` captures an immutable playback snapshot:

- one primary selected non-empty MIDI track plays through the internal synth;
- every selected audio track with an assigned readable WAV plays with it;
- audio-only and MIDI-only selections are playable;
- group tracks, empty MIDI tracks, and audio tracks without an assigned source are ignored;
- missing or unreadable assigned WAV files are skipped with a non-modal status;
- non-selected tracks do not play.

The first selected playable MIDI track is the primary MIDI track. When multiple
MIDI tracks are selected, only that primary track is captured. This matches
comparison mode, where the first selected MIDI track is edited and the second is
the visual ghost. The Piano Roll comparison ghost never adds notes to playback.

The intended review workflow is to Ctrl-select an edited/repaired MIDI track and
its corresponding audio track, then press `Play`.

## Transport Controls

The compact transport strip provides:

- `<<` and `>>` to seek backward or forward by 15 seconds, clamped to the
  selected material's duration;
- `Play` to start the current selection from project time zero;
- `Pause` to preserve the current position and immutable snapshot;
- `Play` after Pause to resume that same snapshot from the preserved position;
- `Stop` to silence playback and reset the position to zero;
- `Panic` to silence playback immediately and clear the prepared snapshot;
- a current/total time counter;
- a BPM readout and Master Volume control.

Seeking while playing continues playback from the new position. Seeking while
paused preserves the paused state. Transport actions do not create
`ProjectHistory` entries.

## Shared Clock, Counter, and Playhead

MIDI events and WAV frames use one transport clock starting at project time zero.
Timeline and Piano Roll display the same playhead derived from that clock.

The counter displays current milliseconds with a compact total duration. The
Timeline follows active playback only after the playhead crosses 80% of the
visible viewport, repositioning it near 72% without changing zoom. Explicit
seeks also make the new playhead position visible. The Piano Roll shares the
same horizontal viewport and therefore follows the same playhead.

Stop resets and hides the active playhead. Natural completion leaves it at the
selection end.

## Tempo Resolution and WAV Analysis

The displayed and playback MIDI tempo uses this deterministic priority:

1. an explicit tempo event imported from the primary selected MIDI track;
2. a confidently detected tempo from a selected readable WAV;
3. the fixed audition fallback of 130 BPM.

Imported MIDI files record whether their source actually contained tempo meta
events, so a parser fallback is never mistaken for an explicit source tempo.
Tempo changes in an explicit MIDI tempo map remain effective at their event
positions.

WAV tempo analysis:

- runs on a background thread and never in the audio callback;
- reads only a bounded leading portion of the source;
- uses an onset-energy envelope and autocorrelation over the audition BPM range;
- rejects silence, steady low-energy material, and low-confidence estimates;
- caches results for the app session by path, size, and modification time;
- invalidates the cached result when the source fingerprint changes.

Detection changes only the BPM readout and beat/time mapping. WAV samples always
play at their original speed; there is no time stretching or beat warping.

## WAV Playback

- WAV data is decoded and copied into the immutable snapshot before playback.
- Mono WAV is duplicated to left and right output; stereo WAV preserves both channels.
- Source sample rates such as 44.1 kHz and 48 kHz are supported.
- Linear interpolation maps shared transport time to source frames when the
  output-device rate differs.
- WAV files play at their original speed.
- No source file is modified, rewritten, or reread by the audio callback.

Master Volume is applied to the combined internal synth and WAV mix.
Stop, Panic, device stop, and application shutdown silence both sources.

## Threading and Safety

M3.4 reuses the single M3.3 `AudioDeviceManager` and callback.

- The callback consumes immutable MIDI events and preloaded audio samples.
- Pause/resume and seek publish precomputed playback cursors; the callback does
  not rescan project data.
- Large retired sample buffers are reclaimed by the UI thread, not the audio callback.
- It performs no file I/O, Hermes work, Python work, project mutation,
  selection mutation, or history mutation.
- Playback, Stop, Panic, and Volume do not create `ProjectHistory` entries.
- Missing and unreadable WAV files are skipped without modal dialogs.

## Timing Boundary

This is audition-grade synchronization:

- WAV time zero equals project time zero;
- explicit MIDI tempo metadata has priority over WAV detection;
- inconclusive analysis falls back to 130 BPM;
- WAV speed never follows MIDI tempo;
- there is no time stretching, beat warping, looping, ruler scrubbing, or claim
  of final sample-accurate DAW mixing.

It is reliable enough to judge whether edited MIDI follows the original stem,
but it is not a production mix engine.

## Out of Scope

M3.4 does not add VST hosting, recording, a metronome, looping, mute/solo,
mixer channels, effects, fades, crossfades, audio editing, time stretching,
Cubase/Reaper synchronization, direct WAV import, or visual redesign.

WAV sources continue to enter the project only through the established
file-assignment and Hermes workflows. The transport remains audition-grade,
not a production mix engine.
