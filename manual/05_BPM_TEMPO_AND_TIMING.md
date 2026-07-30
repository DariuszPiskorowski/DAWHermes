# 5. BPM, Tempo and Timing

[Previous: Composer Assistant](04_COMPOSER_ASSISTANT.md) | [Back to contents](README.md) | [Next: MIDI Editor](06_MIDI_EDITOR.md)

## What BPM means

BPM means **beats per minute**.

A tempo of 120 BPM means that 120 quarter-note beats pass in one minute. A tempo of 90 BPM is slower. A tempo of 140 BPM is faster.

BPM is not the same as loudness, musical key or note velocity.

## Why tempo matters in DAWHermes

DAWHermes uses tempo information to connect musical time with real time.

Tempo affects:

- bar and beat positions on the Timeline ruler;
- the horizontal MIDI grid;
- MIDI note playback timing;
- the displayed BPM value;
- conversion between a beat-based Loop range and seconds;
- the position of MIDI notes relative to the project playhead;
- exported MIDI tempo information when source metadata is available.

## BPM does not change WAV speed

A very important rule is:

> DAWHermes does not speed up or slow down a WAV file merely because it detects or chooses a BPM value.

WAV audio plays at its original speed.

The selected BPM helps DAWHermes draw musical time and schedule MIDI. It does not perform time stretching, beat warping or pitch-preserving tempo conversion.

This means a wrong BPM can make the grid and MIDI interpretation look incorrect even though the WAV itself still sounds at normal speed.

## MIDI tempo and audio tempo are different kinds of information

### Tempo stored in MIDI

A MIDI file may contain explicit tempo events. These are instructions that say how fast MIDI beats should pass.

This information may be correct, but it can also be misleading when:

- the MIDI was created from a different version of the song;
- the file contains a default tempo rather than the real tempo;
- audio was recorded without following that MIDI tempo;
- a conversion tool guessed or inserted tempo metadata;
- the MIDI represents half-time or double-time differently from the audio.

### Tempo detected from WAV

A WAV file often does not contain a trustworthy musical tempo value.

DAWHermes analyses rhythmic changes in the audio and estimates a likely BPM. This is useful, but audio tempo detection is an interpretation rather than guaranteed metadata.

The detector looks for repeating onset patterns and compares related tempo candidates.

## How DAWHermes chooses project tempo

The current project-tempo priority is:

1. the first non-empty MIDI track in project order that contains explicit tempo metadata;
2. otherwise, the first readable WAV track in project order with a confident detected BPM;
3. otherwise, the 120 BPM fallback.

This order is deterministic: the same project arrangement produces the same choice.

## Why explicit MIDI tempo currently has priority

Explicit MIDI tempo is direct timing metadata rather than an audio estimate. When it is trustworthy, it provides a precise tempo map and can include tempo changes.

However, direct metadata is not automatically correct for the real audio.

A MIDI file that says 70 BPM can still accompany audio whose musical pulse is understood as 140 BPM. For this reason, the BPM display should be treated as the application's current timing authority, not as proof that every imported file agrees.

When working with WAV and MIDI together, listen and visually compare them. Hermes synchronization and manual MIDI editing may still be needed.

## Multiple MIDI tempo maps

A project may contain more than one MIDI track with explicit tempo information.

When those maps disagree, DAWHermes currently:

- uses the first suitable MIDI track in project order;
- reports a concise non-modal conflict diagnostic;
- does not silently combine contradictory tempo maps.

The WAV files still play at original speed.

## The 120 BPM fallback

120 BPM is used when DAWHermes has neither:

- a suitable explicit MIDI tempo map; nor
- a confident WAV tempo estimate.

The fallback keeps the ruler, MIDI playback and Loop calculations usable.

It does **not** mean that the music has been proven to be 120 BPM.

When the BPM display remains at 120 for unknown material, it may simply mean that no stronger tempo source was available.

## Half-time and double-time

Rhythms can often be interpreted at two related tempo levels.

Examples:

- 60 and 120 BPM;
- 70 and 140 BPM;
- 75 and 150 BPM;
- 90 and 180 BPM.

### Why 70 and 140 can describe the same accents

Imagine a 140 BPM rhythm in which every second kick is much stronger.

A simple detector may hear the strong pattern like this:

```text
STRONG - weak - STRONG - weak
```

The strongest repetition occurs every two beats. A naive method may therefore report 70 BPM even though weaker beats support a 140 BPM quarter-note pulse.

### How DAWHermes improves this decision

The current WAV detector does not simply choose one strongest repeating interval.

It compares meaningful tempo candidates and their half-time/double-time relationships using evidence such as:

- regular beat-grid support;
- intermediate onsets;
- alternating strong and weak accents;
- the difference between competing candidate scores;
- the amount and duration of rhythmic evidence.

