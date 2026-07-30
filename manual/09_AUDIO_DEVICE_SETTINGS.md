# 9. Audio Device Settings

[Previous: Hermes Tools](08_HERMES_TOOLS.md) | [Back to contents](README.md) | [Next: Export and File Exchange](10_EXPORT_AND_FILE_EXCHANGE.md)

## Why audio-device settings matter

DAWHermes must send sound to a Windows audio device.

The selected device and its settings affect:

- whether you hear playback;
- which speakers or headphones receive sound;
- the delay between an action and audible output;
- playback stability;
- the available input and output channels.

DAWHermes uses one central audio-device service for the complete project, MIDI audition, WAV playback and Test Output.

## The Audio menu

The top-level **Audio** menu contains:

- **Audio Settings...**
- **Test Output**
- **Restart Audio Device**
- **Audio Device Status...**

These commands open or act only after you deliberately choose them. DAWHermes does not show an audio-settings popup automatically at startup.

# Audio Settings

Choose **Audio -> Audio Settings...** to open the device selector.

The available choices depend on the audio systems and devices reported by Windows and the installed DAWHermes build.

The panel can expose:

- audio device type or system;
- output device;
- input device;
- sample rate;
- buffer size;
- active output channels;
- active input channels.

## Output device

The output device is where DAWHermes sends sound.

Examples may include:

- built-in laptop speakers;
- a USB audio interface;
- headphones connected through an interface;
- another Windows playback device.

Choose the device connected to the speakers or headphones you intend to use.

## Input device

The input device is where future recorded audio would enter the application.

DAWHermes currently allows device input configuration as preparation for later work, but it does not yet provide:

- recording;
- track arming;
- input monitoring;
- input meters.

Selecting an input device therefore does not create a recording workflow.

## Active channels

An audio device may offer several input and output channels.

For ordinary stereo listening, the main left and right output channels should be active.

When no output channel is active, the device may be open but produce no audible project output.

Use only the channels you understand and need. Complex multi-output routing is not yet part of the DAWHermes mixer workflow.

# Sample rate

## What sample rate means

Sample rate describes how many audio samples are processed each second.

Common values include:

- 44.1 kHz;
- 48 kHz;
- higher rates supported by some devices.

The available values come from the selected device.

## How DAWHermes handles different WAV rates

Imported WAV files may use a different sample rate from the active output device.

DAWHermes converts source positions safely for audition playback while keeping the WAV at its original speed.

This is not time stretching and does not change the musical tempo.

## Choosing a sample rate

Begin with a rate that the selected device supports reliably.

When working mainly with 44.1 kHz material, 44.1 kHz is a natural choice. When your wider production workflow uses 48 kHz, 48 kHz may be preferable.

Changing the device sample rate while playback is active requires the audio engine to stop safely and rebuild rate-dependent playback state.

DAWHermes should remain responsive, but playback does not continue uninterrupted through the reconfiguration.

# Buffer size

## What the buffer is

The audio buffer is a small block of audio prepared before it is sent to the device.

A smaller buffer usually means lower delay, but it gives the computer less time to process each block.

A larger buffer usually improves stability, but increases delay.

## Practical trade-off

### Smaller buffer

Possible advantages:

- faster audible response;
- more comfortable live playing in future instrument workflows.

Possible disadvantages:

- higher CPU pressure;
- clicks or dropouts on an overloaded system;
- less tolerance for complex processing.

### Larger buffer

Possible advantages:

- more stable playback;
- more processing time per block.

Possible disadvantages:

- greater delay;
- less immediate response.

## Choosing a buffer size

There is no universal best value.

Use the device's current default as a safe starting point. Change it only when you have a clear reason.

For the current preparation and audition workflow, stability is usually more important than achieving the smallest possible latency.

# Latency

Latency is the delay introduced between an input or action and the corresponding audio result.

The device-status dialog may report input and output latency where the driver provides it.

In the current DAWHermes workflow, latency matters mainly for responsive playback and future recording preparation. DAWHermes does not yet provide live input monitoring or instrument performance through hosted VSTs.

# Test Output

Choose **Audio -> Test Output** to play a short, low-gain test tone.

The test:

- lasts approximately half a second;
- uses a safe low level;
- plays through the available output channels;
- ends automatically;
- does not change the project;
- does not create Undo/Redo history.

Use Test Output after:

- selecting a new output device;
- changing speakers or headphones;
- changing sample rate or buffer size;
- restarting the device;
- confirming that the application can reach the chosen output.

Test Output is unavailable while project playback is playing or paused. Stop the project before using it.

# Restart Audio Device

Choose **Audio -> Restart Audio Device** when the current device needs to be reopened.

Restarting:

- safely silences project playback;
- attempts to restore the selected configuration;
- tries the default output once if restoration fails;
- leaves the application usable even when no device can be opened.

Use Restart after:

- reconnecting a USB interface;
- changing driver settings outside DAWHermes;
- waking a device that stopped responding;
- switching Windows audio hardware.

Restart is not a project reset. It does not delete tracks or MIDI edits.

# Audio Device Status

Choose **Audio -> Audio Device Status...** to open a read-only status dialog.

It can report:

- audio system or device type;
- output device name;
- input device name or `None`;
- sample rate;
- buffer size;
- active input channels;
- active output channels;
- reported input latency;
- reported output latency;
- device open or running state;
- current error information.

The normal bottom status area also shows a concise device summary.

Use the status dialog when you need to confirm what DAWHermes is actually using, rather than what Windows was expected to use.

# Saved settings

DAWHermes saves audio-device configuration in application settings.

At the next launch it attempts to restore the saved state.

If the saved device is no longer available, DAWHermes tries the default output once.

It does not repeatedly open error windows at startup.

# No-device state

DAWHermes can remain open even when no audio device is available.

In this state you may still be able to:

- inspect tracks;
- view waveforms;
- edit MIDI notes;
- use non-playback project functions;
- review settings.

You will not hear project playback until a usable output device is opened.

The status area and application log record the device problem.

# Changing settings during playback

When audio-device configuration changes while the project is active, DAWHermes safely stops playback and active MIDI voices before applying the new device state.

This prevents the old and new device states from competing for the same playback engine.

After configuration completes, start playback again.

# Practical examples

## Switch from laptop speakers to a USB interface

1. Stop project playback.
2. Connect and power the interface.
3. Choose **Audio -> Audio Settings...**.
4. Select the interface as output device.
5. Confirm active left and right outputs.
6. Use **Test Output**.
7. Play the project.

## Increase stability

1. Stop playback.
2. Open **Audio Settings...**.
3. Choose a larger buffer size offered by the device.
4. Use **Test Output**.
5. Play a demanding project section.

A larger buffer may reduce clicks at the cost of greater delay.

## Confirm the active device

1. Choose **Audio -> Audio Device Status...**.
2. Read the output name, sample rate and buffer size.
3. Compare them with the intended device.

# Important limitations

- Device input configuration does not mean recording is implemented.
- There is no track arming or input monitoring.
- There is no multi-bus mixer or output routing matrix.
- DAWHermes does not manage third-party driver control panels.
- Available rates and buffers depend on the selected hardware and driver.
- Device reconfiguration stops active playback safely.
- The current MIDI sound remains a simple audition instrument.

---

[Previous: Hermes Tools](08_HERMES_TOOLS.md) | [Back to contents](README.md) | [Next: Export and File Exchange](10_EXPORT_AND_FILE_EXCHANGE.md)
