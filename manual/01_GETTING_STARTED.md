# 1. Getting Started

[Back to the manual contents](README.md) | [Next: Tracks, MIDI and Audio](02_TRACKS_MIDI_AND_AUDIO.md)

## What DAWHermes is

DAWHermes is a Windows music-production workbench for preparing, correcting and understanding musical material before final production.

It combines three kinds of work in one place:

- ordinary MIDI and audio preparation;
- Hermes tools that analyse relationships between WAV and MIDI;
- a planned DAW-level Composer Assistant for creating or completing MIDI inside a chosen musical passage.

DAWHermes is not intended to replace every part of Cubase. Cubase remains the destination for final instruments, detailed mixing, effects, automation and mastering. DAWHermes concentrates on the earlier stages: importing stems, recovering MIDI, correcting notes, checking timing, preparing arrangements and exporting clean MIDI.

## Audio and MIDI in simple terms

### WAV audio

A WAV file contains recorded sound. It may contain drums, bass, vocals, a synthesizer, a complete mix or any other audio material.

You can hear the exact sound stored in the file, but the file does not automatically tell the editor which musical notes were played. It may also contain no reliable tempo information.

### MIDI

MIDI does not contain recorded sound. It contains instructions such as:

- play this note;
- start it at this musical position;
- hold it for this long;
- use this velocity;
- play it on this MIDI channel.

DAWHermes uses a simple internal sound for MIDI audition. This sound is intentionally basic. It helps you hear pitch, timing and note length, but it is not a finished production instrument.

### Why both are useful together

A WAV track can be the reference that shows what was actually heard. A MIDI track is editable. DAWHermes lets you compare them, correct MIDI and use Hermes to create or improve MIDI from audio.

## Launching DAWHermes

Launch the installed application from:

- the **DAWHermes** Desktop shortcut; or
- the **DAWHermes** entry in the Windows Start Menu.

Normal use does not require Visual Studio, VS Code, Codex, PowerShell or an open terminal window.

## The main window

The application is divided into several areas.

### Menu bar

The menu bar contains commands for importing files, editing, track operations, Hermes tools, audio-device configuration and view options.

A menu or dialog opens only when you deliberately choose a command. Selecting a track does not automatically open tools.

### Transport strip

The transport strip controls playback. It contains controls for moving backward or forward, playing, pausing, stopping, enabling Loop, reading the current time and viewing the current BPM.

The Master Volume control changes the safe audition level of the complete project.

### Tracks panel

The left panel lists audio, MIDI and group tracks. Selecting a track chooses what you want to inspect or edit. Selection does not decide what the whole-project Play command will sound.

The small `M` and `S` controls change audibility:

- `M` means Mute;
- `S` means Solo.

### Timeline

The centre Timeline shows tracks from left to right in musical time.

- Audio tracks show waveform thumbnails.
- MIDI tracks show compact note bars.
- The ruler at the top shows bars and beats.
- The Loop work region is created on this ruler.

### AI Assistant panel

The right panel is reserved for the DAW-level Composer Assistant workflow. The connector settings and connection probe exist, but full music generation inside DAWHermes is not implemented yet.

### Piano Roll

The full-width lower area is the MIDI editor. It shows MIDI notes against a piano keyboard and a musical grid. This is where notes can be selected, created, moved, resized, deleted, quantized and edited for velocity.

### Status area

The bottom status area reports useful information without forcing repeated popup windows. It may show audio-device status, processing progress, skipped files or a concise error message.

## Your first session

The following short workflow introduces the parts that already work.

### 1. Import a WAV file

1. Choose **File -> Import Audio as Track...**.
2. Select one or more WAV files.
3. Confirm the file chooser.
4. DAWHermes creates one audio track for each valid WAV.
5. The waveform appears in the Timeline.

The WAV remains in its original folder. DAWHermes references it in place and does not copy, convert or modify it.

### 2. Import a MIDI file

1. Choose **File -> Import MIDI as Track...**.
2. Select a MIDI file.
3. When the file contains more than one track with notes, choose the note-bearing track you need.
4. Confirm the import.
5. The MIDI notes appear in the Timeline and Piano Roll.

Tracks that contain only metadata and no notes are not offered as musical tracks.

### 3. Play the project

1. Press **Play**.
2. Every playable, non-muted project track participates, not only the selected track.
3. Watch the shared playhead move in the Timeline and Piano Roll.
4. Read the counter and BPM display.
5. Press **Pause** to hold the current position or **Stop** to return to the project beginning.

### 4. Select and inspect a MIDI track

1. Click a MIDI track in the track list.
2. Look at its notes in the Piano Roll.
3. Move the mouse over a note to see pitch, velocity, start, duration and channel information.
4. Click a note to select it.

Selecting the track does not silence the other tracks. Use Mute or Solo for listening decisions.

### 5. Make a simple MIDI edit

1. Select one note.
2. Drag it left or right to change timing.
3. Drag it up or down to change pitch.
4. Drag its right edge to change its length.
5. Use **Edit -> Undo** when you need to reverse the edit.

### 6. Protect your work before closing

DAWHermes does not yet have a project save/load format. Important edited MIDI should be exported before closing the application:

1. Select the MIDI track you want to keep.
2. Choose **File -> Export Selected MIDI Track...**.
3. Save the new MIDI file in a known folder.

The original imported MIDI file is not overwritten.

## Important beginner principles

### Selection is not audibility

Clicking a track selects it for editing. It does not mean that only this track will play.

Use:

- `M` to remove a track from the sound;
- `S` to hear selected musical material in isolation.

### Source files remain outside the application

WAV and MIDI files are not copied into an internal project package. Moving or deleting a referenced WAV later can make that audio track unavailable.

### Editing changes DAWHermes project memory

MIDI edits affect the version currently held inside DAWHermes. They do not rewrite the imported source MIDI. Use export to create a new file containing the edited result.

### Audio and MIDI may disagree

A MIDI file may contain a tempo or timing interpretation that does not match the real WAV. DAWHermes and Hermes provide tools for analysing and correcting these relationships, but you should still listen critically.

## Current limits to remember

DAWHermes currently does not provide:

- recording;
- a production mixer;
- VST instruments;
- effects processing;
- project save/load;
- audio clip movement or editing;
- full Composer Assistant generation;
- automatic transfer to Cubase.

These limits do not prevent the current preparation workflow: import, listen, isolate, analyse, edit, correct and export MIDI.

---

[Back to the manual contents](README.md) | [Next: Tracks, MIDI and Audio](02_TRACKS_MIDI_AND_AUDIO.md)