This allows a 140 BPM track with alternating accents to remain 140 BPM instead of being confidently reduced to 70 BPM.

### Protecting genuine slow music

DAWHermes does not automatically double every result below 80 BPM.

A real 70 BPM pattern with no convincing intermediate beats should remain near 70 BPM.

When the evidence cannot clearly distinguish the two interpretations, the detector may remain unconfident rather than reporting a confidently wrong answer.

## Confident and unconfident WAV detection

A WAV BPM result is used only when the detector has enough rhythmic evidence and sufficient confidence.

Material may remain unconfident when it contains:

- silence;
- a steady tone;
- very few clear events;
- irregular free timing;
- a genuinely unresolved half-time/double-time relationship;
- a short or weakly rhythmic passage.

An unconfident result is not promoted above the 120 BPM fallback.

## Project order matters

Tempo source priority uses project order.

For example:

```text
Track 1: MIDI with explicit 128 BPM
Track 2: MIDI with explicit 140 BPM
Track 3: WAV confidently detected at 140 BPM
```

DAWHermes uses the explicit tempo from Track 1 and reports that conflicting MIDI tempo information exists.

It does not use the WAV result simply because the WAV may sound more convincing.

This behaviour may be refined in a future milestone, but the current manual must reflect the actual application.

## Tempo maps and tempo changes

A MIDI tempo map can contain more than one tempo event.

Example:

- 120 BPM at the beginning;
- 128 BPM after a transition;
- 140 BPM in a final section.

DAWHermes can use imported explicit MIDI tempo events when mapping MIDI beats to project seconds.

A single detected WAV BPM is only an overall estimate. It is not a complete studio beat grid and does not map local tempo changes throughout a performance.

## BPM and Loop

The Loop range is stored in musical beat positions.

Tempo is used to determine where those beat positions occur in seconds.

This provides musically meaningful ranges such as four bars or eight bars.

Because WAV speed is unchanged, a badly chosen tempo can make the beat-based Loop boundary feel misaligned with the audio. The audio itself is not stretched to fit the boundary.

## BPM and MIDI synchronization

Two separate problems are often confused:

### Wrong tempo interpretation

The grid itself does not represent the music correctly.

Example: the project uses 70 BPM while the intended pulse is 140 BPM.

### Correct tempo but shifted notes

The BPM may be correct, but MIDI notes begin earlier or later than events in the WAV.

This is a synchronization problem. Use **Hermes -> Synchronize MIDI with WAV...** or edit the notes manually.

Changing BPM and aligning note timing are not the same operation.

## Practical examples

### A real 90 BPM WAV

A clear 90 BPM rhythm should produce a detected result near 90 BPM when no explicit MIDI tempo takes priority.

### A 140 BPM WAV with strong alternating accents

The detector compares 70 and 140 BPM evidence. Weak but regular intermediate beats can confirm the 140 BPM pulse.

### MIDI says 70, WAV sounds like 140

If the first suitable MIDI track contains explicit 70 BPM metadata, the current project-tempo priority uses that MIDI information even when the WAV detector could identify 140 BPM.

The BPM display therefore reflects the chosen MIDI authority. It does not prove that the audio is truly 70 BPM.

### No reliable tempo source

When MIDI has no explicit tempo and the WAV is not rhythmically clear enough, DAWHermes uses 120 BPM so that MIDI and the ruler remain operational.

## Set / Fix BPM status

A **Set / Fix BPM...** Hermes command may appear in the interface, but the real Hermes processing path for this command is not implemented yet.

Do not treat it as an available correction workflow.

At present, BPM can be supplied by imported MIDI metadata, estimated from WAV or replaced by the fallback. A full user-controlled BPM correction workflow is planned for later.

## What to check when tempo seems surprising

Without treating this as a complete troubleshooting guide, verify the musical context:

- Is there a MIDI track with explicit tempo metadata?
- Is it the first suitable MIDI track in project order?
- Does the audio feel like half-time or double-time?
- Is the material rhythmically clear enough for audio detection?
- Does the BPM display stay at the 120 fallback?
- Is the real problem tempo, or are the MIDI notes simply shifted?

Use your ears together with the ruler and waveform. BPM analysis is an audition and preparation aid, not a replacement for a full manual beat-grid process.

## Important limitations

- WAV BPM is an estimate, not a guaranteed embedded value.
- One detected BPM is not a detailed beat map.
- WAV playback speed is never changed by BPM detection.
- Explicit MIDI tempo currently takes priority over detected audio tempo.
- Conflicting MIDI maps are not merged.
- Hermes Set / Fix BPM is not implemented.
- Time stretching and beat warping are not available.

---

[Previous: Composer Assistant](04_COMPOSER_ASSISTANT.md) | [Back to contents](README.md) | [Next: MIDI Editor](06_MIDI_EDITOR.md)
