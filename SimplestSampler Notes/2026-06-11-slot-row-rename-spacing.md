---
title: 2026-06-11 Slot Row Rename Spacing
date: 2026-06-11
tags:
  - simplestsampler
  - ui
  - slot-row
status: done
---

# 2026-06-11 Slot Row Rename Spacing

## Goal

Keep sample-slot rows the same height and spacing while renaming, without removing the existing focused rename field styling.

## Context

User report: entering rename mode in a slot row changed the row rhythm compared to the normal name + filename display state. `SlotRowView.swift` applied extra focused-only padding directly to the `TextField`, so the top metadata line became taller and pushed the filename line down.

## Changes

- Added `namePlaceholder` and `nameLayoutText` helpers in `SimplestSampler/Views/SlotRowView.swift`.
- Changed the slot name view to reserve layout with the normal semibold text metrics and render the actual `TextField` as an overlay.
- Moved the rename field chrome to a negative-padding background/overlay so the focused border, fill, and shadow stay visible without affecting row layout.

## Verification

- `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build` → `** BUILD SUCCEEDED **`

## Not verified

- Manual visual check in the running app that Active and Stored rows keep identical spacing before and during rename.

## Tried and failed

- None.

## Risks

- The overlay approach depends on the hidden text and visible `TextField` staying on the same font metrics; future font/token changes should keep those in sync.
