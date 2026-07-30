# 3. Transport, Loop and the Work Region

[Previous: Tracks, MIDI and Audio](02_TRACKS_MIDI_AND_AUDIO.md) | [Back to contents](README.md) | [Next: Composer Assistant](04_COMPOSER_ASSISTANT.md)

## What the transport does

The transport controls movement through the project and decides when audio and MIDI playback starts, pauses, stops or repeats.

DAWHermes uses one shared project position for:

- all playable WAV tracks;
- all playable MIDI tracks;
- the Timeline playhead;
- the Piano Roll playhead;
- the time counter;
- the Loop range.

Track selection does not define transport playback. Play uses the complete project and Mute/Solo decide which tracks are heard.

## Transport controls

The transport row is arranged as:

```text
<<   Play   Pause   Stop   >>   Loop   counter   BPM
```

The Master Volume control sets the safe overall audition level.

## Playing the project

Press **Play** to audition the complete playable project.

DAWHermes includes:

- every non-empty MIDI track;
- every readable imported WAV track;
- tracks allowed by the current Mute/Solo state.

It excludes:

- group rows themselves;
- empty MIDI tracks;
- missing or unreadable WAV files;
- read-only comparison overlays.

### What happens when the project changes

When Play begins, DAWHermes prepares a stable playback version of the current notes and WAV sources. This prevents edits from changing audio unpredictably in the middle of playback.

Mute and Solo remain live. Larger content changes, such as edited notes or replaced sources, are heard after playback is prepared again.

## Pause

Press **Pause** when you want to stop sound temporarily without losing your position.

Pause preserves:

- the current project time;
- the visible playhead;
- the current Loop position;
- the current viewport.

Press **Play** or the resume control to continue from the paused position.

Use Pause when:

- you need to inspect a note at the current location;
- you want to compare the same position after changing a listening setting;
- you do not want to return to the beginning.

## Stop

Press **Stop** to end playback and reset the project position to time zero.

Stop:

- silences WAV and MIDI playback;
- resets the counter to `00:00`;
- hides the active playhead;
- preserves the horizontal view instead of forcing the screen back to the beginning.

Use Stop when you have finished auditioning a section or want the next playback to begin from the project start, unless an enabled Loop range redirects the start position.

## Moving backward and forward

The `<<` and `>>` controls move the transport by five seconds.

They are useful for:

- quickly repeating a phrase;
- moving past a long intro;
- checking the beginning of a transition;
- locating a timing problem without dragging the playhead precisely.

The position is kept inside valid project or Loop limits.

## Counter

The counter shows:

```text
current time / total project time
```

Example:

```text
00:42 / 03:18
```

The total duration is based on the complete playable project. It does not become shorter simply because a track is muted or soloed.

## BPM display

The BPM display shows the tempo currently used for the musical ruler, MIDI timing and Loop conversion.

It does not change the speed of WAV playback.

The source of this value is explained in detail in [BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md).

## Playhead and viewport follow

The orange playhead appears in both the Timeline and Piano Roll.

During playback, the shared horizontal viewport follows when the playhead approaches the edge of the visible region. The two workspaces remain aligned to the same musical time.

Pause leaves the playhead visible. Stop hides it.

## Master Volume

Master Volume controls the overall audition level of MIDI and WAV playback.

It does not edit source files and it is not a production mixer or automation control.

Keep the level moderate, especially when importing unfamiliar WAV stems or changing audio devices.

# Loop

## What Loop is

Loop repeats a chosen musical time range.

Instead of playing through the complete project once, DAWHermes returns from the end of the range to its beginning and continues until you pause or stop.

Example:

```text
bar 17 -> bar 18 -> ... -> bar 25 -> bar 17 -> ...
```

## Why Loop exists

Repeated listening is essential when working on a small part of a track. A single playback may not reveal whether:

- a bass note begins too early;
- a drum fill is convincing;
- a transition leads naturally into the next section;
- MIDI follows the audio reference;
- a generated phrase fits its surrounding context;
- an edit improves or damages the groove.

Loop keeps the same passage returning automatically, so you can listen while editing or switching Mute/Solo states.

## Loop as the DAWHermes work region

In DAWHermes, the Loop range is more than a repeat control. It is the project's focused **work region**.

The work region identifies the exact passage currently under attention. It can be used for:

- repeated listening;
- focused MIDI correction;
- checking WAV/MIDI synchronization;
- comparing original and corrected tracks;
- evaluating a transition or fill;
- isolating the passage that should later be sent to Composer Assistant.

This is central to the intended DAWHermes workflow:

```text
Choose a passage
Create the Loop work region
Listen in context
Edit or analyse the passage
Ask Composer Assistant to work inside the same region
Audition the result repeatedly
Keep, edit or reject the result
```

**Current status:** Loop playback and visible work-region editing are implemented. Passing the range into Composer Assistant for music generation is planned and is not available yet.

