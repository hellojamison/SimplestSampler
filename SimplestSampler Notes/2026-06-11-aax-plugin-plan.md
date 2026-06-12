---
date: 2026-06-11
tags: [phase-3, aax, audiosuite, capture]
status: implemented-scaffold
---

# AAX AudioSuite Capture plugin

## Goal

Ship an **AudioSuite-only** AAX plugin that captures the current Pro Tools selection via the Analysis pass (UI label **Capture**) into SimplestSampler active slots, sharing disk + JSON with the standalone app. Live instrument / MIDI playback is a later phase.

## Context

- Standalone app keeps PTSL capture, library, and global shortcuts ([[2026-06-06-phase-2-pt-capture]])
- AAX SDK 2.9.0 at `/Users/jamisonrabbe/Downloads/aax-sdk-2-9-0` (not in repo)
- Developer Pro Tools runs unsigned plugins; retail requires PACE signing
- Companion architecture: one `.aaxplugin` = full slot bank (4–16 slots)

## Product shape (locked)

| Component | Role |
|-----------|------|
| `SimplestSampler.app` | UI, PTSL capture, shortcuts, writes `plugin-active-slots.json` |
| `SimplestSamplerAudioSuite.aaxplugin` | AudioSuite Capture → WAV + JSON on Analysis pass |
| Shared bridge | `~/Library/Application Support/SimplestSampler/plugin-active-slots.json` |

**Not v1:** PTSL inside plugin, native instrument, slots 5–16 shortcut bindings in app UI.

## Technical approach

### Repo layout

```
aax/
  CMakeLists.txt
  SimplestSamplerAudioSuite/
    Source/   # Describe, Parameters, HostProcessor, GUI, CaptureWavWriter, PluginLibraryBridge
scripts/build-aax-plugin.js
```

### Capture flow

1. User selects audio in PT → AudioSuite → SimplestSampler
2. **Capture** (`PreAnalyze` → `AnalyzeAudio` → `PostAnalyze`)
3. Float32 WAV → `Generated Sampler Captures/`
4. Atomic JSON update → app reloads Active slots on mtime

### Dev IDs (placeholders)

- Manufacturer display name: **Take One Audio** (`SetManufacturerName` / `kSimplestSampler_ManufacturerName`)
- Manufacturer ID: `JMRB`
- Product: `SSmp`
- AudioSuite plug-in: `ASmp`

Register real 4-char IDs with Avid before any distribution.

### App bridge

- `PluginBridgeService.swift` read/write + `PluginBridgeWatcher`
- `SamplerViewModel` merges `source: aax-audiosuite-capture` slots
- Shortcut bindings exported in `shortcutBindings` on prefs / slot changes

## Verification

See [[Phases/Phase 3 AAX Plugin]] for evidence (cmake/xcodebuild commands and results).

### 2026-06-11 visibility fix

- **Root cause:** Plugin was in `~/Library/Application Support/Avid/Audio/Plug-Ins/`; Pro Tools Developer 26.4 log (`~/Library/Logs/Avid/Pro_Tools_Developer_2026_06_11_02_51_12.txt`) loads third-party AAX only from `/Library/Application Support/Avid/Audio/Plug-Ins/` — SimplestSampler never appeared in scan list.
- **Registration:** `AAX_eProperty_PlugInID_AudioSuite`, HostProcessor proc ptr, manufacturer/product IDs were already correct vs `DemoDelay_HostProcessor`; category changed from `SWGenerators` → `Example|Effect`.
- **Install:** `node scripts/build-aax-plugin.js` copies to system Plug-Ins folder + `codesign --force --deep --sign -`.

## Changes (2026-06-11 custom GUI)

- **Root cause of ugly UI:** `AAX_eProperty_UsesClientGUI=true` + empty `CreateViewContainer` → Pro Tools drew generic sliders for every parameter (truncated names like `LOT 1 STATU`).
- **Fix:** Cocoa `AAX_CEffectGUI_Cocoa` panel (`SimplestSampler_AS_GUI.mm`) — dark theme slot rows, click-to-select capture target, active-slot count popup, shortcut labels from bridge JSON; internal params only (`TargetSlot`, `ActiveSlotCount`, bypass).
- **Build:** `node scripts/build-aax-plugin.js` (adds SDK `AAX_CEffectGUI_Cocoa.mm`, links AppKit/Cocoa).

## Not verified

- Manual PT Capture workflow
- Custom GUI + **Capture** button label in AudioSuite after PT restart
- PT rescan after system-folder install

## Signing / release

- Avid ID registration: [developer.avid.com/audio](https://developer.avid.com/audio)
- PACE: contact `audiosdk@avid.com` when ready for retail PT
