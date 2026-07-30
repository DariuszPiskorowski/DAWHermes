# DAWHermes User Manual

Beginner-friendly guide to the currently implemented DAWHermes workflow.

**Language:** English  
**Accepted application baseline:** `main`, commit `d5eb2a75fd1a05f759a0f7757df3e72b12b5851a`

**Milestone 5.1 status:** implemented and awaiting manual user acceptance

DAWHermes is developing quickly. This manual describes features that are present in the application now. Future workflows are clearly marked as **Planned** and must not be treated as available commands.

The manual is intentionally separate from the developer documentation in `docs/`. It explains musical purpose and practical operation rather than source code or software architecture.

## Suggested reading order

1. [Getting Started](01_GETTING_STARTED.md)
2. [Tracks, MIDI and Audio](02_TRACKS_MIDI_AND_AUDIO.md)
3. [Transport, Loop and the Work Region](03_TRANSPORT_LOOP_AND_WORK_REGION.md)
4. [Composer Assistant](04_COMPOSER_ASSISTANT.md)
5. [BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md)
6. [MIDI Editor](06_MIDI_EDITOR.md)
7. [VST3 Instruments](07_VST3_INSTRUMENTS.md)
8. [Hermes Tools](08_HERMES_TOOLS.md)
9. [Audio Device Settings](09_AUDIO_DEVICE_SETTINGS.md)
10. [Export and File Exchange](10_EXPORT_AND_FILE_EXCHANGE.md)
11. [Common Workflows](11_COMMON_WORKFLOWS.md)
12. [Glossary](12_GLOSSARY.md)

## Quick links

- [Import WAV and MIDI files](02_TRACKS_MIDI_AND_AUDIO.md#importing-audio-wav)
- [Play the complete project](03_TRANSPORT_LOOP_AND_WORK_REGION.md#playing-the-project)
- [Mute or Solo tracks](02_TRACKS_MIDI_AND_AUDIO.md#mute-and-solo)
- [Create and use a Loop range](03_TRANSPORT_LOOP_AND_WORK_REGION.md#creating-a-loop-range)
- [Understand the planned Composer Assistant workflow](04_COMPOSER_ASSISTANT.md#planned-work-region-workflow)
- [Read about the planned ACE Studio MCP connection](04_COMPOSER_ASSISTANT.md#planned-ace-studio-connection)
- [Understand where BPM comes from](05_BPM_TEMPO_AND_TIMING.md#how-dawhermes-chooses-project-tempo)
- [Edit MIDI notes](06_MIDI_EDITOR.md)
- [Use a VST3 instrument on a MIDI track](07_VST3_INSTRUMENTS.md)
- [Create drum MIDI with Hermes](08_HERMES_TOOLS.md)
- [Repair bass MIDI against audio](08_HERMES_TOOLS.md)
- [Synchronize MIDI with WAV](08_HERMES_TOOLS.md)
- [Configure the audio output](09_AUDIO_DEVICE_SETTINGS.md)
- [Export corrected MIDI](10_EXPORT_AND_FILE_EXCHANGE.md)
- [Read the complete planned DAWHermes–ACE Studio–Cubase workflow](11_COMMON_WORKFLOWS.md#15-use-the-planned-complete-dawhermesace-studiocubase-workflow)

## What DAWHermes currently does well

DAWHermes currently provides a focused preparation workspace for:

- importing WAV stems and MIDI tracks;
- viewing audio waveforms and MIDI notes together;
- playing the complete MIDI/WAV project;
- isolating tracks with Mute and Solo;
- repeatedly auditioning a musical passage with Loop;
- editing MIDI notes in the Piano Roll;
- assigning independent VST3 instruments to MIDI tracks for audition;
- converting drum audio into MIDI with Hermes;
- repairing bass MIDI against a reference WAV;
- synchronizing MIDI timing with a WAV reference;
- exporting an edited MIDI track for further work in another DAW.

## Major planned direction

ACE Studio is a first-class planned integration for DAWHermes. It is one of the main workflows for which the application is being developed, together with Composer Assistant and final production in Cubase.

The project owner has a paid ACE Studio installation and has confirmed that the **MCP Server** option is present and can be enabled. This removes the main account-access uncertainty for development, although DAWHermes still has to inspect the actual MCP endpoint and tool set before implementing the connection.

ACE Studio remains a separate commercial application. It is installed and licensed separately, and DAWHermes must remain usable without it.

## What is not available yet

The following are planned or outside the current application scope:

- recording and input monitoring;
- mixer faders, pan, effects, sends and buses;
- full Composer Assistant music generation inside DAWHermes;
- connection to the ACE Studio MCP Server;
- Hermes Set / Fix BPM processing;
- project save/load format;
- audio clip editing, time stretching and beat warping;
- Cubase-specific export.

Standard MIDI export is available and can already be used to move corrected MIDI into Cubase, Reaper or another compatible program.

---

[Next: Getting Started](01_GETTING_STARTED.md)
