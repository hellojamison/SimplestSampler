---
title: Window sizing and scrolling
date: 2026-06-12
tags:
  - sampler
  - ui
  - window-sizing
---

# Window sizing and scrolling

## Goal

Stop the main app window from launching too short for the current tab, and make the main list/grid area scroll instead of clipping when content exceeds the visible height.

## Context

The sampler window could reopen at a saved height that was only checked against the hard minimum height. That let shorter historical frames survive even when the current tab needed more space. The default window height was also shorter than the Active tab's own preferred height once the slot-count controls were included, so a fresh launch could start clipped before any manual resize.

Follow-up regression: after the first pass, the app still relaunched too short on `Active`. Live capture showed a `612x468` window where the fourth slot barely fit and the `+ Add Slot` control was completely clipped. The root cause was that the preferred-height math still used the old `slotRowStride = 42` estimate, which no longer matched the current themed row height plus explicit `rowSpacing`.

## Changes

- `SamplerCapture.swift`: changed `defaultWindowHeight` to match the Active tab's preferred launch height instead of a smaller stale constant, and added a Stored-tab preferred-height allowance for the category chip bar
- `ContentView.swift`: made the main window content fill the available height, and made the Active / Stored / Sound Board content regions explicit vertical `ScrollView`s with indicators so overflow stays scrollable
- `ContentView.swift` `WindowFrameTracker`: when restoring a saved frame on launch, now bumps undersized saved heights up to the current tab's preferred content height instead of only accepting any frame above the hard minimum
- `SamplerViewModel.swift`: `resizeWindowForCurrentTab()` now grows the window when the current tab needs more height, but no longer shrinks an already taller user-sized window
- `SamplerCapture.swift`: replaced the stale Active-row stride estimate with explicit row-height (`48`) + row-spacing (`8`) math, plus explicit add-slot spacing, so preferred Active height now matches the real 4-slot stack instead of undercounting it by about one control row
- `ContentView.swift` and `SamplerViewModel.swift`: window growth now compares the current content height to the preferred content height before adding only the missing delta, instead of assuming the current frame height already equals visible content height

## Verification

- `./scripts/build-app.sh` -> `** BUILD SUCCEEDED **`
- App bundle: `build/Build/Products/Debug/SimplestSampler.app`
- 2026-06-12 01:49 build completed after the launch-height and scrolling changes; no Swift compile errors from `SamplerCapture.swift`, `ContentView.swift`, or `SamplerViewModel.swift`
- 2026-06-12 02:00 follow-up build also completed with `** BUILD SUCCEEDED **`
- Live relaunch of `build/Build/Products/Debug/SimplestSampler.app`
- Window bounds from `swift /tmp/list_windows.swift`: before follow-up fix `612x468`; after follow-up fix `612x516`
- `screencapture -x -l<window-id> /tmp/simplestsampler-window-after.png` after relaunch showed all 4 Active slots plus the `+ Add Slot` control visible at launch
- Calculated preferred heights after the follow-up fix: default `Active` `516`, default `Stored` `510`, `Sound Board` `566`

## Not Verified

- Manual relaunch of the app with a previously short saved frame to confirm the window now reopens at or above the current tab's preferred height
- Manual overflow behavior in the running app for `Active`, `Stored`, and `Sound Board`, including whether the list/grid scrolls naturally at smaller window heights
- Manual scrollbar appearance with the user's macOS scrollbar preference settings
- Whether the grown `516`-point launch frame is immediately persisted back into `samplerSessionState.windowFrame` without a subsequent user resize/move

## Tried and Failed

- none

## Risks

- The launch sizing logic is build-verified but still needs a live relaunch pass to confirm saved-window restoration behaves well across tab changes and older session data
- macOS scrollbar visibility is still partly governed by the system scrollbar preference, even though the sampler content now stays inside explicit scrolling regions
- `Stored` and `Sound Board` preferred heights are now consistent with the revised sizing math, but only the default `Active` relaunch was live-checked during this follow-up
