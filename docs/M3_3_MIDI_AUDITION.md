# Milestone 3.3: MIDI Audition Playback

## Purpose and Status

Milestone 3.3 is complete and manually accepted.

Its purpose is to make current edited MIDI notes audible inside DAWHermes without introducing a full DAW audio engine.

## Playback Behaviour

- Play is available for the primary selected non-empty MIDI track only.
- Play captures an immutable snapshot of the current in-memory edited notes.
- Editing during playback remains responsive and is heard after Stop/Play.
- Created, deleted, moved, resized, velocity-edited, and quantized note state is reflected in the next snapshot.
- Imported tempo maps control beat-to-second timing; missing tempo metadata falls back to 120 BPM.
- Fractional beats and note durations are supported.
- The comparison ghost overlay is never added to the played snapshot.

## Internal Synth and Audio Device

`src/audio/MidiAuditionEngine` opens the system default audio output on first playback and renders a low-gain polyphonic sine synth.

Velocity controls note amplitude, while the transport Volume slider applies a safe master gain with a default of 25%.

Stop, Panic, application shutdown, and audio-device stop silence active voices. A missing output device produces a concise status message without modal error spam.

The sound is intentionally functional. It is intended for checking pitch, rhythm, duration, and relative velocity rather than production-quality rendering.

## Transport and Playhead

The top transport strip provides:

- Play;
- Stop;
- Panic;
- Volume.

Timeline and Piano Roll show the same orange playback playhead while playback advances. Explicit Stop and Panic reset and hide the playhead; natural completion leaves it at the end position.

Transport actions and volume changes do not write `ProjectHistory`.

## Threading and Safety

- The audio callback consumes an immutable event snapshot.
- It does not mutate `ProjectModel`, selection, or `ProjectHistory`.
- Snapshot replacement and transport state use atomics.
- No Hermes, Python, file I/O, or other heavy processing runs in the audio callback.
- Playback does not read or modify source MIDI/WAV files and writes no generated audio files.

## Out of Scope

M3.3 does not add:

- VST or soundfonts;
- WAV or audio-track playback;
- recording;
- metronome or looping;
- mixer, effects, or automation;
- ASIO-specific UI;
- Cubase or Reaper synchronization;
- note preview on click/create/move.

Optional per-note preview is deferred. Milestone 3.3 manual acceptance passed using full selected-track Play.
