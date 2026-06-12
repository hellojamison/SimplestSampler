# SimplestSampler — Project Notes

Start here for operational memory. Repo: [github.com/hellojamison/SimplestSampler](https://github.com/hellojamison/SimplestSampler)

## Current state

- **Phase:** 3a–3d scaffold — AudioSuite Capture plugin builds with custom Cocoa slot-bank GUI; installed to system Plug-Ins folder; app bridge wired; manual PT Capture/GUI not yet verified
- **Active areas:** [[Areas/Sampler]] · [[Areas/PT-Capture]] · [[Phases/Phase 3 AAX Plugin]]
- **Last build:** AAX `node scripts/build-aax-plugin.js` → installed `/Library/Application Support/Avid/Audio/Plug-Ins/SimplestSamplerAudioSuite.aaxplugin` (2026-06-11)
- **Blockers:** Avid Manufacturer/Product IDs not registered (dev placeholders `JMRB`/`SSmp`); PACE signing before retail PT
- **UI:** OverCue-aligned spacing; dynamic active slots (4–16, drag reorder); Active / Stored / Simple-ish tabs; app syncs `plugin-active-slots.json` with AAX plugin; Impeccable baseline in root `PRODUCT.md` + `DESIGN.md` (north star: "The Session Bench")
- **Notes:** Spec-aligned vault; validate with `python3 scripts/validate_notes.py`
- **Next:** Quit/relaunch PT; verify AudioSuite → SimplestSampler shows dark slot panel (not generic sliders) + **Capture** button; run Capture → Active tab; register Avid IDs + PACE when shipping
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
