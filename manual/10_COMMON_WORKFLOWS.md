# 10. Common Workflows

[Previous: Export and File Exchange](09_EXPORT_AND_FILE_EXCHANGE.md) | [Back to contents](README.md) | [Next: Glossary](11_GLOSSARY.md)

This chapter combines individual DAWHermes functions into complete musical tasks.

Workflows marked **Available now** can be performed with the current application. Workflows marked **Planned** explain the intended future direction and are not yet complete commands.

# 1. Import WAV stems and play the complete project

**Status: Available now**

Use this when you have separate audio files for drums, bass, synths or other parts.

1. Choose **File -> Import Audio as Track...**.
2. Select all required WAV stems.
3. Confirm the file chooser.
4. Wait for the tracks and waveforms to appear.
5. Press **Play**.
6. Confirm that all non-muted stems play together.
7. Use `M` to remove one stem temporarily.
8. Use `S` to hear one stem or group in isolation.
9. Press **Stop** when finished.

The source WAV files remain unchanged and stay in their original folder.

# 2. Import MIDI and compare it with a source WAV

**Status: Available now**

Use this when you have both recorded audio and a MIDI interpretation of the same part.

1. Import the WAV.
2. Import the MIDI file with **File -> Import MIDI as Track...**.
3. Select the relevant note-bearing MIDI track during import.
4. Play the complete project.
5. Solo the WAV and MIDI tracks when you need to focus on them.
6. Mute the MIDI and listen to the WAV alone.
7. Restore MIDI and mute the WAV.
8. Listen for wrong pitch, shifted starts, incorrect note lengths and missing events.
9. Create a short Loop around the problem passage.
10. Decide whether the MIDI needs manual editing, bass repair or synchronization.

# 3. Correct MIDI notes manually

**Status: Available now**

Use this when only a small number of notes are wrong.

1. Select the MIDI track.
2. Create a Loop around the problem area.
3. Open the notes in the Piano Roll.
4. Select the wrong note or notes.
5. Change pitch with vertical dragging or Up/Down Arrow.
6. Change timing with horizontal dragging or Left/Right Arrow.
7. Change note length by dragging the right edge.
8. Adjust velocity when required.
9. Re-audition the passage.
10. Use Undo when the change is worse.
11. Export the corrected track before closing DAWHermes.

# 4. Convert drum WAV into MIDI with Hermes

**Status: Available now**

Use this when you have a drum stem but need editable drum events.

1. Import the drum WAV.
2. Select its audio track.
3. Choose **Hermes -> Drums -> Make MIDI from WAV...**.
4. Choose a result layout:
   - separate MIDI tracks;
   - one grouped multitrack;
   - one single drum track.
5. Choose the detection profile.
6. Choose Multi-detector or Global mode.
7. Choose the target mapping.
8. Decide whether enabled empty layers should be created.
9. Choose **Create MIDI**.
10. Wait for processing to finish.
11. Loop a representative section.
12. Solo generated layers one by one.
13. Correct missing or extra notes in the Piano Roll.
14. Export the required result tracks.

A grouped result is especially useful when you want one Solo control for the full drum kit and separate child tracks for editing.

# 5. Repair bass MIDI against bass audio

**Status: Available now**

Use this when bass MIDI exists but its musical notes do not accurately follow the WAV.

1. Import the bass WAV.
2. Import the bass MIDI candidate.
3. Select both tracks.
4. Choose **Hermes -> Bass -> Repair MIDI against WAV...**.
5. Confirm the displayed WAV and MIDI pair.
6. Name the repaired result track.
7. Choose **Run**.
8. Wait for the new MIDI track.
9. Create a Loop around a clear bass phrase.
10. Compare original MIDI, repaired MIDI and WAV with Mute/Solo.
11. Edit remaining mistakes manually.
12. Export the repaired track.

The original WAV and MIDI remain unchanged.

# 6. Synchronize MIDI timing with WAV

**Status: Available now**

Use this when the notes are broadly correct but their attacks do not align with the audio.

1. Import the reference WAV and MIDI candidate.
2. Select both tracks.
3. Choose **Hermes -> Synchronize MIDI with WAV...**.
4. Choose the role: Bass, Drums, Synth, Guitar or Other.
5. Keep source tempo-map preservation enabled when the MIDI tempo is trustworthy.
6. When disabling tempo preservation, enter the intended BPM override.
7. Name the synchronized result track.
8. Choose **Run**.
9. Loop a passage with clear attacks.
10. Alternate between original and synchronized MIDI.
11. Inspect note starts in the Piano Roll.
12. Make final manual corrections.
13. Export the synchronized result.

Synchronization does not change WAV playback speed.

# 7. Use Loop to inspect a difficult passage

**Status: Available now**

Use this for any section that needs repeated attention.

