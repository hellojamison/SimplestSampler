---
title: Active tab drag-to-reorder
date: 2026-06-11
tags: [sampler, ui]
---

# Active tab drag-to-reorder

## Goal

Allow drag-to-reorder samples in the Active tab without breaking file drop-to-load.

## Context

Active slots are `recentCaptures` in session state (`SamplerSessionState`). Order is already persisted via `PreferencesStore.saveSessionState()`. Simple-ish tab uses row `onDrag` for category assignment; Active needed a separate reorder path.

## Changes

- `UTType.simplestSamplerActiveSlotIndex` in `SamplerCapture.swift`
- `SamplerViewModel.moveActiveSlot(from:to:)` — array move + `persistRecentCaptures()`
- `SlotRowView`: grip handle with `onDrag` on Active rows only; `onDrop` for slot-index type alongside existing `.fileURL` drop

## Verification

- `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build` → **BUILD SUCCEEDED**

## Not verified

- Live drag UX in Active tab
- Reorder with empty slots mixed in
- Shortcut slot numbers follow new order after reorder (expected)

## Tried and failed

(none)
