# 4. Composer Assistant

[Previous: Transport, Loop and the Work Region](03_TRANSPORT_LOOP_AND_WORK_REGION.md) | [Back to contents](README.md) | [Next: BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md)

## What Composer Assistant is

Composer Assistant is the planned DAW-level musical assistant for DAWHermes.

Its purpose is to help create, continue, complete or improve MIDI inside a clearly defined musical passage while considering the surrounding project.

A useful assistant should not receive only an isolated request such as “make a melody.” It should understand:

- where the requested passage begins and ends;
- what happens before and after it;
- which MIDI tracks provide harmony, rhythm and context;
- which musical role needs work;
- the project tempo;
- which existing notes must remain unchanged.

This is why the [Loop work region](03_TRANSPORT_LOOP_AND_WORK_REGION.md#loop-as-the-dawhermes-work-region) is important. It identifies the passage that the assistant will eventually be asked to work on.

## Composer Assistant is not Hermes

Composer Assistant and Hermes have different jobs.

### Composer Assistant

Composer Assistant is intended for musical creation and continuation, for example:

- filling an empty phrase;
- continuing a melody;
- proposing accompaniment;
- improving a transition;
- creating a variation;
- generating MIDI for a selected musical role.

### Hermes

Hermes is a set of analysis, extraction, correction and synchronization tools.

Hermes currently performs tasks such as:

- converting drum audio into MIDI;
- repairing bass MIDI against a WAV reference;
- synchronizing MIDI timing with WAV.

Hermes works from evidence in existing audio and MIDI. Composer Assistant is intended to propose new musical content.

See [Hermes Tools](07_HERMES_TOOLS.md) for the current Hermes workflow.

## Composer Assistant is not the internal MIDI sound

The simple internal MIDI sound is only for audition. It lets you hear pitch, timing, length and velocity.

It does not generate music and it is not Composer Assistant.

## Available now

DAWHermes currently contains a safe connector boundary for an existing Composer Assistant service.

The current connector can:

- store whether the connector is enabled;
- store a host address;
- store a port;
- store a connection timeout;
- optionally restrict the connection to the local computer;
- run a manual reachability probe when the user explicitly requests it.

The connector is disabled by default.

DAWHermes does not attempt a Composer Assistant connection automatically at startup.

The current manual probe only checks whether the configured service can be reached. It does not request music, create MIDI or change the project.

## What the current connector does not do

The current application does not yet:

- send the Loop range to Composer Assistant;
- package the surrounding MIDI context;
- call the music-generation operation from the DAWHermes project;
- receive generated MIDI;
- show candidate versions;
- insert or replace MIDI in the selected region;
- compare, accept or reject an assistant result;
- start or stop the external Composer Assistant service;
- connect Composer Assistant to ACE Studio;
- inspect or edit an ACE Studio project through MCP.

A successful connection probe therefore means only that the configured endpoint is reachable.

It does not mean that the full musical workflow is integrated.

## Planned work-region workflow

The intended DAWHermes workflow is:

1. Identify the weak, empty or unfinished passage.
2. Create a Loop range around it.
3. Listen to the passage repeatedly in context.
4. Select the MIDI track or musical role that needs work.
5. Ask Composer Assistant to work inside the Loop region.
6. Supply surrounding MIDI before and after the range.
7. Supply relevant project tracks as context.
8. Receive one or more MIDI proposals.
9. Audition the result inside the same Loop.
10. Keep, edit or reject the proposal.

This workflow is **planned**. The current application does not yet perform steps 5–10.

## Planned ACE Studio connection

ACE Studio is a first-class planned integration for DAWHermes, not a minor optional experiment in the product roadmap.

The long-term aim is for Composer Assistant to act as the musical coordinator between the DAWHermes project and a separately installed ACE Studio application.

The intended relationship is:

```text
DAWHermes project and Loop work region
        ↓
DAWHermes Composer Assistant
        ↓
ACE Studio MCP Server
        ↓
Open ACE Studio project
```

The DAWHermes project owner has a paid ACE Studio installation and has personally confirmed that the **MCP Server** option is present and can be enabled in ACE Studio settings.

This confirms that the planned integration can be designed around a mechanism that is actually available in the development environment. It does not depend on an unverified free ACE Studio tier.

### What Composer Assistant may eventually send or coordinate

The planned integration may include:

- the Loop work-region boundaries;
- surrounding MIDI context;
- selected melodies or accompaniment tracks;
- lyrics when relevant;
- project tempo and timing information;
- track names and musical roles;
- instructions for an AI vocal or AI instrument performance.

### What may eventually return from ACE Studio

Depending on the tools exposed by the installed MCP server, the workflow may eventually allow:

- creation or editing of MIDI inside an open ACE Studio project;
- preparation of AI vocal or AI instrument parts;
- synchronization of material with the chosen work region;
- retrieval or exchange of results for audition and further work.

These are planned outcomes, not promises about the current ACE Studio MCP tool set.

Before implementation, the ACE integration milestone must inspect the installed ACE Studio version and record:

- the local MCP endpoint and connection method;
- the tools exposed by that exact version;
- which actions require an open ACE Studio project;
- how MIDI, lyrics, timing and track roles are represented;
- which results can be read back or exported;
- whether relevant capabilities differ by membership level;
- how protocol changes will be handled safely.

The presence of the MCP switch confirms access, but it does not prove that every desired operation is already exposed.

See [Export and File Exchange](09_EXPORT_AND_FILE_EXCHANGE.md#ace-studio-is-a-planned-first-class-integration) for the wider DAWHermes–ACE Studio–Cubase workflow.

## ACE Studio remains a separate application

ACE Studio is not bundled with DAWHermes.

A user who wants the ACE-specific workflow will need a compatible ACE Studio installation and their own valid ACE Studio account or licence.

DAWHermes should remain usable without ACE Studio. Features that do not depend on ACE Studio, including Hermes tools, MIDI editing, playback and standard MIDI export, must continue to work independently.

This means ACE Studio is:

- a major planned integration;
- optional for an individual user;
- not a hidden dependency required to launch DAWHermes;
- not included in the DAWHermes licence or installer.

## ACE Studio and ACE-Step are different

The open-source **ACE-Step** model is not a free edition of ACE Studio.

ACE-Step is a separate local music-generation model with its own interfaces. It does not automatically provide the ACE Studio application, ACE Studio projects, voices, instruments, Bridge synchronization or MCP tools.

A future DAWHermes milestone may evaluate ACE-Step separately, but that would be a different integration from the primary ACE Studio connection.

## Why the surrounding context matters

A musical phrase usually makes sense because of what surrounds it.

For example, a new lead phrase may need to follow:

- the current chord progression;
- the bass rhythm;
- the drum groove;
- the note where the previous phrase ended;
- the note expected at the beginning of the next section;
- the energy level of the arrangement.

Sending only the empty bars would remove important information.

The intended assistant workflow therefore uses the chosen work region together with surrounding MIDI and relevant tracks.

## Intended musical operations

The following examples describe the planned purpose of Composer Assistant. They are not current menu commands.

### Fill an empty fragment

Use this when a MIDI track contains a gap that should continue the musical idea.

Example:

- an eight-bar lead contains notes in bars 1–4 and 7–8;
- the Loop region covers bars 5–6;
- Composer Assistant receives the surrounding phrase and proposes a continuation.

### Continue a melody

Use this when the existing melody is good but ends too early.

The assistant should consider the original melodic shape, range, rhythm and harmony rather than beginning a completely unrelated idea.

### Propose accompaniment

Use this when the main melody exists but a supporting MIDI layer is missing.

Possible roles include:

- chord support;
- rhythmic synth;
- bass movement;
- counter-melody;
- simple arpeggio.

### Improve a transition

Use this around the end of one section and the beginning of another.

The work region should include enough music on both sides for the assistant to understand where the transition comes from and where it must lead.

### Create a variation

Use this when a repeated phrase needs a controlled difference rather than a completely new composition.

The original phrase and surrounding project should remain available as context.

## A practical future example

Imagine a trance arrangement with a weak four-bar transition before the chorus.

1. Create a Loop from one bar before the transition to one bar after it.
2. Listen to the full project.
3. Solo the relevant MIDI tracks when you need to inspect harmony or rhythm.
4. Select the MIDI track that should receive the new transition.
5. Ask Composer Assistant for a transition variation inside the central four bars.
6. Keep the outer bars unchanged as context.
7. Audition the returned MIDI repeatedly with the whole project.
8. When the result is intended for an ACE Studio vocal or instrument part, send the accepted context through the planned MCP integration.
9. Review or refine the performance in ACE Studio.
10. Return the accepted material to the wider DAWHermes/Cubase workflow.

Only the Loop, listening, selection and manual editing parts are available now. Assistant generation, ACE Studio MCP exchange and result insertion remain planned.

## Connector settings in plain language

### Enabled

The connector must be enabled before a manual probe can be useful.

Leaving it disabled is the safe choice when you are not using the service.

### Host

The host identifies the computer on which the Composer Assistant service is running.

It may be:

- the same Windows computer;
- another computer on the local or private network;
- a configured Linux machine.

The manual does not prescribe one fixed address because this depends on the user's installation.

### Port

The port identifies the service endpoint on that computer.

The current compatibility connector expects the service configuration used by the existing Composer Assistant setup.

### Timeout

The timeout controls how long the manual probe waits before reporting that no response was received.

A very short timeout may fail on a slow connection. A very long timeout makes a failed probe take longer.

### Loopback-only safety

Loopback means the same computer.

When loopback-only safety is enabled, the connector refuses a remote host. This is useful when the service should never be contacted over a network.

## What the connector does not manage

DAWHermes currently does not:

- install the Composer Assistant model;
- launch its server;
- stop its server;
- configure Reaper;
- move a model between computers;
- manage network or VPN software;
- launch ACE Studio;
- enable or configure the ACE Studio MCP server;
- manage an ACE Studio account or licence.

Those remain separate setup responsibilities until the full DAWHermes integration is implemented.

## Important limitations

- Full AI music generation is not connected to DAWHermes yet.
- The Loop range is not yet sent to the assistant.
- A successful probe does not prove that generation will work.
- No generated MIDI is inserted into the project through this connector yet.
- There is no candidate-management or accept/reject interface yet.
- ACE Studio MCP is confirmed as available in the owner's installation, but DAWHermes does not connect to it yet.
- The current connector exists for safe compatibility and reachability testing.

## How this chapter will change later

When Composer Assistant becomes operational inside DAWHermes, this chapter must be expanded with:

- exact menu or panel commands;
- how to choose the target track;
- how the Loop range is submitted;
- what context is included;
- generation options;
- progress and cancellation;
- result preview;
- insertion, replacement and Undo/Redo;
- exact ACE Studio MCP setup and supported tools;
- practical examples using real project material.

Until then, every generation and ACE Studio workflow in this chapter remains clearly marked as planned.

---

[Previous: Transport, Loop and the Work Region](03_TRANSPORT_LOOP_AND_WORK_REGION.md) | [Back to contents](README.md) | [Next: BPM, Tempo and Timing](05_BPM_TEMPO_AND_TIMING.md)
