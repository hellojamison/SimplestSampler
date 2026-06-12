---
date: 2026-06-11
phase: 3
tags: [aax, audiosuite, capture]
---

# Phase 3 — AAX AudioSuite Capture

## Goal

AudioSuite-only `.aaxplugin` that captures Pro Tools selection audio via the Analysis pass (button relabeled **Capture**) into SimplestSampler active slots, sharing `plugin-active-slots.json` with the macOS app.

## Context

- SDK 2.9.0 at `/Users/jamisonrabbe/Downloads/aax-sdk-2-9-0` (not committed)
- Forked from `DemoDelay_HostProcessor` HostProcessor pattern (not `DemoMIDI_Sampler`)
- Dev placeholder IDs: `JMRB` / `SSmp` / `ASmp` — register real Avid Manufacturer + Product IDs before distribution
- Instrument / MIDI playback deferred to a later phase

## Changes

| Area | Path |
|------|------|
| CMake + plugin sources | `aax/SimplestSamplerAudioSuite/Source/` |
| Build script | `scripts/build-aax-plugin.js` |
| App bridge | `SimplestSampler/Services/PluginBridgeService.swift` |
| ViewModel reload | `SamplerViewModel` watches `plugin-active-slots.json` mtime |
| Shared JSON | `~/Library/Application Support/SimplestSampler/plugin-active-slots.json` |
| Capture WAV output | `~/Library/Application Support/SimplestSampler/Generated Sampler Captures/` |

Plugin properties: `AAX_eProperty_RequiresAnalysis`, `GetCustomLabel(Analysis) → "Capture"`, `UsesClientGUI`. Category: `Example|Effect` (AudioSuite menu). Install path: `/Library/Application Support/Avid/Audio/Plug-Ins/` (not `~/Library/...`).

Parameters: **Target Slot** (1–16), **Active Slots** (4–16), per-slot status (0/1 from bridge).

## Verification

### Working

- `cmake -B out/aax-sdk-build -S $AAX_SDK_ROOT -G Xcode -DAAX_BUILD_EXAMPLES=OFF` → configure OK
- `cmake --build out/aax-sdk-build --config Release --target AAX_Export AAXLibrary` → **BUILD SUCCEEDED**
- `node scripts/build-aax-plugin.js` → **BUILD SUCCEEDED** → installs `/Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin` (Mach-O arm64, adhoc signed)
- `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Release build` → **BUILD SUCCEEDED**
- `python3 scripts/validate_notes.py` → OK (1 unrelated warning)

### Not verified

- Dev Pro Tools 26.4 after system-folder install: AudioSuite → SimplestSampler → **Capture** button label and ingest
- Clip-by-clip / stereo selection smoke tests in PT
- End-to-end: plugin capture → app Active tab reload while app running
- SDK `DemoDelay_HostProcessor` legacy Xcode project (expects prebuilt `libAAXLibrary_libcpp` in SDK `Libs/Release`)

### Tried and failed

- `xcodebuild -project .../DemoDelay_HostProcessor.xcodeproj -scheme DemoDelay_example -configuration Release build` → linker error `library 'AAXLibrary_libcpp' not found` (use cmake `AAXLibrary` path instead for spike)

## Signing checklist (before retail PT)

- [ ] Request Manufacturer ID + Product ID from [developer.avid.com/audio](https://developer.avid.com/audio)
- [ ] Replace dev placeholders in `SimplestSampler_AS_Defs.h`
- [ ] PACE signing tools via `audiosdk@avid.com`
- [ ] Install signed `.aaxplugin` to `/Library/Application Support/Avid/Audio/Plug-Ins`
- [ ] Validate on retail Pro Tools build

## Related

- [[2026-06-11-aax-plugin-plan]]
- [[Known-Good/Data Locations]]
- [[Runbooks/Build and Run]]
