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

### Planned major integrations, not available yet

- first-class ACE Studio integration;
- Cubase-specific exchange/export;
- multi-track export package;
- automatic stem and MIDI bundle;
- project save/load;
- audio export from the DAWHermes arrangement.

## ACE Studio is a planned first-class integration

ACE Studio integration is one of the main reasons DAWHermes is being developed. It is not a minor afterthought.

The intended long-term workflow is to let DAWHermes prepare, repair, edit and organise musical material, then exchange selected MIDI, audio and project context with ACE Studio for AI vocals and AI instrument performances. The result should return to the same wider production workflow and continue toward final production in Cubase.

However, ACE Studio itself is a separate commercial application and service. It is not bundled with DAWHermes, and its licence is not included with DAWHermes.

A user who wants the ACE Studio part of the workflow will need:

- ACE Studio installed separately;
- a valid ACE Studio membership or licence;
- access to the required ACE Studio integration feature;
- any account permissions required by ACE Studio at that time.

The rest of DAWHermes should remain usable when ACE Studio is not installed, but the ACE-specific workflow will naturally be unavailable.

## The current ACE Studio integration surface

ACE Studio 2.0 currently provides two important documented integration paths:

### ACE Bridge 2

ACE Bridge 2 is a VST3, AU and AAX plug-in installed with ACE Studio. It connects a compatible DAW to ACE Studio for MIDI and audio synchronization.

DAWHermes does not host VST3 plug-ins yet, so direct ACE Bridge 2 use must wait until the VST3-hosting milestone or another deliberate bridge design is implemented.

### ACE Studio MCP Server

ACE Studio 2.0 also includes an experimental local MCP server. When enabled inside ACE Studio, it exposes the current ACE Studio project to compatible AI clients and agents through a local Streamable HTTP endpoint.

This is the most relevant currently documented programmatic integration path for the future DAWHermes AI assistant. It could allow the DAWHermes assistant to inspect and edit an open ACE Studio project without pretending that ACE Studio is part of the DAWHermes executable.

The MCP server is still marked experimental. Its tools, behaviour, limits and compatibility may change before it becomes stable. DAWHermes must therefore treat its protocol as versioned external integration rather than an unchanging internal API.

## Confirmed access for the DAWHermes project

The DAWHermes project owner has a paid ACE Studio installation and has personally confirmed that the **MCP Server** option is present and can be enabled in ACE Studio settings.

This removes the main account-access uncertainty for the planned integration. DAWHermes does not need to depend on an unverified free ACE Studio tier for development of its primary ACE workflow.

Before implementation begins, the integration milestone must still inspect and record:

- the installed ACE Studio version;
- the local MCP endpoint and connection procedure;
- the tools exposed by that version of the MCP server;
- which operations require an open ACE Studio project;
- how MIDI, lyrics, timing, track roles and generated results are represented;
- whether any relevant feature differs between ACE Studio membership levels;
- how protocol changes will be handled safely.

The presence of the MCP switch confirms availability, but it does not by itself prove every desired DAWHermes operation is already exposed by the current MCP tool set.

## Is there a free ACE Studio version with API access?

At the current manual baseline, the official ACE Studio documentation does not list a permanent free ACE Studio desktop plan with MCP or API access.

Official ACE Studio support states that using ACE Studio requires a membership obtained through a subscription, lifetime purchase or voucher. The public pricing page lists paid Artist and Artist Pro plans rather than a continuing free desktop tier.

The ACE Studio MCP server is not presented as a separate cloud API subscription. It runs locally inside ACE Studio 2.0. Access therefore appears to depend on having a working licensed ACE Studio installation rather than purchasing an additional API package.

ACE Studio also gives registered users monthly AI credits for some web and generative features. Those credits do not by themselves prove that a free user receives full ACE Studio desktop access or access to the local MCP server.

For DAWHermes, the free-tier question is no longer a development blocker because the project owner has confirmed MCP Server access in the paid installation. Future public distribution must still explain that users need their own compatible ACE Studio installation and account for the ACE-specific workflow.

## ACE Studio and ACE-Step are different projects

The open-source **ACE-Step** music-generation model on GitHub is not a free edition of ACE Studio.

They solve different problems:

- **ACE Studio** is a commercial desktop AI music workstation with AI vocal and instrument workflows, its own project interface, ACE Bridge 2 and an experimental MCP server.
- **ACE-Step** is an open-source local foundation model for generating music on the user's own computer.

ACE-Step provides its own local programmatic interfaces, including a local HTTP API in current releases. This does not make it an ACE Studio API, and it does not automatically provide ACE Studio voices, instruments, project editing, Bridge synchronization or Studio account features.

A future DAWHermes milestone may evaluate ACE-Step separately as a local generation engine. That would be a second integration decision, not a free replacement for the planned ACE Studio connection.

Neither ACE Studio nor ACE-Step is integrated into DAWHermes at the current manual baseline.

## Planned ACE Studio workflow

The intended direction is:

1. prepare or correct MIDI in DAWHermes;
2. select the musical work region with Loop;
3. use the DAWHermes Composer Assistant to understand the project context;
4. send or apply suitable MIDI, lyrics, track and timing information to ACE Studio;
5. generate or refine an AI vocal or AI instrument performance in ACE Studio;
6. audition the returned result with the DAWHermes project;
7. edit or repeat the operation as needed;
8. export the accepted material for final production in Cubase.

The exact controls and transfer method have not been implemented yet. They must be documented only after the real integration exists and has been manually accepted.

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