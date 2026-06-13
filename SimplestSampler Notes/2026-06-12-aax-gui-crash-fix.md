---
schema_version: 1
title: AAX GUI timer crash fix
type: task
status: in-progress
date: 2026-06-12
tags:
  - aax
  - audiosuite
  - crash
  - gui
area: sampler
last_verified: 2026-06-12
freshness: reverify-before-use
related:
  - [[Phases/Phase 3 AAX Plugin]]
  - [[Runbooks/Build and Run]]
  - [[Home]]
---

# AAX GUI timer crash fix

## Goal

Stop Pro Tools `EXC_BAD_ACCESS` on load when `SimplestSampler_AS_GUI::TimerWakeup()` calls `-[SimplestSamplerPanelView refresh]`.

## Context

- Crash: Pro Tools Developer 26.4.0.1, main thread, `objc_msgSend` inside `SimplestSamplerPanelView refresh` from `TimerWakeup`.
- Latest signature: `KERN_INVALID_ADDRESS at 0xd66323c0` — `objc_msgSend` inside `refreshOnMainThread + 132` → `[_slotRows copy]` on deallocated panel after `performSelectorOnMainThread` from `TimerWakeup` (MRC plugin; cancelPreviousPerformRequests does not prevent messaging deallocated target).
- GUI code lives in `aax/SimplestSamplerAudioSuite/Source/SimplestSampler_AS_GUI.mm` (panel view is a private class in that file).
- Disassembly (arm64 Release build): `-[SimplestSamplerPanelView refresh] + 352` → instruction after `bl _objc_msgSend$copy` on `_slotRows` ivar — source line 258 `for (SimplestSamplerSlotRowView* row in [_slotRows copy])`.
- Root cause: TOCTOU race — `refresh` passes top-of-method guards, then `DeleteViewContainer`/`detachFromGUI` frees panel subviews on main thread while a timer-queued or in-flight `refresh` still reads `_slotRows`.

## Changes

### Pass 1 (insufficient)

- `detachFromGUI`, `DeleteViewContainer` override, `TimerWakeup`/`refresh` nil guards, main-thread dispatch.

### Pass 2 (insufficient)

- Removed raw `mPanelView` storage; added `PanelView()` that resolves the panel only from live `mViewController.view` with class check.
- Added `mPanelRefreshEnabled` — cleared at `DeleteViewContainer`/destructor start, set only after panel is attached (`superview != nil`) in `CreateViewContainer`.
- `TimerWakeup` / `RefreshPanel` require `mPanelRefreshEnabled`, view container, live panel, `superview`, and not detached.
- Panel: `readyForRefresh` set at end of init; cleared on detach/dealloc; `refresh` copies `_slotRows`, validates row class, snapshots `_gui` before C++ calls; MRC-safe main-thread scheduling via `performSelectorOnMainThread:`.

### Pass 4 (current)

- Disassembly mapped `-[SimplestSamplerPanelView refreshOnMainThread] + 132` to `[_slotRows copy]` inside `@synchronized` — same UAF as pass 3 but triggered when `performSelectorOnMainThread` delivered refresh to a deallocating/deallocated panel (MRC; `cancelPreviousPerformRequests` insufficient).
- Replaced `performSelectorOnMainThread` with `SchedulePanelRefresh()` using `dispatch_async` + MRC retain/release and `refreshEpoch` token on panel; stale blocks bail without messaging dead objects.
- Replaced mutable `_slotRows` + runtime `copy` with immutable `_slotRowsSnapshot` created once at init; removed `@synchronized` and detach-time nil of subview refs.
- `detachFromGUI` / `dealloc` increment `refreshEpoch`, cancel legacy selectors, clear `_gui` only.
- `TimerWakeup` and `RefreshPanel` route through `SchedulePanelRefresh()`.

## Verification

### Working

- `otool -tvV` on Release binary (pass 3): crash offset `0x9224 + 352 = 0x9384` maps to `[_slotRows copy]` in `refresh`.
- `otool -tvV` on Release binary (pass 4): `refreshOnMainThread` no longer calls `copy` or `objc_sync_enter`; uses `_slotRowsSnapshot count` directly.
- `node scripts/build-aax-plugin.js` → AAX SDK + `SimplestSamplerAudioSuite` `** BUILD SUCCEEDED **`.
- Reinstalled to `/Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin`.
- Binary mtime `Jun 12 23:55:15 2026`.

### Not verified

- Pro Tools relaunch/rescan without crash.
- AudioSuite GUI renders slot panel after fix.
- Capture round-trip still works.
- Repeated open/close AudioSuite window cycles.

### Tried and failed

- Pass 1 (detach + nil guards + recreate logic): user confirmed PT still crashed on load with same stack (`TimerWakeup` → `refresh` → `objc_msgSend`).
- Pass 3 (`@synchronized` + `refreshOnMainThread` + cancel selectors + nil subviews on detach): user confirmed PT still crashed (`KERN_INVALID_ADDRESS at 0xd66323c0`, `refreshOnMainThread + 132` → `[_slotRows copy]`).
- Pass 2 (`PanelView()` + `mPanelRefreshEnabled` + row copy): user confirmed PT still crashed on load (`KERN_INVALID_ADDRESS at 0x10` in `refresh` +352).

## Risks / Follow-ups

- Manual PT verification required to confirm crash is gone on plugin scan and AudioSuite open/close cycles.
- If crash persists, capture fresh crash report — offset inside `refreshOnMainThread` would indicate a new failure site.
