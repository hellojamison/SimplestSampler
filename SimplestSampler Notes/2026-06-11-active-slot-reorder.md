---
title: Active tab drag-to-reorder
date: 2026-06-11
tags: [sampler, ui]
---

# Active tab drag-to-reorder

## Goal

Allow drag-to-reorder samples in the Active tab without breaking file drop-to-load.

## Context

Active slots are `recentCaptures` in session state (`SamplerSessionState`). Order is already persisted via `PreferencesStore.saveSessionState()`. Simple-ish tab uses row `onDrag` for category assignment; Active needed a separate reorder path. After the first pass, the drag grip was visible on Active rows but the whole row still had a second `onDrag` attached for Simple-ish category moves, so Active drags could be claimed by an empty item provider instead of the grip's slot-index provider.

## Changes

- `UTType.simplestSamplerActiveSlotIndex` in `SamplerCapture.swift`
- `SamplerViewModel.moveActiveSlot(from:to:)` — array move + `persistRecentCaptures()`
- `SlotRowView`: kept the grip handle as the Active-only reorder source, removed the unintended row-level drag source from Active rows by gating stored-row drag behind a dedicated modifier, widened the grip hit target, added an open-hand cursor affordance, and unified row drop handling so Active rows still accept file drops while reorder drops accept the custom slot type and plain text fallback

## Verification

- `./scripts/build-app.sh` → `** BUILD SUCCEEDED **`

## Not verified

- Live drag UX in Active tab
- Reorder with empty slots mixed in
- Play button, rename field, and drag grip interactions in the running app
- Shortcut slot numbers follow new order after reorder (expected)

## Tried and failed

(none)

## Risks

- Active row reorder is intended only in the `Active` tab; `Stored` and `Simple-ish` rows should not gain this behavior.
