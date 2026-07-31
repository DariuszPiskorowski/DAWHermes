# 6. MIDI Editor

[Previous: BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md) | [Back to contents](README.md) | [Next: VST3 Instruments](07_VST3_INSTRUMENTS.md)

## What the MIDI editor is

The MIDI editor is the full-width lower part of the DAWHermes window.

It contains:

- a piano keyboard on the left;
- a Piano Roll grid;
- MIDI note rectangles;
- pitch and view controls;
- a compact velocity editor;
- comparison information when Compare mode is active.

The MIDI editor changes note instructions, not recorded WAV audio.

## Opening a MIDI track in the Piano Roll

1. Import a MIDI track or create an empty MIDI track.
2. Click the MIDI track in the track list.
3. Its notes appear in the Piano Roll.

The selected editable track is the primary track. A comparison overlay may also be visible, but the comparison track remains read-only.

## Understanding a MIDI note

Each note rectangle represents one musical event.

Its properties are:

- **Pitch** — which note is played.
- **Start** — where the note begins.
- **Duration** — how long it lasts.
- **Velocity** — how strongly the note is played.
- **Channel** — the MIDI channel used by the event.

Move the pointer over a note to view its pitch, velocity, start, duration and channel.

## Selecting notes

### Select one note

Click a note.

The note becomes the current selection for editing.

### Add or remove one note from a selection

Hold Ctrl and click another note.

This allows several separate notes to be edited together.

### Clear the note selection

Click empty Piano Roll space without dragging.

### Marquee selection

A marquee is a rectangular selection box.

1. Begin dragging in empty Piano Roll space.
2. Draw a rectangle around the notes you need.
3. Release the mouse button.

Notes inside the rectangle become selected.

Use the additive selection modifier when you need to add the marquee result to an existing selection.

## Creating notes

Double-click empty Piano Roll space.

DAWHermes creates a note at the clicked pitch and time.

When Snap is enabled, the note begins on the active grid. Its minimum duration follows the editor's supported note-length rules.

Use note creation for:

- filling a missing pitch;
- adding a simple accompaniment note;
- rebuilding a short phrase;
- creating material on an empty MIDI track.

## Deleting notes

1. Select one or more notes.
2. Press Delete or Backspace.

You can also choose **Edit -> Delete Selected Notes**.

The deletion is one Undo/Redo action.

## Moving notes with the mouse

1. Select the note or notes.
2. Drag a selected note.
3. Move horizontally to change timing.
4. Move vertically to change pitch.
5. Release the mouse button to commit the edit.

When several notes are selected, dragging one of the selected notes moves the selected group together.

DAWHermes prevents pitches from moving outside the valid MIDI range.

## Changing note length

1. Move the pointer to the right edge of a note.
2. Drag the edge left to shorten the note.
3. Drag the edge right to lengthen it.
4. Release the mouse button.

Changing duration is different from moving a note. The start stays in place while the end changes.

Use duration editing when:

- notes overlap too far;
- a bass note stops too early;
- a short stab should be more sustained;
- a converted note has an incorrect tail.

## Keyboard nudging

Arrow keys provide precise movement.

- **Left Arrow** — move selected notes earlier by one active grid step.
- **Right Arrow** — move selected notes later by one active grid step.
- **Up Arrow** — raise selected notes by one semitone.
- **Down Arrow** — lower selected notes by one semitone.
- **Shift + Up Arrow** — raise selected notes by one octave.
- **Shift + Down Arrow** — lower selected notes by one octave.

The arrow keys edit selected notes. They do not scroll the complete Piano Roll.

## Snap

Snap places note edits on the active musical grid.

When Snap is enabled, it affects:

- note creation;
- mouse movement;
- duration resize;
- keyboard timing nudges;
- quantization.

When Snap is disabled, mouse operations can use finer positions.

Use Snap when the music should follow regular beat divisions. Turn it off when correcting material that intentionally falls between grid lines.

## Grid resolution

The editor provides grid choices such as:

- `1/4`;
- `1/8`;
- `1/16`;
- `1/32`.

A smaller division gives finer timing control.

Examples:

- `1/4` for quarter-note placement;
- `1/8` for common rhythmic movement;
- `1/16` for detailed dance-music patterns;
- `1/32` for very short events or fine correction.

The grid also affects Loop snapping on the Timeline.

