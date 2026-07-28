# Milestone 3.4: Audio Stem Playback with Synchronized MIDI Audition

## Purpose and Status

Milestone 3.4 is implemented and installed for manual acceptance.

It allows an assigned WAV stem to be heard with the current edited MIDI state so
Hermes repair and synchronization results can be judged against the source audio.
Milestone 3.3 MIDI-only audition remains supported.

## Play Selection Policy

`Play Selection` captures an immutable playback snapshot:

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
its corresponding audio track, then press `Play Selection`.

## Shared Clock and Playhead

MIDI events and WAV frames use one transport clock starting at project time zero.
Timeline and Piano Roll display the same playhead derived from that clock.

MIDI beat timing uses the selected MIDI track's imported tempo map, with a
120 BPM fallback. Audio-only playback uses the project-resolved tempo map for
the visual beat position when one exists, otherwise the same 120 BPM fallback.
Stop resets and hides the playhead; natural completion leaves it at the
selection end.

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
- Large retired sample buffers are reclaimed by the UI thread, not the audio callback.
- It performs no file I/O, Hermes work, Python work, project mutation,
  selection mutation, or history mutation.
- Playback, Stop, Panic, and Volume do not create `ProjectHistory` entries.
- Missing and unreadable WAV files are skipped without modal dialogs.

## Timing Boundary

This is audition-grade synchronization:

- WAV time zero equals project time zero;
- MIDI uses its existing tempo map;
- WAV speed never follows MIDI tempo;
- there is no BPM detection, time stretching, beat warping, seeking, looping,
  ruler scrubbing, or claim of final sample-accurate DAW mixing.

It is reliable enough to judge whether edited MIDI follows the original stem,
but it is not a production mix engine.

## Out of Scope

M3.4 does not add VST hosting, recording, a metronome, looping, seeking,
mute/solo, mixer channels, effects, fades, crossfades, audio editing,
time stretching, Cubase/Reaper synchronization, or visual redesign.
