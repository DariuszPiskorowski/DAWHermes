# 9. Export and File Exchange

[Previous: Audio Device Settings](08_AUDIO_DEVICE_SETTINGS.md) | [Back to contents](README.md) | [Next: Common Workflows](10_COMMON_WORKFLOWS.md)

## Why export matters

DAWHermes is currently a preparation and correction workspace rather than the final production destination.

After editing or generating MIDI, export creates a standard MIDI file that can be opened in another music program such as Cubase or Reaper.

Export is also important because DAWHermes does not yet have a project save/load format.

## Export Selected MIDI Track

Choose **File -> Export Selected MIDI Track...**.

The command exports one selected, non-empty MIDI track.

### Step by step

1. Select the MIDI track you want to keep.
2. Confirm that it contains notes.
3. Choose **File -> Export Selected MIDI Track...**.
4. Choose a destination folder.
5. Enter a clear filename.
6. Save the file.
7. Open the exported MIDI in the destination DAW when needed.

## What is exported

DAWHermes exports the edited MIDI currently held in project memory.

The result can preserve:

- note pitch;
- note velocity;
- MIDI channel;
- note start;
- note duration;
- note-off timing;
- track name;
- PPQ timing resolution when available;
- tempo map when available;
- time signatures when available;
- MIDI file type information when available.

When source metadata is missing, DAWHermes uses sensible standard fallback values for the MIDI file.

## What is not exported

The selected-track MIDI export does not include:

- WAV audio;
- other project tracks;
- Mute/Solo state;
- the Loop range;
- DAWHermes panel layout;
- application settings;
- a DAWHermes project file;
- comparison overlays;
- internal note IDs;
- audio-device configuration.

## Source-file safety

Export does not overwrite the original imported MIDI.

DAWHermes writes a new file at the destination you choose.

The original WAV and MIDI source files remain unchanged.

Editing inside DAWHermes also works on project memory rather than rewriting the imported `.mid` file.

## Choosing a destination and filename

Use filenames that explain the musical role and processing stage.

Examples:

```text
Bass_Hermes_Repaired_v1.mid
Bass_Hermes_Synced_Final.mid
Drums_GM_Corrected.mid
Synth_Lead_Manual_Edit.mid
```

Avoid repeatedly overwriting the only exported copy until you are certain the new version is better.

## Exporting a Hermes result

Hermes creates new MIDI tracks inside DAWHermes.

To export one:

1. Compare the Hermes result with the source track and WAV.
2. Correct obvious note problems in the Piano Roll.
3. Select the finished result track.
4. Choose **File -> Export Selected MIDI Track...**.
5. Save it with a name that identifies the Hermes operation.

For grouped drum output, export each required child MIDI track separately unless you intentionally created one single drum track.

## Exporting for Cubase

Cubase is the intended final production and mixing destination.

The current workflow is standard MIDI exchange:

1. Prepare and correct MIDI in DAWHermes.
2. Export the selected MIDI track.
3. Open or import the `.mid` file in Cubase.
4. Assign the final VST instrument.
5. Continue arrangement, sound design, mixing and effects in Cubase.

DAWHermes does not yet provide a Cubase-specific package or one-click transfer.

Check the following after import into Cubase:

- the intended tempo context;
- track placement;
- MIDI channel;
- drum mapping;
- note lengths;
- the assigned instrument;
- alignment with the source WAV stems.

## Exporting for Reaper

Reaper can be used as a quick audition or reference environment.

The workflow is similar:

1. Export MIDI from DAWHermes.
2. Import it into Reaper.
3. Assign an instrument or use the existing Reaper setup.
4. Compare it with reference audio.

Reaper is not a hidden dependency of DAWHermes and is not required for normal DAWHermes operation.

## Tempo considerations during exchange

An exported MIDI file can carry tempo information when the selected track has valid source metadata.

However, MIDI tempo and real WAV tempo can disagree.

Before final production:

