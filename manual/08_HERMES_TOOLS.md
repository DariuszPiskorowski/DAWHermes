# 8. Hermes Tools

[Previous: VST3 Instruments](07_VST3_INSTRUMENTS.md) | [Back to contents](README.md) | [Next: Audio Device Settings](09_AUDIO_DEVICE_SETTINGS.md)

## What Hermes is

Hermes is the built-in analysis and correction system used by DAWHermes.

It works with existing audio and MIDI evidence. Its job is to help recover, repair and align musical information.

Hermes is useful when:

- a WAV contains drums but no usable drum MIDI exists;
- bass MIDI is present but does not accurately follow the bass audio;
- MIDI notes are musically correct but begin too early or too late compared with the WAV;
- you need editable MIDI derived from an audio reference.

Hermes processing runs inside the DAWHermes workflow. It is not intended to be a second user-facing application.

## Hermes and Composer Assistant are different

Hermes analyses and corrects existing material.

Composer Assistant is intended to propose new musical content inside a selected work region.

Examples:

- **Hermes:** detect drum events in a WAV and create MIDI.
- **Hermes:** repair bass MIDI against the recorded bass.
- **Hermes:** synchronize MIDI timing to audio.
- **Composer Assistant, planned:** write a new transition or continue a melody.

See [Composer Assistant](04_COMPOSER_ASSISTANT.md) for the planned creative workflow.

## How Hermes commands open

Hermes commands are available through the **Hermes** menu and deliberate track context menus.

Selecting a track does not automatically open Hermes.

A dialog appears only after you intentionally choose a Hermes command.

The current command structure includes:

```text
Hermes
├── Drums
│   ├── Make MIDI from WAV...
│   └── Drum Mapping...
├── Bass
│   └── Repair MIDI against WAV...
├── Synchronize MIDI with WAV...
└── Set / Fix BPM...
```

The final command is visible but its real processing path is not implemented yet.

# Drums — Make MIDI from WAV

## What it does

**Drums -> Make MIDI from WAV...** analyses a selected drum WAV and creates editable MIDI events representing detected drum hits.

The source WAV remains unchanged.

## Why it exists

A drum stem is useful for listening, but it is difficult to edit individual hits inside recorded audio.

MIDI makes it possible to:

- replace the original drum sound with another instrument;
- correct missed or unwanted hits;
- separate kick, snare and cymbal-type layers;
- change velocities;
- quantize selected events;
- export the result to Cubase or another DAW.

## Preparing the source

Use the cleanest drum-focused WAV available.

A separated drum stem usually gives better evidence than a complete mix containing vocals, bass and synths.

### Step by step

1. Import the WAV with **File -> Import Audio as Track...**.
2. Click the audio track to select it.
3. Choose **Hermes -> Drums -> Make MIDI from WAV...**.
4. Review the drum options.
5. Choose the result layout.
6. Choose a detection profile and detection mode.
7. Choose the target drum mapping.
8. Confirm with **Create MIDI**.
9. Wait for background processing to finish.
10. Inspect the generated MIDI tracks in the Timeline and Piano Roll.
11. Solo or mute layers to check them against the reference WAV.

## Result layout

### Separate MIDI tracks

Creates separate result tracks for enabled drum layers.

Use this when you want direct control over each layer without a parent group.

### One grouped multitrack

Creates a group track containing child MIDI tracks.

Use this when you want:

- organised drum layers;
- one group Mute or Solo control;
- individual editing of kick, snare and other children;
- a clear hierarchy for export preparation.

### One single drum track

Places detected drum events on one MIDI track.

Use this when the destination instrument expects a conventional multichannel or mapped drum performance on a single track.

## Detection profile

The available profiles are:

- **Conservative**
- **Balanced**
- **Sensitive**

A practical interpretation is:

- Conservative aims to avoid doubtful extra hits and may miss weak events.
- Balanced is a middle starting point.
- Sensitive is more willing to include weak events and may produce more false detections.

