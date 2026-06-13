# SimplestSampler — Project Notes

Start here for operational memory. Repo: [github.com/hellojamison/SimplestSampler](https://github.com/hellojamison/SimplestSampler)

## Current state

- **Phase:** 3a–3d scaffold — AudioSuite Capture plugin builds with custom Cocoa slot-bank GUI; installed to system Plug-Ins folder; app bridge wired; manual PT Capture/GUI not yet verified
- **Active areas:** [[Areas/Sampler]] · [[Areas/PT-Capture]] · [[Phases/Phase 3 AAX Plugin]]
- **Last build:** `./scripts/build-app.sh` → `build/Build/Products/Debug/SimplestSampler.app` after `** BUILD SUCCEEDED **` (2026-06-12 02:41:28); `node scripts/build-aax-plugin.js` → AAX SDK + `SimplestSamplerAudioSuite` `** BUILD SUCCEEDED **`, fourth-pass GUI crash fix (`SchedulePanelRefresh` with epoch + MRC retain, immutable slot-row snapshot, no `performSelectorOnMainThread`) reinstalled to `/Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin` at 2026-06-12 23:55:15
- **Dev workflow:** `scripts/build-app.sh` wraps the standard Debug `xcodebuild` command; use `--open` to relaunch the built app from the resolved bundle path
- **Blockers:** Avid Manufacturer/Product IDs not registered (dev placeholders `JMRB`/`SSmp`); PACE signing before retail PT
- **UI:** OverCue default theme tokens now drive warm light + warm earth dark palettes, plus Theme pack and System / Light / Dark appearance controls; the Settings window now uses a tighter shortcut-preferences layout (`minWidth: 392`, `idealWidth: 404`) with content-sized window resizability so it opens closer to its actual content height; the main sampler window now sizes Active launch height from the real themed row stack instead of the stale `42`-point stride estimate, so default Active relaunch now lands at `612x516` instead of `612x468`, clamps undersized saved frames up to the current tab's preferred height on launch, and keeps `Active` / `Stored` / `Sound Board` inside explicit vertical scroll regions instead of clipping when the window is shorter than the content; slot-row play buttons now use the semantic accent green gradient while selected rows keep inverted play chrome for contrast; dynamic active slots (4–16) now start drag from the full filled row, and the old left-edge grip icon is gone so the play button is the leftmost control while cross-tab drag from `Active` into `Stored` / `Sound Board` still works via drag-aware tab pills; the main `Active` / `Stored` / `Sound Board` tab pills stay as a compact centered capsule with padded intrinsic-width hit targets instead of stretching full width across the window; Active / Stored / Sound Board tabs keep stored category chips + add/rename/delete/filter/assignment controls living directly in `Stored`, and stored-row drag-to-category works again after restoring the plain-text stored-capture fallback in `StoredCategoryBar`; Sound Board is a 4×4 pad grid with drop-to-load, active-slot drop-to-assign, click-to-play, PT capture into selected/first-empty pad, and pad-to-pad drag now stays on soundboard-specific UTTypes while deduplicating pad capture IDs after writes so a move-to-empty clears the source pad instead of leaving the same pad ID on multiple pads; slot-row rename focus keeps the normal row spacing while showing the styled inline edit field; app syncs `plugin-active-slots.json` with AAX plugin; Impeccable baseline in root `PRODUCT.md` + `DESIGN.md` (north star: "The Session Bench")
- **Notes:** Spec-aligned vault; validate with `python3 scripts/validate_notes.py`
- **Next:** Quit/relaunch PT; confirm plugin scan no longer crashes (`EXC_BAD_ACCESS` at `refreshOnMainThread + 132` / `[_slotRows copy]` — fourth-pass fix uses epoch-guarded `dispatch_async` refresh and immutable row snapshot); verify AudioSuite → SimplestSampler shows dark slot panel + **Capture** button; run Capture → Active tab; register Avid IDs + PACE when shipping
- **Reference (read-only):** `/Users/jamisonrabbe/Projects/OverCue` sampler + `main.js` capture sections

## Quick links

- [[Notes System]]
- [[Dashboards/Open Loops]]
- [[Phases/Phase 1 Playback]]
- [[Phases/Phase 2 PT Capture]]
- [[Phases/Phase 3 AAX Plugin]]
- [[2026-06-06-phase-1-swift-app]]
- [[2026-06-06-phase-2-pt-capture]]
- [[2026-06-06-shortcut-preferences]]
- [[Runbooks/Build and Run]]
- [[Runbooks/Agent Session Handoff]]
- [[Known-Good/Data Locations]]
- [[2026-06-06]]
