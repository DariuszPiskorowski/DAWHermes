# UI Reference Map (Milestone 1.1)

This document captures the structural UI alignment applied in Milestone 1.

## Reference intent

The macOS reference repository (`DAW-create-example`) is used as a read-only interaction and structure reference.

DAWHermes does not copy Swift/SwiftUI code. It mirrors high-level layout semantics in native JUCE.

## Structural mapping

Milestone 1 layout in DAWHermes is now organized as:

1. Menu bar (top)
2. Transport/status strip (under menu)
3. Main workspace row with 3 columns:
   - left: tracks panel (header + track list)
   - center: timeline/work area
   - right: AI assistant panel
4. Bottom full-width row:
   - MIDI editor panel spans the full workspace width
5. Status strip (bottom)

This matches the requested "top 3 columns + bottom full-width MIDI" structure.

## Resizable workspace splitters

Milestone 1.1 adds three draggable separators inside the workspace area:

1. left/center column separator;
2. center/right column separator;
3. top-row/bottom-row separator.

Panel ratios are persisted to user settings and restored on launch.

`View -> Reset Panel Layout` restores default ratios.

## Deterministic geometry extraction

Layout geometry and panel-state sanitization are extracted into `core/MainLayoutGeometry` and consumed by `ui/MainComponent`.

Benefits:

- deterministic and testable sizing behavior;
- no hidden ad-hoc geometry logic inside view code;
- easier parity checks against future reference updates.

## Interaction behavior retained

Milestone 1 keeps Milestone 0 deliberate interaction rules:

- track selection does not auto-open tools;
- context menu opens only on right-click;
- Hermes dialogs open only after explicit command selection.
