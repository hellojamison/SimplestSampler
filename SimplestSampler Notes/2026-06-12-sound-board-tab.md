---
title: Sound Board tab
date: 2026-06-12
tags:
  - sampler
  - ui
---

# Sound Board tab

## Goal

Keep the main sampler tabs at `Active` / `Stored` / `Sound Board`, with stored-category management living inside `Stored` instead of a separate `Simple-ish` tab.

## Context

This overlapped with in-progress Sound Board work. The tab bar needed to end with `Active` / `Stored` / `Sound Board` while preserving stored category add/rename/delete/filter/assignment behavior.

## Changes

- `ContentView.swift`: removed the visible `Simple-ish` tab, kept `Sound Board`, and moved the category UI into the main `Stored` route
- `StoredCategoryBar.swift`: new stored-tab category strip with `All`, `Uncategorized`, per-category chips, add button, rename/delete menus, and stored-capture drop targets
- `SamplerViewModel`: renamed the active filter state to `storedCategoryFilter`, exposed `filteredStoredCaptures`, and reconciles Stored selection against the filtered rows
- `SamplerSessionState` + `PreferencesStore`: migrate saved `activeTab == "simpleish"` sessions to `stored` and decode the old `simpleishCategoryFilter` key into the new stored filter field
- `SimpleishCategoryBar.swift`: removed after the Stored tab took over its UI responsibilities
- Existing Sound Board support remains in place so the visible tabs are now `Active` / `Stored` / `Sound Board`
- `SamplerCapture.swift`: added a dedicated active-slot drag UTType plus shared `NSItemProvider` helpers so Active, Stored, and Sound Board all parse the same drag payload
- `SlotRowView.swift`: filled Active rows still drag from the full row for reorder and cross-tab drops, but the visual left-edge grip icon is gone so the play button is now the leftmost control; Active rows still accept file drops and use the shared payload path for reorder
- `ContentView.swift`: tab pills accept dragged Active slots, switch to `Stored` / `Sound Board` on hover during a drag, and perform a default drop action when released on the pill itself
- `ContentView.swift`: restored the main tab bar to a compact centered pill with intrinsic-width `Active` / `Stored` / `Sound Board` segments; each segment keeps a rectangular hit area via tab padding + `contentShape(Rectangle())` so clicks land beyond the text glyphs without stretching the capsule across the window
- `StoredCategoryBar.swift` + stored-tab scroll area: dropping an Active slot into Stored now stores it; dropping onto a category chip stores it and assigns that category
- `StoredCategoryBar.swift`: restored the pre-move plain-text fallback for stored-capture drags, so dragging a stored row onto `Uncategorized` or a named category chip no longer falls through the active-slot decoder when the custom stored UTType is not surfaced on drop
- `SoundBoardView.swift` + `SamplerViewModel`: dropping an Active slot onto a sound-board pad now assigns the existing capture metadata/file reference to that pad instead of creating another stored copy
- `SoundBoardView.swift` + `SamplerCapture.swift` + `SamplerViewModel`: filled sound-board pads drag with a dedicated pad payload; dropping onto another pad moves into an empty slot or swaps clips when both pads are filled
- `SamplerCapture.swift` + `SoundBoardView.swift`: sound-board pad drags no longer expose a generic plain-text payload that could be misread by active-slot drop handlers; Active-slot drag decoding now only accepts the dedicated active-slot UTTypes
- `SamplerViewModel.swift`: sound-board writes now deduplicate `soundboardPads` by capture ID after move/load/assign/capture paths so the same pad capture ID cannot remain on multiple pads even if a stale or secondary write occurs

## Verification

- `./scripts/build-app.sh` → `** BUILD SUCCEEDED **`
- App bundle: `build/Build/Products/Debug/SimplestSampler.app`
- 2026-06-12 00:38 build completed after the drag/drop changes; no Swift compile errors from the new tab-drop or active-payload code paths
- 2026-06-12 01:02 build completed after moving the active-slot drag source to the row-level modifier; no Swift compile errors from the new active-row drag path
- 2026-06-12 01:04 build completed after widening the main tab hit areas; no Swift compile errors from the equal-width tab segment or drag-hover tab-drop path
- 2026-06-12 01:08 build completed after sound-board pad drag/swap; no Swift compile errors from the new soundboard pad drag UTType or `moveSoundboardPad(from:to:)`
- 2026-06-12 01:26 build completed after removing the per-tab `.frame(maxWidth: .infinity)` expansion and keeping the larger hit target inside each tab pill; no Swift compile errors from the compact-tab layout or tab-drop hover path
- 2026-06-12 01:32 build completed after removing the visible Active-row drag grip while keeping the row-level drag modifier; no Swift compile errors from the simplified row leading layout
- 2026-06-12 02:30:23 `./scripts/build-app.sh` completed after restoring stored-capture plain-text drop fallback in `StoredCategoryBar.swift`; no Swift compile errors from the updated category-chip drop path
- 2026-06-12 02:41:28 `./scripts/build-app.sh` completed after tightening sound-board pad drag payloads and adding `soundboardPads` ID deduplication; no Swift compile errors from the updated drag/drop or sound-board state-management paths

## Not verified

- Manual stored-category chip filtering, row picker assignment, and drag-to-category flows in the running app
- Manual stored-row drag onto category chips with both the custom stored UTType path and the plain-text fallback path
- Manual Sound Board pad play/drop/capture/clear flows in the running app after the tab-bar cleanup
- Window resize when switching between Active, Stored, and Sound Board tabs
- Manual Active full-row drag hover/drop across tabs: hover `Stored` / `Sound Board`, then drop into the stored surface, a category chip, or a sound-board pad
- Manual click/drag interaction balance on active rows: play button, Store/Delete, shortcut buttons, inline rename field focus/editing, row selection, and file drop onto a row after the row-level drag source moved off the grip
- Manual Active-row feel after removing the grip icon: confirm the play button now reads as the leftmost control without making full-row drag/reorder discoverability or reliability worse
- Manual mouse hit-testing on the compact `Active` / `Stored` / `Sound Board` pills to confirm the padded hit area still feels generous near the capsule edges
- Manual sound-board pad drag: move to empty pad, swap between filled pads, and confirm click-to-play / file drop / Active-slot drop still work
- Manual repro of the reported bug in the running app to confirm pad-to-pad drag now clears the source pad on move-to-empty instead of leaving a duplicate behind

## Tried and failed

- none

## Risks

- The compact tab-pill restore is only build-verified so far; manual hit-testing is still needed to confirm clicks near the capsule edges feel generous enough without reintroducing layout stretch
- Active-slot drag hover/drop across the compact tab pills still needs a live app pass to confirm the hover-target area remains comfortable during cross-tab drags now that the visible grip affordance is gone
- The duplication report was fixed by removing ambiguous drag payload fallback and hardening the model, but the live app still needs a manual drag pass to confirm no additional overlapping drop target is firing in the current layout

## Pad behavior

- **Click filled pad:** play/stop toggle
- **Click empty pad:** select pad for capture target
- **Drop audio file:** load clip into pad
- **Capture toolbar (Sound Board tab):** captures into selected pad or first empty pad
- **Right-click filled pad:** Clear Pad
