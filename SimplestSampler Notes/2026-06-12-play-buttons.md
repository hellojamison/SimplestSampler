---
schema_version: 1
title: Play button styling
type: task
status: active
date: 2026-06-12
tags:
  - sampler
  - ui
area: sampler
last_verified: 2026-06-12
freshness: reverify-before-use
---

# Play button styling

## Goal

Make the main sampler play controls read as primary playback actions by using the semantic accent green styling and SF Symbol play/stop icons.

## Context

The toolbar `Play` / `Stop` control in `ContentView.swift` was still text-only and using the neutral toolbar button styling, while slot-row play buttons in `SlotRowView.swift` already used the accent palette. The request was to align both controls around the theme accent tokens without hardcoded colors and keep selected-row contrast intact.

## Changes

- `SimplestSampler/Views/ContentView.swift`: replaced the toolbar `Play` / `Stop` text label with an icon-only `play.fill` / `stop.fill` label, kept the same `playToggleTapped()` action and disabled logic, and changed `SamplerToolbarButtonStyle` to use the semantic accent gradient with a white icon
- `SimplestSampler/Views/SlotRowView.swift`: kept the row play button on the semantic accent fill, tightened the icon rendering with a monochrome white symbol, and added a subtle non-selected shadow while preserving inverted selected-row chrome for contrast

## Verification

- `./scripts/build-app.sh` → `** BUILD SUCCEEDED **`
- App bundle: `build/Build/Products/Debug/SimplestSampler.app`
- Build log confirms the updated SwiftUI play-button styles compiled cleanly at 2026-06-12 01:26

## Not verified

- Manual visual check of the toolbar play/stop button in light and dark themes
- Manual visual check of selected-row play buttons while a row is selected or actively playing
- Manual click test in the running app for toolbar play/stop and slot-row play/stop after the icon-only toolbar change

## Tried and failed

- none

## Risks / Follow-ups

- The toolbar control is now icon-only, so its discoverability depends on the existing help tooltip and familiar play/stop affordance