## Quantization

Quantization moves selected note starts to the active grid.

1. Choose the desired grid resolution.
2. Select the notes.
3. Choose **Edit -> Quantize Selected Notes to Grid**.

Use quantization when:

- imported notes are almost, but not exactly, on the beat;
- generated drum events need a regular grid;
- a repeated synth pattern should be mechanically even.

Quantization is not always musically better. Human timing and intentional swing can be damaged by an unsuitable grid.

The current quantize command has no strength, swing or groove controls. It performs a direct grid operation.

## Velocity

Velocity describes the intensity of a MIDI note. Depending on the final instrument, it may affect loudness, attack, timbre or another performance characteristic.

DAWHermes provides a compact velocity editor for selected notes.

1. Select one or more notes.
2. Change the velocity value.
3. Audition the result.

The internal synth gives a functional indication, but a production instrument in Cubase may respond differently.

Changing velocity does not change pitch, timing or duration.

## Undo and Redo

The following MIDI edits create history actions:

- note creation;
- deletion;
- mouse movement;
- duration resize;
- keyboard nudge;
- velocity change;
- quantization.

Selection changes, view changes, Snap changes and export do not create musical history entries.

Use Undo immediately when an edit moves or changes more notes than intended.

## Zoom, fit and navigation

The Timeline and Piano Roll share the same horizontal musical viewport.

This keeps the selected passage aligned between the arrangement view and detailed MIDI view.

The Piano Roll also provides pitch-view controls, including fitting the visible note range.

Use Fit when notes have moved outside the currently visible pitch area.

## Compare Selected MIDI Tracks

Compare mode displays the differences between exactly two selected MIDI tracks.

1. Select exactly two MIDI tracks.
2. Choose **View -> Compare Selected MIDI Tracks**.
3. Read the colour-coded comparison overlay and legend.

Compare mode is visual and read-only.

The comparison overlay does not become editable and does not change either MIDI track.

Use it to compare:

- original MIDI against a Hermes result;
- original notes against a manually corrected copy;
- two alternative arrangements;
- a synchronized track against its unsynchronized source.

## Editing while Loop plays

A powerful workflow is to combine the MIDI editor with a Loop range.

1. Create a Loop around the problem passage.
2. Play the project.
3. Select the MIDI notes that need correction.
4. Pause when you need a stable visual position.
5. Edit pitch, start, duration or velocity.
6. Resume or restart playback to hear the updated prepared result.
7. Use Mute/Solo to compare reference WAV and MIDI.

Large note edits may become audible after playback is prepared again. Mute/Solo remains live during playback.

## Practical examples

### Correct one wrong bass pitch

1. Loop the bar containing the wrong note.
2. Solo the bass WAV and MIDI as needed.
3. Select the MIDI note.
4. Use Up/Down Arrow to find the correct semitone.
5. Audition the full arrangement.

### Tighten a drum pattern

1. Choose the appropriate grid, often `1/16` for detailed dance rhythms.
2. Select the notes that should be regular.
3. Quantize them.
4. Listen against the drum WAV.
5. Undo when the result loses the intended groove.

### Shorten overlapping notes

1. Select notes whose tails overlap badly.
2. Drag their right edges left.
3. Listen for cleaner articulation.
4. Export the finished MIDI track.

### Create a missing note

1. Select the target MIDI track.
2. Enable a suitable grid.
3. Double-click the empty position.
4. Move or resize the new note as needed.
5. Set its velocity.

## Exporting the edited result

1. Select the non-empty MIDI track.
2. Choose **File -> Export Selected MIDI Track...**.
3. Choose a destination and filename.
4. Save the file.

Export uses the edited notes currently held in DAWHermes. It does not re-read or overwrite the original MIDI file.

More detail is available in [Export and File Exchange](10_EXPORT_AND_FILE_EXCHANGE.md).

## Important limitations

The current MIDI editor does not yet provide:

- copy and paste;
- controller lanes;
- sustain-pedal editing;
- pitch-bend editing;
- full velocity lanes;
- swing or groove quantization;
- Timeline clip movement;
- MIDI automation;
- VST instrument hosting;
- project save/load.

The current styling is functional rather than final.

---

[Previous: BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md) | [Back to contents](README.md) | [Next: VST3 Instruments](07_VST3_INSTRUMENTS.md)
