# Composer Assistant Protocol Notes

This note documents the currently observed Composer Assistant integration protocol and how DAWHermes handles compatibility safely.

## Evidence from existing Reaper scripts

Observed in `%APPDATA%/REAPER/Scripts/composers_assistant_v2`:

- Server script: `composers_assistant_nn_server.py`
  - imports `SimpleXMLRPCServer`
  - binds to `('127.0.0.1', 3456)`
- Reaper client script: `rpr_ca_functions.py`
  - uses `xmlrpc.client.ServerProxy('http://127.0.0.1:3456')`
  - calls remote function `call_nn_infill(...)`

Installation instructions in the same package explicitly require launching `composers_assistant_nn_server.exe` manually before running scripts.

## Protocol summary

- transport: XML-RPC over HTTP
- default endpoint: `127.0.0.1:3456`
- deployment model: separately launched local server process

This matches legacy Reaper integration expectations.

## DAWHermes boundary (Milestone 1)

DAWHermes adds a connector boundary (`ComposerAssistantConnector`) and UI settings, but does not auto-connect and does not alter Reaper-side behavior.

Default DAWHermes settings are conservative:

- connector disabled
- host `100.126.75.32`
- port `3456`
- timeout `800 ms`
- loopback-only safety disabled by default (user-toggleable)

A manual TCP probe action is available for operator-controlled reachability validation.

## Why this does not break existing Reaper integration

- DAWHermes does not modify Reaper scripts.
- DAWHermes does not start/stop the Composer Assistant server.
- DAWHermes keeps the same default port expected by existing scripts.
- Connection attempts happen only when explicitly requested from DAWHermes UI.

## Scope note on Ubuntu endpoint mention

A remote Ubuntu service endpoint can be configured in DAWHermes settings by changing host/port and disabling loopback-only mode, but this is opt-in and outside the default safety profile.
