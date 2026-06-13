---
schema_version: 1
title: OverCue default theme
type: task
status: active
date: 2026-06-11
tags:
  - sampler
  - ui
  - theme
  - overcue
area: Sampler
last_verified: 2026-06-12
freshness: reverify-before-use
related:
  - "[[2026-06-11]]"
  - "[[Areas/Sampler]]"
---

# OverCue default theme

## Goal

Complete the SwiftUI sampler's OverCue-default visual redesign, including the warm dark palette from `darkTheme.css`, extensible theme-pack plumbing, and settings UI for theme selection.

## Context

- Reference only: `/Users/jamisonrabbe/Projects/OverCue/public/css/sampler.css`
- Reference only: `/Users/jamisonrabbe/Projects/OverCue/public/css/darkTheme.css`
- Swift theme source: `SimplestSampler/Views/SamplerTheme.swift`
- Preference wiring: `SimplestSampler/Models/SamplerPreferences.swift`, `SimplestSampler/Services/PreferencesStore.swift`, `SimplestSampler/ViewModels/SamplerViewModel.swift`
- Settings UI: `SimplestSampler/Views/ShortcutPreferencesView.swift`

## Changes

- Added `SamplerAppTheme` with `.default` and wired `appTheme` through `SamplerPreferences`, `PreferencesStore`, and `SamplerViewModel`.
- Reworked `SamplerThemedRoot` to resolve tokens via `SamplerTheme.tokens(appTheme:colorScheme:)`.
- Replaced the prior cool dark sampler colors with OverCue's warm earth defaults:
  - background `#3b3835` -> `#282624`
  - panel `rgba(46, 43, 41, 0.92)`
  - accent `#2c7a5f` -> `#1d6f54`
  - capture `#8a4545` -> `#6b3434`
- Rebuilt the theme token set around semantic roles (`bgTop` / `bgBottom`, `panel`, `panelSoft`, `border`, `controlTop`, `accentTop`, `accentStrong`, `selectedRow`, `sliderTrack`, `sliderThumb`, etc.).
- Updated sampler views to use shared tokens consistently, including the volume slider track/thumb styling and the Appearance section in Settings.
- Updated `SlotPlayButtonStyle` in `SimplestSampler/Views/SlotRowView.swift` so slot-row play buttons use the semantic accent gradient (`accentTop` -> `accentStrong`) with a white glyph; selected rows still use the inverted selected-row chrome so the control stays readable against the green selected-row background.
- Left the toolbar `Play` button in `SimplestSampler/Views/ContentView.swift` unchanged because it uses `SamplerToolbarButtonStyle`, a separate neutral toolbar control style.

## Verification

- `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build`
  - Result: `** BUILD SUCCEEDED **`
- `open "/Users/jamisonrabbe/Library/Developer/Xcode/DerivedData/SimplestSampler-egeasldslawyzqebfjylplcnobff/Build/Products/Debug/SimplestSampler.app"`
  - Result: Debug app bundle launched
- `scripts/build-app.sh`
  - Result: `** BUILD SUCCEEDED **`
  - App bundle: `build/Build/Products/Debug/SimplestSampler.app`

## Not verified

- Manual visual comparison of the running app in System, Light, and Dark after launch
- Final on-device appearance of native slider/thumb rendering across macOS control states
- Manual visual review that selected and unselected slot-row play buttons both read clearly after the accent-green change

## Tried and failed

- `screencapture -x "/tmp/simplestsampler-theme-check.png"` failed with `could not create image from display`, so automated screenshot verification was unavailable

## Risks / Follow-ups

- Confirm the warm dark palette reads correctly on-device in the main window and Settings, especially selected rows and the new Appearance controls
- Remove or supersede the earlier partial frosted-theme note once this redesign is fully manually verified