## Creating a Loop range

Use the ruler at the top of the Timeline.

1. Move the pointer to empty ruler space near the desired beginning.
2. Hold the left mouse button.
3. Drag to the desired end.
4. Release the button after passing the normal drag threshold.
5. A translucent range appears across the ruler and Timeline lanes.
6. Press **Loop** to enable repeating playback.
7. Press **Play**.

A simple click without a drag seeks the transport instead of creating a range.

## Enabling and disabling Loop

Press **Loop** to turn repetition on or off.

- When Loop is on, playback wraps at the range boundary.
- When Loop is off, playback continues normally through the project.
- Turning Loop off does not delete the range.

Keeping the range visible is useful because it preserves the musical passage you are working on even when you temporarily need normal playback.

## Resizing the range

### Change the beginning

1. Move the pointer to the left boundary.
2. Drag it left or right.
3. Release it at the new start position.

### Change the end

1. Move the pointer to the right boundary.
2. Drag it left or right.
3. Release it at the new end position.

Use boundary resizing when the chosen phrase is slightly too long or short.

## Moving the complete range

1. Move the pointer inside the highlighted range, away from its boundaries.
2. Drag the range left or right.
3. Release it at a new location.

The length remains unchanged.

This is useful when comparing equivalent passages, for example:

- verse 1 against verse 2;
- the first chorus against the final chorus;
- two different four-bar fills.

## Snap and Loop boundaries

When Snap is enabled, Loop boundaries follow the active beat-grid resolution.

Available grids include:

- `1/4`;
- `1/8`;
- `1/16`;
- `1/32`.

Snap helps create clean musical boundaries at bars or beat divisions.

When Snap is disabled, the range can use a finer unsnapped position. This can be useful for audio that does not align perfectly with the musical grid, although a musically aligned range is usually easier for MIDI work.

## Clearing the range

To remove the range completely:

1. Right-click the Timeline ruler.
2. Choose **Clear Loop Range**.

Clearing the range also disables Loop.

This is different from merely turning the Loop button off.

## Where playback begins

When Loop is enabled:

- if the stopped transport position is already inside the range, Play begins there;
- if the stopped position is outside the range, Play begins at the Loop start.

After reaching the Loop end, playback returns to the start.

## What happens at the Loop boundary

DAWHermes returns WAV and MIDI to the same project position.

At the boundary:

- WAV playback resumes from the corresponding source position;
- MIDI notes that should end at the boundary are stopped;
- MIDI notes that should already be active at the Loop start are reconstructed;
- the shared playhead returns to the Loop start;
- playback continues without waiting for the visible screen timer.

The purpose is to prevent WAV and MIDI from drifting apart or leaving hanging notes during repetition.

## Editing the range while playing

You can change Loop boundaries while playback is active.

The new valid range becomes active safely. When the current position lies beyond the new end, playback returns to the new start.

Use this carefully: a large change can produce an immediate musical jump, which is expected because the work region has changed.

## Pause, Stop and seeking with Loop

### Pause

Pause holds the current position inside the Loop. Resume continues from there.

### Stop

Stop resets project time to zero and hides the playhead. When Loop remains enabled, the next Play starts at the Loop start because time zero lies outside the range.

### Five-second seeking

The backward and forward controls remain available. The resulting position stays within valid playback limits.

## Practical Loop uses

### Correct four bars of bass

1. Create a Loop around the four bars.
2. Solo the bass WAV and MIDI tracks as needed.
3. Listen for pitch and timing differences.
4. Edit MIDI notes.
5. Repeat until the corrected MIDI follows the reference.

### Check a drum fill

1. Create a Loop that begins one or two bars before the fill.
2. Include the bar after the fill so you hear how it resolves.
3. Solo the drum group, then restore the full arrangement.
4. Judge both detail and context.

### Prepare a transition for Composer Assistant

1. Create a Loop around the weak or empty transition.
2. Include enough surrounding music to understand its role.
3. Decide which MIDI track or musical role needs work.
4. Keep this range as the intended assistant work region.

The final submission to Composer Assistant is planned, not yet implemented.

### Compare before and after

1. Keep the original MIDI track.
2. Create or import a corrected version.
3. Loop the relevant passage.
4. Alternate Mute/Solo between versions.
5. Keep the version that works better in the arrangement.

## Important limitations

- Loop repeats project playback but does not time-stretch WAV files.
- The range does not currently submit music to Composer Assistant.
- Audio clips cannot be moved or cut inside the range.
- Loop is not an automation or punch-recording range.
- There is no crossfade at the boundary.
- Correct musical boundaries still depend on sensible BPM and timing information.

---

[Previous: Tracks, MIDI and Audio](02_TRACKS_MIDI_AND_AUDIO.md) | [Back to contents](README.md) | [Next: Composer Assistant](04_COMPOSER_ASSISTANT.md)
