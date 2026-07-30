# Milestone 5.1: VST3 Instrument Hosting

Status: M5.1 implemented and awaiting manual user acceptance.

## Ownership and lifetime

- `MainApplication` continues to own one application-lifetime
  `AudioDeviceService`.
- `AudioDeviceService` continues to own the only `AudioDeviceManager` and the
  only registered project audio callback.
- A dedicated application-lifetime VST3 host service is owned alongside the
  existing playback engine and is invoked by that existing callback. It never
  opens a second audio device.
- The project model stores only a serializable instrument assignment
  descriptor. It never owns an `AudioPluginInstance`, plugin editor, window, or
  other JUCE runtime object.
- Each MIDI track may own one independent runtime VST3 instrument instance.
  Assigning the same plugin to two tracks therefore creates two instances.
- Plugin instances and editor windows are created, replaced, and destroyed on
  the message thread. One editor window is allowed per assigned track.

## Realtime boundary

- Scanning, catalog persistence, plugin instantiation, preparation, editor
  creation, model mutation, logging, and retired-runtime destruction happen
  outside the audio callback.
- The callback consumes immutable or atomically published runtime state.
- Host-owned MIDI and audio scratch storage, latency-delay storage, and routing
  data are bounded and prepared before publication. Host code does not perform
  file I/O, logging, model/UI mutation, blocking lock acquisition, or
  host-owned container growth in the callback.
- Third-party plugin code is called only through `processBlock`; a plugin may
  have internal behaviour that DAWHermes cannot control.
- Runtime replacement is transactional: the previous playable assignment
  remains active until the replacement instance has been created and prepared.
  Playback is stopped before an assignment changes.

## MIDI and transport

- MIDI events retain their source track, channel, pitch, velocity, note-on, and
  note-off data.
- An assigned VST3 instrument receives only its track's MIDI. The internal
  audition synth receives MIDI only from tracks configured to use it, so there
  is no doubled synth output.
- Mute, Solo, Pause, Stop, seek, loop wrap, device restart, assignment changes,
  and shutdown clear or reconstruct plugin notes at the same transport
  boundaries used by the internal synth.
- Each plugin receives a JUCE `AudioPlayHead` position containing play/record
  state, sample and second positions, PPQ position, BPM, time signature, and
  active loop range where available.

## Latency compensation

- The host computes the maximum active instrument latency outside the callback.
- Lower-latency VST3 instruments, the internal audition synth, and WAV stems
  are delayed to that maximum.
- Delay lines are bounded and preallocated. A latency change triggers an
  outside-callback runtime rebuild before a new layout is published.

## Catalog and crash recovery

- Only JUCE VST3 host support is enabled. VST2, Audio Unit, AAX, and other host
  formats remain disabled.
- The cached instrument catalog and scan dead-man file live in the normal
  DAWHermes user settings area, not in the repository.
- DAWHermes does not scan at startup. Scanning is a deliberate user action from
  the VST3 Instrument Manager.
- A scan tests one candidate at a time, supports safe cancellation between
  candidates, filters non-instruments, deduplicates stable identifiers, and
  publishes a deterministic catalog only after the scan completes. The
  previously usable catalog remains intact after cancellation or failure.
- A normal scan omits candidates retained by the previous dead-man record so
  the remaining catalog can recover. `Rescan All` deliberately retries them.
  Stale recovery entries that no longer match discovered candidates are
  ignored and reported without blocking the scan.

## Failure behaviour

- Catalog browsing remains available with no audio device.
- A missing or failed persisted VST3 assignment is reported and falls back to
  the internal audition synth for that track.
- Device changes stop playback, clear notes, and reprepare active plugin
  instances for the new sample rate and block size before playback resumes.
- A runtime that fails device reprepare is removed outside the callback. The
  message thread changes that track visibly to the Internal Audition Synth;
  successfully reprepared track assignments are preserved.
- No plugin binary, preset, generated scan artifact, or user music asset is
  copied into or committed to the repository.
