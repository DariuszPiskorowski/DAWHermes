# 2. Tracks, MIDI and Audio

[Previous: Getting Started](01_GETTING_STARTED.md) | [Back to contents](README.md) | [Next: Transport, Loop and the Work Region](03_TRANSPORT_LOOP_AND_WORK_REGION.md)

## What a track is

A track is one musical lane in the project. DAWHermes currently uses three main track types:

- **Audio track** — references a WAV file and displays its waveform.
- **MIDI track** — contains editable note instructions.
- **Group track** — organises related child tracks and controls their shared Mute/Solo behaviour.

The order in the track list is also the order shown in the Timeline.

## Selecting tracks

Click a track row to select it.

Selection is used for tasks such as:

- showing the track in the Piano Roll;
- editing MIDI notes;
- selecting a WAV/MIDI pair for Hermes;
- exporting a MIDI track;
- comparing two MIDI tracks;
- deleting a track.

Selecting a track does **not** decide whether it plays. The Play command auditions the complete playable project. Use Mute and Solo to control what is heard.

### Selecting more than one track

Some operations require more than one selected track, especially:

- selecting one audio track and one MIDI track for bass repair or synchronization;
- selecting exactly two MIDI tracks for visual comparison.

Use the normal Windows multi-selection modifier, such as Ctrl-click, to add or remove tracks from the selection.

Right-click behaviour is deliberate:

- right-clicking an unselected track selects that track before opening its context menu;
- right-clicking a track that is already part of a multi-selection preserves the selection.

## Importing audio WAV

### What audio import does

Choose **File -> Import Audio as Track...** to add one or more WAV files.

For every valid WAV, DAWHermes creates an audio track with:

- a name based on the filename;
- a reference to the original file location;
- a waveform thumbnail in the Timeline;
- audio metadata used for playback and timing;
- eligibility for BPM analysis and Hermes processing.

### Step by step

1. Choose **File -> Import Audio as Track...**.
2. Select one or several WAV files.
3. Confirm the file chooser.
4. Wait for the tracks and waveforms to appear.
5. Press **Play** to hear them as part of the complete project.

### What happens to the source file

The WAV remains in its original folder. DAWHermes does not:

- copy it into the repository or application folder;
- convert it;
- change its sample data;
- write analysis files beside it;
- overwrite it.

This also means the track depends on the original path. If you later move, rename or delete the WAV, DAWHermes may no longer be able to read it.

### Importing several stems

A stem is an audio file containing one part of a production, such as drums, bass, synth or vocals.

You can select several WAV stems in one import operation. DAWHermes creates one track for each valid file. The batch behaves as one Undo/Redo action.

Example:

- `Drums.wav`
- `Bass.wav`
- `Synth.wav`

After import, all three can play together from project time zero.

### Unreadable files

When a batch contains a file that cannot be read, DAWHermes skips that file and reports the problem concisely. Other valid files can still be imported.

## Importing MIDI

Choose **File -> Import MIDI as Track...**.

A MIDI file may contain several internal tracks. DAWHermes offers only tracks that actually contain notes. Metadata-only or empty tracks are not imported as musical tracks.

### Step by step

1. Choose **File -> Import MIDI as Track...**.
2. Select a `.mid` file.
3. When several note-bearing tracks are available, choose the one you need.
4. Confirm the import.
5. Select the created track to inspect its notes in the Piano Roll.

Imported MIDI can preserve useful source information such as:

- tempo events;
- time signatures;
- PPQ timing resolution;
- track name;
- note channels.

This information is important for playback, the Timeline and later export.

## Adding an empty MIDI track

Choose **Track -> Add MIDI Track** when you need a new empty MIDI lane.

An empty MIDI track produces no sound until notes are added. You can create notes by double-clicking empty Piano Roll space after selecting the track.

## Audio waveform and MIDI notes

### Waveform

The waveform is a visual summary of audio loudness over time. It helps you locate starts, stops, accents and musical sections.