1. Drag on the Timeline ruler to create a Loop range.
2. Adjust its left and right boundaries.
3. Move the complete range when necessary.
4. Enable **Loop**.
5. Press **Play**.
6. Use Mute/Solo to isolate relevant tracks.
7. Pause when you need to inspect notes without losing position.
8. Edit MIDI or compare Hermes results.
9. Disable Loop when you need to hear the surrounding arrangement.
10. Keep the visible range as a remembered work region.
11. Right-click the ruler and choose **Clear Loop Range** when finished.

# 8. Prepare a work region for Composer Assistant

**Status: Partly available; generation is planned**

The current application can prepare and audition the work region, but it cannot yet submit it for generation.

1. Identify the empty or weak passage.
2. Create a Loop that covers the intended edit area.
3. Include enough music before and after it for context.
4. Listen to the complete project.
5. Select the MIDI track or musical role that will need new material.
6. Inspect the surrounding MIDI in the Piano Roll.
7. Keep the Loop range visible as the defined work region.

**Planned next steps:**

8. Send the region, surrounding MIDI and relevant project tracks to Composer Assistant.
9. Receive a MIDI proposal.
10. Audition it inside the same Loop.
11. Keep, edit or reject it.

Steps 8–11 are not implemented yet.

# 9. Solo a group and edit one musical layer

**Status: Available now**

Use this with a grouped Hermes drum result or any group hierarchy.

1. Solo the group.
2. Confirm that its non-muted children are heard.
3. Mute children that are not relevant.
4. Select one child MIDI track.
5. Create a Loop around the section being edited.
6. Correct the child's notes in the Piano Roll.
7. Restore other children one at a time.
8. Disable group Solo to hear the result in the full arrangement.

Remember: Mute wins over Solo, and soloing one child does not automatically include its siblings.

# 10. Compare original and corrected MIDI visually

**Status: Available now**

1. Keep both the original and corrected MIDI tracks.
2. Select exactly those two MIDI tracks.
3. Choose **View -> Compare Selected MIDI Tracks**.
4. Read the colour-coded overlay and legend.
5. Use the comparison to locate added, removed or changed notes.
6. Leave the comparison overlay read-only.
7. Edit the primary MIDI track, not the ghost comparison.
8. Turn Compare mode off when finished.

Use this after bass repair, synchronization or manual correction.

# 11. Export corrected MIDI for Cubase

**Status: Available now through standard MIDI export**

1. Finish editing and listening in DAWHermes.
2. Select the final non-empty MIDI track.
3. Choose **File -> Export Selected MIDI Track...**.
4. Save the MIDI with a clear version name.
5. Open Cubase.
6. Import the MIDI file.
7. Assign the intended VST instrument.
8. Confirm tempo, channel, timing and mapping.
9. Continue sound design, arrangement, mixing and effects in Cubase.

Cubase-specific one-click exchange is planned but not yet available.

# 12. Check two possible MIDI versions by ear

**Status: Available now**

1. Keep both versions as separate MIDI tracks.
2. Create a Loop around the passage where they differ.
3. Solo version A.
4. Listen for several repetitions.
5. Turn off A's Solo and Solo version B.
6. Compare rhythm, pitch direction and fit with the arrangement.
7. Restore the complete project.
8. Use Compare mode when you also need a visual difference view.
9. Export the chosen result.

# 13. Check project tempo before detailed editing

**Status: Available now**

1. Read the BPM display.
2. Identify whether the project contains MIDI with explicit tempo.
3. Remember that the first suitable MIDI tempo has priority.
4. When no explicit MIDI tempo exists, allow WAV analysis to complete.
5. Listen for half-time/double-time interpretation.
6. Confirm that the grid follows the intended musical pulse.
7. Distinguish wrong tempo from notes that are merely shifted.
8. Use synchronization for timing alignment, not as a replacement for understanding BPM.

See [BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md) for the full explanation.

# 14. Change the audio output safely

**Status: Available now**

1. Stop playback.
2. Choose **Audio -> Audio Settings...**.
3. Select the intended output device.
4. Confirm active channels.
5. Keep the current device sample rate or choose a supported value.
6. Begin with the device's stable buffer setting.
7. Use **Audio -> Test Output**.
8. Read **Audio Device Status...** when you need confirmation.
9. Play the project.

# Workflow principles

Across all workflows, remember:

- source WAV and MIDI files remain unchanged;
- DAWHermes project state is not yet saved as a project file;
- export important edited MIDI before closing;
- selection chooses editing targets;
- Mute/Solo controls audibility;
- Loop defines the focused work region;
- Hermes analyses or corrects existing material;
- Composer Assistant generation remains planned;
- final instruments, mixing and mastering belong in Cubase.

---

[Previous: Export and File Exchange](09_EXPORT_AND_FILE_EXCHANGE.md) | [Back to contents](README.md) | [Next: Glossary](11_GLOSSARY.md)
