# 12. Glossary

[Previous: Common Workflows](11_COMMON_WORKFLOWS.md) | [Back to contents](README.md)

## ACE Bridge 2

A plug-in installed with ACE Studio that can connect a compatible DAW to ACE Studio for MIDI and audio synchronization. DAWHermes can host VST3 instruments, but ACE Bridge 2 has not been validated as a supported DAWHermes integration.

## ACE-Step

A separate open-source local music-generation model. ACE-Step is not a free edition of ACE Studio and does not automatically provide ACE Studio projects, voices, instruments, Bridge synchronization or MCP tools.

## ACE Studio

A separate commercial AI music workstation planned as a first-class DAWHermes integration for AI vocal and AI instrument workflows. ACE Studio is installed and licensed separately. DAWHermes should remain usable when ACE Studio is not installed.

## Audio

Recorded sound represented as digital samples. In DAWHermes, imported audio currently uses WAV files.

## Audio device

The Windows hardware or driver destination used for sound input or output, such as laptop speakers, headphones or a USB audio interface.

## Bar

A musical measure containing a defined number of beats. In common 4/4 time, one bar usually contains four quarter-note beats.

## Beat

A regular unit of musical pulse. BPM describes how many beats occur in one minute.

## Beat grid

The visible musical timing divisions used for note placement, quantization, Snap and Loop boundaries.

## Beat warping

Changing audio timing so recorded beats align to a new grid or tempo. DAWHermes does not currently provide beat warping.

## BPM

Beats per minute. The numerical tempo used to relate musical beats to real time.

## Buffer size

The number of audio samples prepared in one block for the audio device. Smaller buffers can reduce delay but increase processing pressure. Larger buffers can improve stability but increase delay.

## Channel

A numbered route in MIDI or audio. MIDI channels can separate instrument instructions. Audio channels commonly represent left and right stereo output.

## Composer Assistant

The planned DAW-level musical assistant intended to generate, continue or improve MIDI inside a chosen work region while considering surrounding project context. It is also intended to coordinate the future ACE Studio MCP workflow. Full generation and ACE Studio connection are not yet integrated.

## Compare mode

A read-only visual overlay used to compare exactly two selected MIDI tracks. The comparison ghost cannot be edited.

## Counter

The transport display showing current playback time and total project duration.

## DAW

Digital Audio Workstation. Software used to record, edit, arrange and produce music. DAWHermes is a focused DAW workbench rather than a complete replacement for Cubase.

## Double-time

A tempo interpretation that counts the pulse twice as fast. For example, the same accent pattern may be interpreted as 70 BPM or 140 BPM.

## Drum mapping

The assignment between drum roles and MIDI note numbers. A mapping decides which note triggers kick, snare, hi-hat or another drum sound in the destination instrument.

## Duration

The amount of time a MIDI note remains active.

## Export

Writing prepared project data to a new external file. DAWHermes currently exports one selected MIDI track at a time.

## Ghost track

A read-only MIDI comparison overlay. It helps show differences but cannot be edited or played as an independent project track.

## Grid

See **Beat grid**.

## Group track

A parent row that organises child tracks. A group produces no sound by itself, but its Mute and Solo controls affect playable descendants.

## Half-time

A tempo interpretation that counts the pulse at half the faster rate. A 140 BPM rhythm with strong alternating accents may be interpreted as 70 BPM.

## Hermes

The built-in DAWHermes analysis and correction system. Current Hermes workflows include drum audio-to-MIDI extraction, bass MIDI repair and MIDI/WAV synchronization.

## Latency

Delay between an action or input and the corresponding audio output. Device, buffer and driver settings influence latency.

## Loop

A transport mode that repeatedly plays a selected musical time range. In DAWHermes the visible Loop range also acts as the work region for focused editing and the planned Composer Assistant workflow.

## Master Volume

The overall safe audition level for project playback. It is not a production mixer fader or automation control.

## MCP Server

A local server exposed by a compatible application through the Model Context Protocol. ACE Studio 2.0 includes an experimental MCP Server option that the DAWHermes project owner has confirmed is available in the paid development installation. DAWHermes does not connect to it yet.

## MIDI

Musical Instrument Digital Interface data. MIDI contains instructions such as note pitch, start, duration, velocity and channel. It does not contain recorded instrument sound.

## MIDI channel

A numbered MIDI route from 1 to 16 used to organise or direct musical events.

## MIDI note

An instruction to play one pitch with a defined start, duration, velocity and channel.

## Mute

A track control that makes a track inaudible. Mute also applies through parent groups and has priority over Solo.

## Note-off

The MIDI event that ends a sounding note.

## Note-on

The MIDI event that begins a sounding note.

## Onset

The beginning or attack of a sound event. Audio analysis often uses onsets to locate rhythmic events.

## Piano Roll

The MIDI editing grid that places time horizontally and pitch vertically against a piano keyboard.

## Pitch

How high or low a musical note is.

## Playhead

The moving visual line that shows the current project playback position in the Timeline and Piano Roll.

## PPQ

Pulses per quarter note. A MIDI timing resolution that determines how precisely beat positions are stored.

## Project order

The top-to-bottom order of tracks in the DAWHermes project. Current tempo-source selection uses this order deterministically.

## Quantization

Moving selected MIDI note starts to the chosen beat grid.

## Recording

Capturing new audio or MIDI input into tracks. Recording is not implemented in DAWHermes yet.

## Sample rate

The number of digital audio samples processed per second, commonly 44.1 kHz or 48 kHz.

## Snap

An editor mode that aligns note operations and Loop boundaries to the selected beat-grid division.

## Solo

A track control used to hear selected tracks or groups while excluding other non-soloed material. Mute still has priority.

## Source file

The original WAV or MIDI file imported or referenced by a DAWHermes track. DAWHermes does not overwrite these files during normal editing, Hermes processing or export.

## Stem

An audio file containing one musical part or related group, such as drums, bass, vocals or synths.

## Synchronization

Adjusting MIDI timing so events follow a WAV reference more closely. Synchronization is different from changing BPM or changing WAV speed.

## Tempo

The speed of the musical pulse, usually expressed in BPM. A tempo map can contain tempo changes through a piece.

## Tempo map

A sequence of MIDI tempo events that defines how musical beats convert to real time across the project.

## Time signature

A musical instruction describing how beats are grouped into bars, such as 4/4.

## Timeline

The arrangement view showing project tracks from left to right in time, including waveforms, MIDI note summaries, ruler, playhead and Loop range.

## Time stretching

Changing audio duration or tempo, usually while preserving pitch. DAWHermes does not currently provide time stretching.

## Track

One project lane containing audio, MIDI or a group hierarchy.

## Transport

The controls and state used to play, pause, stop, seek and loop through the project.

## Velocity

A MIDI value representing note-playing intensity. Depending on the instrument, it may affect loudness, attack or tone.

## Viewport

The visible horizontal or vertical part of a larger Timeline or Piano Roll.

## WAV

A common uncompressed or lossless-compatible digital audio file format used by DAWHermes for imported audio tracks.

## Waveform

A visual representation of audio amplitude through time.

## Work region

The focused musical passage identified by the Loop range. It is used now for repeated listening and editing and is intended later to define the passage sent to Composer Assistant and coordinated with ACE Studio.

---

[Back to the manual contents](README.md)