Always listen to the result. No profile guarantees perfect separation for every drum recording.

## Detection mode

The dialog offers:

- **Multi-detector**
- **Global**

Multi-detector is intended for analysing drum roles through more than one focused detection path. Global uses a broader combined interpretation.

The best choice depends on the recording. Compare the generated result rather than assuming one mode is always superior.

## Target mapping

The target mapping decides which MIDI note numbers represent drum sounds.

Current choices include:

- **UJAM Kandy**
- **General MIDI**
- **Sitala**
- **Custom**

Choose the mapping that matches the drum instrument you plan to use later.

A correct rhythm with the wrong mapping may trigger the wrong drum sounds, such as a snare note playing a tom.

## C1 MIDI note

The dialog allows the MIDI note number used as C1 to be specified.

Different music programs and instruments sometimes label octaves differently even when the numeric MIDI note is the same. This setting helps align the generated mapping with the intended destination.

When uncertain, use the current default and verify the result in the target instrument.

## Create empty enabled layers

When enabled, DAWHermes can create an enabled drum layer even when Hermes detected no notes for that layer.

This can be useful when you want a consistent track structure, for example:

```text
Drums Group
├── Kick
├── Snare
├── Hi-Hat
└── Cymbals
```

An empty layer is not a failure by itself. It may mean:

- the selected source contains no clear events of that type;
- the profile was too conservative;
- the event is masked by other sounds;
- the enabled layer was created intentionally for structural consistency.

## Drum Mapping

**Hermes -> Drums -> Drum Mapping...** opens the drum-mapping interface.

Use it to inspect or prepare how detected drum roles correspond to MIDI notes and target instruments.

Mapping changes meaning, not timing. A mapping tells an instrument which sound a MIDI note should trigger.

## Checking the drum result

After processing:

1. Keep the source WAV available as reference.
2. Solo the generated group or MIDI track.
3. Listen for missing and extra hits.
4. Compare against the WAV in a short Loop.
5. Inspect note positions in the Piano Roll.
6. Correct obvious mistakes manually.
7. Quantize only when the original groove should follow a strict grid.
8. Export the final MIDI track or tracks required by your destination workflow.

## Undo and Redo

A generated Hermes drum result can be removed with Undo and restored with Redo.

Redo restores the stored result. It does not run drum analysis again.

# Bass — Repair MIDI against WAV

## What it does

**Bass -> Repair MIDI against WAV...** compares an existing bass MIDI candidate with a reference bass WAV and creates a repaired MIDI copy.

The source WAV and source MIDI remain unchanged.

## Why it exists

Bass MIDI recovered from audio or obtained from another source may contain:

- wrong pitches;
- missing notes;
- extra notes;
- inaccurate note lengths;
- notes that do not match the recorded bass phrase.

Hermes uses the WAV as evidence and the MIDI as an editable candidate.

## Required selection

Bass repair requires exactly the useful audio/MIDI context:

- one selected audio track with an existing readable WAV source;
- one selected non-empty MIDI track with valid source information.

### Step by step

1. Import the bass WAV.
2. Import the bass MIDI candidate.
3. Select both tracks using multi-selection.
4. Choose **Hermes -> Bass -> Repair MIDI against WAV...**.
5. Review the displayed reference WAV and MIDI candidate.
6. Accept or edit the proposed result-track name.
7. Choose **Run**.
8. Wait for background processing.
9. Inspect the new repaired MIDI track.
10. Compare source MIDI, repaired MIDI and WAV using Mute/Solo and Loop.

## What the result is

Hermes creates a new MIDI track rather than overwriting the candidate.

A typical name is based on the original track with a suffix such as:

```text
Hermes Bass Repaired
```

Keeping both tracks allows direct comparison and protects the source candidate.

## What repair does not guarantee

Repair is an analysis result, not proof of musical perfection.

You should still check:

- pitch choices;
- note starts;
- note lengths;
- repeated notes;
- octave interpretation;
- transitions between notes.

