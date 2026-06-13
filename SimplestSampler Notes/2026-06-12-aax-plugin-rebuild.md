---
schema_version: 1
title: AAX plugin rebuild
type: task
status: verified
date: 2026-06-12
tags:
  - aax
  - plugin
  - build
  - sampler
area: sampler
last_verified: 2026-06-12
freshness: reverify-before-use
related:
  - [[Runbooks/Build and Run]]
  - [[Phases/Phase 3 AAX Plugin]]
  - [[Home]]
---

# AAX plugin rebuild

## Goal

Rebuild and reinstall the `SimplestSamplerAudioSuite` AAX AudioSuite plugin so it matches the current SimplestSampler app bridge and can be smoke-tested again in Pro Tools.

## Context

- The app now exports plugin bridge state from `SimplestSampler/Services/PluginBridgeService.swift`.
- The current bridge document contains `activeSlotCount`, `slots`, and `shortcutBindings`.
- The plugin reads and writes that same JSON shape via `aax/SimplestSamplerAudioSuite/Source/PluginLibraryBridge.cpp`.
- Recent app work on theme controls, Sound Board, and window sizing did not change the plugin bridge contract; the plugin still only depends on the active-slot bank and shortcut export.

## Changes

- Re-read `README.md`, [[Runbooks/Build and Run]], `scripts/build-aax-plugin.js`, and the `aax/` target sources before rebuilding.
- Confirmed no source edits were required in `aax/` or the app bridge to support the current 4-16 slot range or shortcut export.
- Rebuilt the AAX SDK targets plus `SimplestSamplerAudioSuite` with `node scripts/build-aax-plugin.js`.
- Reinstalled the plugin bundle to `/Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin`.
- Reconfirmed the install is ad-hoc signed and still uses the expected bundle identifier `com.hellojamison.simplestsampler.audiosuite`.

## Verification

### Working

- `node scripts/build-aax-plugin.js` -> AAX SDK `** BUILD SUCCEEDED **`
- `node scripts/build-aax-plugin.js` -> `SimplestSamplerAudioSuite` `** BUILD SUCCEEDED **`
- Build script output: `Built AAX plugin at /Users/jamisonrabbe/Projects/SimplestSampler/out/aax/SimplestSamplerAudioSuite/Release/SimplestSamplerAudioSuite.aaxplugin`
- Build script output: `Installed AAX plugin to /Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin`
- `codesign -dv --verbose=2 "/Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin"` -> `Identifier=com.hellojamison.simplestsampler.audiosuite`, `Signature=adhoc`
- `stat -f "%Sm %N" "/Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin/Contents/MacOS/SimplestSamplerAudioSuite"` -> `Jun 12 20:51:45 2026 /Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin/Contents/MacOS/SimplestSamplerAudioSuite`
- Bridge sync check: `PluginBridgeService.swift` exports `activeSlotCount` plus `shortcutBindings`; `PluginLibraryBridge.cpp` reads `activeSlotCount`, `slots`, and `shortcutBindings`; `SimplestSampler_AS_Parameters.cpp` uses the same shortcut keys (`samplerCaptureSlotN`, `samplerSlotN`) for slot labels in the Cocoa GUI

## Not verified

- Manual Pro Tools relaunch/rescan after this reinstall
- Live AudioSuite smoke test: open `SimplestSampler`, confirm the custom Cocoa slot panel appears, confirm the footer action reads `Capture`, and confirm a capture lands back in the app via `plugin-active-slots.json`
- Whether the current app-side theme pack should eventually influence the plugin Cocoa colors; the plugin still uses its own fixed dark palette

## Tried and failed

- None.

## Risks

- The Xcode-generated plugin build still emits a `CFBundleIdentifier` warning because the Info.plist identifier is set while the target `PRODUCT_BUNDLE_IDENTIFIER` build setting is empty; this did not block the build, but it is still noisy.
- Retail release work is still outstanding: registered Avid Manufacturer/Product IDs and PACE signing.
