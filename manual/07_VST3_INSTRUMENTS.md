# 7. VST3 Instruments

[Previous: MIDI Editor](06_MIDI_EDITOR.md) | [Back to contents](README.md) | [Next: Hermes Tools](08_HERMES_TOOLS.md)

## What this feature is for

Each MIDI track can use either the built-in **Internal Audition Synth** or one
installed 64-bit VST3 instrument. VST3 instruments are useful when you want to
hear the complete MIDI arrangement with familiar sounds while keeping imported
WAV tracks synchronized.

DAWHermes hosts instruments for audition. It does not bundle any third-party
plugin, copy plugin files, save a project, manage presets, render a final mix,
or host audio effects in this milestone.

## Build the instrument list

DAWHermes never scans plugins at startup.

1. Open **Plugins -> VST3 Instrument Manager...**.
2. Choose **Scan** to inspect new or changed plugins in the standard VST3
   locations.
3. Choose **Rescan All** only when you want every candidate tested again.
4. Watch the progress and current candidate name. You can choose
   **Cancel Scan** between candidates.
5. Use the filter box to narrow the cached list by instrument or manufacturer.

Playback stops before a scan begins. DAWHermes tests one candidate at a time
and keeps the previous usable catalog if a scan is cancelled or cannot be
saved. A plugin that crashes while being inspected is recorded in a recovery
file in the DAWHermes settings folder, allowing later candidates to be tested
on the next scan.

Only VST3 entries that identify themselves as instruments and provide audio
output appear in the list. Effects, MIDI-only tools, unsupported formats, and
duplicate entries are omitted.

## Assign an instrument to a MIDI track

1. Right-click the MIDI track.
2. Open **Instrument**.
3. Choose **Select VST3 Instrument...**.
4. Select one cached instrument and choose **Select**.
5. Wait for the track row to show the instrument name.

DAWHermes stops playback and prepares the new instance asynchronously. The
previous instrument remains usable unless the replacement succeeds. If no
usable VST3 runtime is available, playback safely uses the Internal Audition
Synth.

Assigning the same plugin to two MIDI tracks creates two independent instances.
Each instance receives only its own track's note events, including MIDI channel
and velocity.

To return to the built-in sound, right-click the track and choose
**Instrument -> Use Internal Audition Synth**.

## Open the instrument editor

Right-click an assigned MIDI track and choose
**Instrument -> Open Instrument Editor**.

DAWHermes opens the plugin's native editor when available. Otherwise, it opens
a generic parameter editor. A track has at most one editor window; choosing the
command again brings that window to the front. Replacing the instrument,
switching to the internal synth, deleting the track, starting a new project, or
closing DAWHermes closes the associated editor.

## Playback behaviour

VST3 instruments participate in the existing whole-project transport:

- Play and Pause/resume use the same project clock as MIDI, WAV, Timeline, and
  Piano Roll.
- Stop, seek, loop wrap, device restart, assignment changes, and shutdown clear
  or reconstruct held notes.
- Mute and Solo apply live to plugin tracks using the same hierarchy as other
  tracks.
- Master Volume controls internal synth, VST3 instruments, and imported WAV
  stems.
- Plugins receive play state, sample and second position, PPQ position, BPM,
  time signature, and loop information.
- DAWHermes compensates the internal synth, WAV stems, and lower-latency
  instruments to the largest active instrument latency within a bounded
  safeguard.

Imported WAV audio remains unchanged and plays at original speed. VST3
assignment does not alter MIDI notes, tempo maps, Hermes results, or Undo/Redo
history.

## Audio-device changes

Changing or restarting the audio device stops playback and clears held notes.
Active instruments are prepared for the new sample rate and buffer size.
Catalog browsing and scanning remain available when no audio output device is
open, but audition playback requires an output device.

## Current limitations

- VST3 instruments only; VST2, Audio Unit, AAX, effects, sends, and buses are
  not hosted.
- One instrument per MIDI track.
- No preset browser, plugin-state save, automation, freeze, bounce, or offline
  rendering.
- DAWHermes has no project save/load format yet, so track assignments do not
  return after restarting the application.
- A third-party plugin can still have its own performance, stability,
  licensing, authorization, or compatibility limits.

Next: [Hermes Tools](08_HERMES_TOOLS.md)