1. Review the [BPM source priority](05_BPM_TEMPO_AND_TIMING.md#how-dawhermes-chooses-project-tempo).
2. Check the exported MIDI against the reference WAV.
3. Confirm the destination DAW project tempo.
4. Avoid assuming that imported MIDI tempo is automatically the correct audio tempo.

## Drum mapping considerations

A drum MIDI file may be rhythmically correct but use note numbers intended for a different drum instrument.

Before or after export, confirm whether the destination expects:

- General MIDI;
- UJAM Kandy mapping;
- Sitala mapping;
- a custom mapping.

Wrong mapping changes which drum sound is triggered, not the timing of the note.

## Current file-exchange status

### Available now

- standard selected-track MIDI export;
- imported WAV references;
- imported MIDI;
- standard manual transfer of exported MIDI to another DAW.

### Optional future integrations, not available yet

- ACE Studio exchange;
- a possible local ACE-Step workflow;
- Cubase-specific exchange/export;
- multi-track export package;
- automatic stem and MIDI bundle;
- one-click transfer;
- project save/load;
- audio export from the DAWHermes arrangement.

## ACE Studio is an optional external service

ACE Studio is not a permanent built-in DAWHermes tool and it is not required for normal DAWHermes operation.

It is a separate commercial AI music workstation. A musician may choose to use it for AI vocals, AI instruments or other ACE Studio features, but another musician can use DAWHermes without installing or paying for ACE Studio.

Any future DAWHermes connection must therefore remain optional. DAWHermes must still launch, edit, play, use Hermes and export MIDI when ACE Studio is absent.

The possible future role is:

1. prepare or correct MIDI in DAWHermes;
2. send or exchange selected material with ACE Studio;
3. use ACE Studio for an optional AI vocal or instrument performance;
4. bring the resulting MIDI or audio back into the wider production workflow;
5. continue final production in Cubase.

This workflow is only a design option. DAWHermes does not currently send tracks directly to ACE Studio, control an ACE Studio project, or receive processed results automatically.

## Possible ACE Studio connection methods

ACE Studio currently documents more than one integration surface, but DAWHermes has not selected one.

Possible future methods include:

- ordinary MIDI and WAV file exchange;
- ACE Bridge 2, which connects ACE Studio with compatible DAWs through a plug-in;
- the experimental ACE Studio MCP server, if it becomes stable and suitable for the DAWHermes workflow;
- a private or account-specific API, but only when access and permitted use have been confirmed.

The manual must not promise a public ACE Studio API connection. At the current manual baseline, there is no verified general public REST API or documented free ACE Studio API tier that DAWHermes can depend on.

The project owner may have subscription-based or experimental integration access. That access must be verified in the actual ACE Studio account and documentation before implementation begins.

## ACE Studio and ACE-Step are different projects

The open-source **ACE-Step** music-generation model on GitHub is not a free edition of ACE Studio.

They solve different problems:

- **ACE Studio** is a separate desktop music workstation and commercial service with its own voices, instruments, project interface and account features.
- **ACE-Step** is an open-source local music-generation model that can generate music on the user's own computer.

ACE-Step 1.5 provides its own local HTTP API and can run without an ACE Studio subscription. That does not make it an ACE Studio API, and it does not automatically provide ACE Studio's voices, interface, Bridge workflow or project features.

A future DAWHermes milestone could evaluate ACE-Step as a separate optional local generator. That would be a different integration decision from connecting DAWHermes to ACE Studio.

Neither ACE Studio nor ACE-Step is integrated into DAWHermes now.

## Why the manual mentions ACE now

ACE is mentioned so users understand the intended direction without mistaking it for a required dependency.

The correct current interpretation is:

- ACE Studio may become an optional external destination or service;
- ACE-Step may be evaluated separately as an optional local model;
- neither is part of the current DAWHermes installation;
- no ACE-specific menu command is currently available;
- the final connection method has not been chosen.

This chapter will be rewritten with exact operating steps only after a real ACE integration has been implemented and manually accepted.

## Cubase-specific export status

Cubase-specific exchange is planned, but it is not implemented.

The current reliable method is standard MIDI export followed by manual import in Cubase.

Future Cubase exchange may include stronger project context, naming, tempo and track-role handling. This manual will be updated when the real behaviour exists.

## No audio export yet

DAWHermes currently plays imported WAV files but does not render or export a mixed WAV project result.

For final audio production, use Cubase or another production DAW.

## No project save/load yet

Closing the application does not currently create a reusable DAWHermes project file.

Before closing:

- export important edited MIDI;
- record which WAV sources were used;
- use clear filenames for results;
- keep original source files safely organised.

## Practical export checklist

Before exporting a MIDI track, confirm:

- the correct track is selected;
- the track contains notes;
- pitch errors have been checked;
- note starts and lengths are correct;
- velocity is sensible;
- the result has been heard against the reference WAV;
- the intended tempo interpretation is understood;
- drum mapping is correct when relevant;
- the destination filename is clear.

## Important limitations

- Export is one MIDI track at a time.
- Empty MIDI tracks cannot be exported as musical results.
- WAV audio is not included.
- DAWHermes project state is not saved in the MIDI file.
- There is no Cubase-specific exchange yet.
- There is no ACE Studio or ACE-Step integration yet.
- There is no full project render or stem export.

---

[Previous: Audio Device Settings](08_AUDIO_DEVICE_SETTINGS.md) | [Back to contents](README.md) | [Next: Common Workflows](10_COMMON_WORKFLOWS.md)
