# Milestone 4.1: Audio Engine, Device Configuration and Project Playback

Status: complete and manually accepted.

## User workflow

`Play` now auditions the complete project. Track selection remains an editing
concern and does not determine audibility. Every non-empty MIDI track and every
readable imported mono/stereo WAV participates in deterministic project order.
Group tracks, empty MIDI/generated layers, comparison ghosts, and missing or
unreadable WAV sources produce no audio.

The transport remains `<<`, `Play`, `Pause`, `Stop`, `>>`, `Loop`, counter,
`BPM`, `Panic`, and Master Volume. Five-second seeking, the shared
Timeline/Piano Roll playhead, viewport follow, paused-position preservation,
and stopped playhead hiding remain intact.

## Audio menu

The explicit top-level `Audio` menu contains:

- `Audio Settings...`
- `Test Output`
- `Restart Audio Device`
- `Audio Device Status...`

`Audio Settings...` opens JUCE's native device selector. It exposes only device
systems reported by the compiled JUCE build and supports output/input device,
sample rate, buffer size, and active channel configuration. Inputs are
configuration-only: recording, monitoring, meters, and arming are out of scope.

Device configuration is stored in the existing application properties, outside
`ProjectHistory`. Startup first restores the saved JUCE device state, then tries
the default output exactly once. Failure is reported in the status strip and
application log without a startup popup. The application remains usable with no
device.

`Test Output` emits an approximately 0.5-second, low-gain 440 Hz tone to both
available output channels and ends automatically. It is unavailable while
project playback is playing or paused. `Restart Audio Device` safely silences
playback, attempts the selected state, and then attempts the default output
once. `Audio Device Status...` is user-invoked and reports device type, names,
sample rate, buffer, active channels, latency, and open/running/error state.

## Central ownership

`MainApplication` owns one application-lifetime `AudioDeviceService`.
`AudioDeviceService` owns the only `juce::AudioDeviceManager` and one
`MidiAuditionEngine`, registered as the single project callback. The service is
injected through `MainWindow` into `MainComponent`; UI destruction precedes
service/device destruction.

The message thread performs device open/close/restart, source inspection,
bounded WAV decoding, MIDI event preparation, loop preparation, and immutable
state publication. The callback performs no file I/O, logging, allocation,
model/history mutation, or UI work. Full playback snapshots and small
Mute/Solo/loop states are retained and reclaimed outside the callback.

## Whole-project snapshot and tempo

Play captures edited MIDI notes, source tempo metadata, decoded WAV buffers,
track identity, and the maximum duration of all playable content. Later note
edits, source replacement, and deletion take effect on the next stopped
preview/Play. Live routing is separate from immutable content.

WAV safeguards remain:

- bounded-block decoding directly into final channel storage;
- 512 MiB aggregate decoded-audio limit;
- deterministic project-order inclusion and later-track skipping;
- Unicode-safe native paths and read-only source handling;
- missing/unreadable source skipping;
- source-rate conversion at original WAV speed (no stretching/warping).

Project tempo is selected in this order:

1. first non-empty MIDI track in project order with explicit tempo metadata;
2. otherwise first confident readable WAV BPM result in project order;
3. otherwise 120 BPM.

Conflicting explicit MIDI maps use the first source and produce a concise
non-modal diagnostic. WAV BPM changes beat mapping only, never playback speed.

## Mute and Solo

Every track row, including groups, has accessible `M` and `S` buttons. They do
not change selection and do not create MIDI-edit Undo/Redo entries.

- Own mute or any ancestor mute makes a playable track inaudible.
- Mute wins over Solo.
- With no Solo, every non-muted playable track is audible.
- With any Solo, a playable track is audible only when it or an ancestor group
  is soloed and it is not muted.
- Soloing a group includes all non-muted playable descendants.
- Soloing a child does not include its siblings.
- Groups produce no audio.

Routing changes publish a small immutable set. The callback clears voices made
inaudible and reconstructs notes that should already be sounding when a MIDI
track becomes audible, without restarting or moving transport. WAV routing
resumes directly at current project time. Project duration and loop bounds do
not depend on Mute/Solo.

## Timeline loop

The Timeline ruler supports:

- click to seek;
- drag empty space to create/replace a range;
- drag either boundary to resize;
- drag inside to move while preserving length;
- right-click, then `Clear Loop Range`.

The range is stored in beat coordinates. Snap uses the selected beat-grid
resolution; unsnapped ranges use a deterministic 1/960-beat minimum. Bounds are
normalized and clamped to playable project time. The translucent range and
boundaries remain visible while Loop is off. Clearing disables Loop.

When Loop is enabled, Play begins at the current position if it is inside the
range, otherwise at loop start. The audio callback wraps at the boundary,
repositions WAV cursors, stops boundary-crossing voices, and reconstructs notes
active at loop start. Loop changes are prepared off-thread and applied from the
next callback block. Pause/resume, seek, Stop, Panic, shared playheads, and
viewport follow retain their established behavior.

## Scope limitations

M4.1 does not add recording, monitoring, arming, mixer faders/pan, effects,
sends, VST hosting, automation, audio editing, time stretching, beat warping,
crossfades, project-file persistence, Hermes Set/Fix BPM, AI connectivity, ACE
exchange, or Cubase export.

## Manual acceptance checklist

- Audio menu commands open only when invoked; settings/status are accurate.
- Test Output is safe and Restart leaves the app responsive.
- Selection does not limit Play; project MIDI and WAV play together.
- Track/group Mute and Solo follow the documented rules during playback.
- Stop, Panic, routing changes, and loop wraps leave no hanging MIDI notes.
- Ruler click seeks; drag creates, resizes, and moves a visible range.
- Loop repeats synchronized MIDI/WAV; Pause/resume and five-second seek work.
- Stop resets time, hides the playhead, and preserves viewport position.
- Device changes/loss fail safely with non-modal status and logs.