Use the Piano Roll for final manual correction.

# Synchronize MIDI with WAV

## What it does

**Synchronize MIDI with WAV...** adjusts a MIDI candidate so its timing follows a selected WAV reference more closely.

It creates a new synchronized MIDI track and preserves the original sources.

## Why synchronization is separate from repair

A MIDI track may contain the correct notes but still feel wrong because:

- all notes begin too early;
- all notes begin too late;
- local events drift away from the audio;
- note starts do not follow recorded attacks;
- an extracted performance needs role-specific timing treatment.

Repair focuses on the musical MIDI candidate against audio evidence. Synchronization focuses on timing alignment.

## Required selection

Synchronization requires:

- one selected audio track with an existing WAV source;
- one selected non-empty MIDI track with valid source information.

### Step by step

1. Import the reference WAV.
2. Import or create the MIDI candidate.
3. Select the WAV and MIDI tracks together.
4. Choose **Hermes -> Synchronize MIDI with WAV...**.
5. Choose the musical role.
6. Decide whether to preserve the source MIDI tempo map.
7. When tempo preservation is disabled, enter the BPM override.
8. Accept or edit the result-track name.
9. Choose **Run**.
10. Compare the new synchronized MIDI with the reference WAV.

## Musical role

Available roles include:

- Bass
- Drums
- Synth
- Guitar
- Other

The role helps the synchronization process interpret the expected event behaviour.

A drum hit, bass note and sustained synth phrase do not have identical timing characteristics.

## Preserve tempo map from source MIDI

This option is enabled by default.

Use it when the source MIDI contains trustworthy tempo information that should remain attached to the result.

## BPM override

When tempo-map preservation is disabled, the dialog accepts a BPM override.

This supplies a fixed tempo assumption for the synchronized result.

The override is not the same as changing WAV speed. The WAV remains the timing reference and still plays at its original speed.

## Result track

DAWHermes creates a new MIDI track, often with a name ending in:

```text
Hermes Synced
```

The original MIDI remains available for comparison.

## Checking synchronization

1. Create a short Loop around a clear musical passage.
2. Play the WAV and synchronized MIDI together.
3. Alternate Mute between original and synchronized MIDI.
4. Listen to attacks, not only sustained sound.
5. Inspect note starts in the Piano Roll.
6. Correct any remaining local problems manually.

# Set / Fix BPM

## Current status

The **Set / Fix BPM...** command does not yet have a real integrated Hermes processing path.

It must not be treated as an available production function.

DAWHermes currently chooses tempo from imported MIDI metadata, confident WAV detection or the 120 BPM fallback. See [BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md).

A future milestone will need to define what Set / Fix BPM changes, which track or project data it updates and how the result is reviewed.

# Background processing and project safety

Hermes drum, bass and synchronization work runs outside normal real-time playback processing.

The application remains responsive while the job is performed.

Successful results are inserted directly as DAWHermes MIDI tracks. You do not need to locate temporary intermediate MIDI files.

For successful bass and synchronization jobs, temporary job data is cleaned up. When a job fails, diagnostic information may be retained so the failure can be investigated.

## Source safety

Hermes operations are designed to preserve source material.

They create new project MIDI results and do not overwrite the selected WAV or source MIDI file.

## Important limitations

- Audio analysis can make mistakes.
- Generated or repaired MIDI still requires listening and inspection.
- Drum separation quality depends heavily on the source.
- Bass repair requires an existing MIDI candidate as well as WAV.
- Synchronization requires an audio/MIDI pair.
- Set / Fix BPM is not implemented.
- Hermes does not create new compositional ideas in the role intended for Composer Assistant.
- Final instrument sound must be chosen in Cubase or another production environment because DAWHermes currently uses a simple audition synth.

---

[Previous: VST3 Instruments](07_VST3_INSTRUMENTS.md) | [Back to contents](README.md) | [Next: Audio Device Settings](09_AUDIO_DEVICE_SETTINGS.md)
