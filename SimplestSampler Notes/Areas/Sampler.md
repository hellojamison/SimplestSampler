# Sampler (UI + playback)

Scope:
- Main window layout (Active / Stored / Sound Board tabs, dynamic slots, stored categories, volume, output)
- Playback, trim ranges, drag-drop loading
- Stored library and slot placement
- Shortcut preferences (Settings / gear / ⌘,)

Track here:
- UI polish backlog
- OverCue default theme baseline in `SamplerTheme.swift` (semantic light palette + warm earth dark palette, `SamplerAppTheme.default`, System / Light / Dark selection in Settings)
- Shortcut preferences window now uses a tighter content-sized layout (`minWidth: 392`, `idealWidth: 404`) instead of the previous `minWidth: 460` / `minHeight: 440` frame, with compact shortcut rows and `Settings` scene `.windowResizability(.contentSize)`
- Slot-row play buttons use the semantic accent green gradient in idle rows; selected rows keep inverted chrome for contrast on the selected-row fill
- Slot-row rename field keeps the same row height/spacing as the normal name + filename layout; focused rename chrome is visual only
- Main app window launch sizing now clamps undersized saved frames up to the current tab's preferred height, and Active launch height is derived from the real themed row stack (`48`-point row height + `8`-point row spacing) instead of the stale `42`-point stride estimate, which moved the default Active relaunch from `612x468` to `612x516`; `Active` / `Stored` / `Sound Board` each live inside an explicit vertical scroll region instead of clipping when the window is short
- Playback/output device behavior
- Sound Board tab: 4×4 pad grid (16 clips), drop-to-load, play on click, PT capture to selected/first-empty pad, context-menu clear; pads persist in session state
- Slot state persistence (`UserDefaults`); filled Active rows now start drag from the whole row, with the old left-edge grip icon removed so the play button is the leftmost control, while Active reorder plus cross-tab drag from `Active` into `Stored` / `Sound Board` still work; the main tab pills stay compact and centered while each pill keeps a padded rectangular hit area for clicks plus drag-hover tab switching; Stored tab owns category filter/add/rename/delete/drag assignment via `StoredCategoryBar`, including storing Active clips on drop
- AAX plugin bridge: `plugin-active-slots.json` sync + reload ([[Phases/Phase 3 AAX Plugin]])
- Parity gaps vs OverCue `sampler.js`

Related: [[Areas/PT-Capture]] for Pro Tools capture only.
