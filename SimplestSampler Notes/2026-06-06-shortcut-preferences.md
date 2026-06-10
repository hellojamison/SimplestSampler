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

## Verification

- `xcodebuild` Debug — BUILD SUCCEEDED (agent report)

## Not verified

- Shortcut conflict detection
- Live global hotkey behavior after clear binding

## Tried and failed

- none

## Risks / Follow-ups

- Deferred: conflict warnings, richer key display, separate prefs status area