The waveform is currently for viewing and alignment. DAWHermes does not yet let you cut, move or stretch audio clips.

### MIDI note bars

MIDI tracks show small note bars in the Timeline and detailed notes in the Piano Roll.

A MIDI note contains:

- pitch;
- start position;
- duration;
- velocity;
- MIDI channel.

These values can be edited without changing the original source MIDI file.

## Mute and Solo

Every track row, including group rows, has compact `M` and `S` controls.

### Mute

Mute removes a track from the audible result.

Use Mute when you want to:

- temporarily hide a part;
- compare the arrangement with and without one layer;
- silence a broken or unfinished track;
- prevent a reference MIDI or WAV from sounding twice.

Click `M` again to restore the track.

A playable track is muted when:

- its own `M` is active; or
- any parent group is muted.

Mute has priority over Solo. A muted track remains silent even if it or its group is soloed.

### Solo

Solo is used to hear one track or one organised group more clearly.

When no Solo is active, all non-muted playable tracks can sound.

When at least one Solo is active, a playable track sounds only when:

- that track is soloed; or
- one of its parent groups is soloed;

and it is not muted by itself or a parent group.

### Group Solo

Soloing a group makes all non-muted playable descendants audible.

Example:

```text
Drums Group [S]
├── Kick
├── Snare
├── Hi-Hat [M]
└── Cymbals
```

You hear Kick, Snare and Cymbals. Hi-Hat remains silent because Mute wins.

### Child Solo

Soloing one child track does not make its siblings audible.

Example:

```text
Drums Group
├── Kick [S]
├── Snare
└── Hi-Hat
```

Only Kick is heard, assuming it is not muted.

### Live changes during playback

Mute and Solo can be changed while the project is playing.

- The transport does not restart.
- The current playback position does not move.
- WAV audio follows the current position when restored.
- MIDI notes that should already be sounding are reconstructed when a track becomes audible again.

The total project duration does not change when tracks are muted or soloed.

## Practical listening examples

### Hear only the bass

1. Start playback.
2. Click `S` on the bass track.
3. Listen for timing, pitch and unwanted gaps.
4. Click `S` again to return to the complete project.

### Compare audio with reconstructed MIDI

Suppose you have `Bass.wav` and a bass MIDI track.

1. Play both together.
2. Mute the MIDI track and listen to the WAV alone.
3. Restore MIDI and mute the WAV.
4. Compare pitch and timing.
5. Use Hermes Bass Repair or Synchronize when the MIDI does not follow the reference correctly.

### Check a drum group

1. Solo the drum group.
2. Mute individual children one at a time.
3. Confirm that each generated drum layer contains the expected events.
4. Restore the full project and judge the drums in context.

## Group tracks

A group track organises child tracks. It does not produce sound by itself.

Hermes drum extraction can create a real group containing several generated MIDI layers. Group Mute/Solo then gives you quick control over the entire extracted kit.

## Deleting a track

Select a track and choose **Track -> Delete Selected Track** or use the appropriate deliberate context-menu command.

Before deleting an important edited MIDI track, export it when you may need the result later. DAWHermes does not yet have project save/load.

## Undo and Redo

Undo and Redo are available for many project changes, including:

- MIDI note creation and editing;
- MIDI note deletion;
- quantization;
- imported audio batches;
- inserted Hermes results.

Selection changes, Mute/Solo, view changes and MIDI export are not musical edit-history entries.

When you undo a Hermes-generated result and then redo it, DAWHermes restores the stored result. It does not run the analysis again.

## Important limitations

- Audio tracks start at project time zero.
- Audio clips cannot yet be moved, cut, faded or stretched.
- There is no mixer with faders, pan or effects.
- There is no project save/load format.
- Track selection and track audibility are separate concepts.
- The internal MIDI sound is for functional audition, not final production.

---

[Previous: Getting Started](01_GETTING_STARTED.md) | [Back to contents](README.md) | [Next: Transport, Loop and the Work Region](03_TRANSPORT_LOOP_AND_WORK_REGION.md)
