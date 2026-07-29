# DAWHermes User Manual

Beginner-friendly guide to the currently implemented DAWHermes workflow.

**Language:** English  
**Application baseline:** Milestone 4.1, commit `ff2b2118cd5bb88196c8a6ea02558c8bc536922a`  
**Status:** draft for user review

DAWHermes is developing quickly. This manual describes features that are present in the application now. Future workflows are clearly marked as **Planned** and must not be treated as available commands.

The manual is intentionally separate from the developer documentation in `docs/`. It explains musical purpose and practical operation rather than source code or software architecture.

## Suggested reading order

1. [Getting Started](01_GETTING_STARTED.md)
2. [Tracks, MIDI and Audio](02_TRACKS_MIDI_AND_AUDIO.md)
3. [Transport, Loop and the Work Region](03_TRANSPORT_LOOP_AND_WORK_REGION.md)
4. [Composer Assistant](04_COMPOSER_ASSISTANT.md)
5. [BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md)
6. [MIDI Editor](06_MIDI_EDITOR.md)
7. [Hermes Tools](07_HERMES_TOOLS.md)
8. [Audio Device Settings](08_AUDIO_DEVICE_SETTINGS.md)
9. [Export and File Exchange](09_EXPORT_AND_FILE_EXCHANGE.md)
10. [Common Workflows](10_COMMON_WORKFLOWS.md)
11. [Glossary](11_GLOSSARY.md)

A troubleshooting chapter is deliberately not included yet. It will be added later, after more of the final workflow has been implemented and real user problems have been collected.

## Quick links

- [Import WAV and MIDI files](02_TRACKS_MIDI_AND_AUDIO.md#importing-audio-wav)
- [Play the complete project](03_TRANSPORT_LOOP_AND_WORK_REGION.md#playing-the-project)
- [Mute or Solo tracks](02_TRACKS_MIDI_AND_AUDIO.md#mute-and-solo)
- [Create and use a Loop range](03_TRANSPORT_LOOP_AND_WORK_REGION.md#creating-a-loop-range)
- [Understand where BPM comes from](05_BPM_TEMPO_AND_TIMING.md#how-dawhermes-chooses-project-tempo)
- [Edit MIDI notes](06_MIDI_EDITOR.md)
- [Create drum MIDI with Hermes](07_HERMES_TOOLS.md#drums--make-midi-from-wav)
- [Repair bass MIDI against audio](07_HERMES_TOOLS.md#bass--repair-midi-against-wav)
- [Synchronize MIDI with WAV](07_HERMES_TOOLS.md#synchronize-midi-with-wav)
- [Configure the audio output](08_AUDIO_DEVICE_SETTINGS.md)
- [Export corrected MIDI](09_EXPORT_AND_FILE_EXCHANGE.md)

## What DAWHermes currently does well

DAWHermes currently provides a focused preparation workspace for:

- importing WAV stems and MIDI tracks;
- viewing audio waveforms and MIDI notes together;
- playing the complete MIDI/WAV project;
- isolating tracks with Mute and Solo;
- repeatedly auditioning a musical passage with Loop;
- editing MIDI notes in the Piano Roll;
- converting drum audio into MIDI with Hermes;
- repairing bass MIDI against a reference WAV;
- synchronizing MIDI timing with a WAV reference;
- exporting an edited MIDI track for further work in another DAW.

## What is not available yet

The following are planned or outside the current application scope:

- recording and input monitoring;
- mixer faders, pan, effects, sends and buses;
- VST3 instrument hosting;
- full Composer Assistant music generation inside DAWHermes;
- Hermes Set / Fix BPM processing;
- project save/load format;
- audio clip editing, time stretching and beat warping;
- ACE Studio exchange;
- Cubase-specific export.

Standard MIDI export is available and can already be used to move corrected MIDI into Cubase, Reaper or another compatible program.

---

[Next: Getting Started](01_GETTING_STARTED.md)
