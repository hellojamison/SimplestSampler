# Shortcut preferences UI

## Goal

Preferences UI for all 8 sampler keyboard shortcuts (capture + play slots 1–4).

## Context

- Inline Cap/Trigger buttons on slot rows existed from Phase 1
- User requested prefs as we build further

## Changes

- `ShortcutPreferencesView.swift`, `ShortcutBindingButton.swift`
- `Settings` scene + gear button + ⌘,
- `PreferencesStore.resetShortcutBindingsToDefaults()`, clear/reset per row
- 2026-06-12 sizing pass: narrowed prefs content from the old `minWidth: 460` layout to a compact `minWidth: 392` / `idealWidth: 404` layout, moved each shortcut row to a tighter info + binding + actions arrangement, and set the `Settings` scene to `.windowResizability(.contentSize)` so the window hugs content height instead of opening tall with empty space

## Verification

- `xcodebuild` Debug — BUILD SUCCEEDED (agent report)
- `./scripts/build-app.sh` — `** BUILD SUCCEEDED **` (2026-06-12)

## Not verified

- Shortcut conflict detection
- Live global hotkey behavior after clear binding
- Live visual check that the Settings window reopens at the tighter content-sized frame without clipping in Light and Dark modes

## Tried and failed

- none

## Risks / Follow-ups

- Deferred: conflict warnings, richer key display, separate prefs status area
- `.windowResizability(.contentSize)` intentionally favors a content-sized settings window over a freely resizable one
