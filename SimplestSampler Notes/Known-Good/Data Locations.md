# Data Locations

**Last verified:** 2026-06-06 (from README / code review — reverify after path changes)

| Path | Purpose |
|------|---------|
| `~/Library/Application Support/SimplestSampler/Samples/` | Stored sample audio files |
| `~/Library/Application Support/SimplestSampler/Samples/.simplestsampler-library.json` | Stored library metadata |
| `~/Library/Application Support/SimplestSampler/Generated Sampler Captures/` | PT multichannel / generated WAVs; AAX Capture output |
| `~/Library/Application Support/SimplestSampler/plugin-active-slots.json` | App ↔ AAX plugin active slot bridge (+ `shortcutBindings`) |
| `UserDefaults` (`com.jamisonrabbe.SimplestSampler`) | Prefs + active slot session state |
| `bin/mac-arm64/ptsl_markers_helper` | Dev helper binary |
| `SimplestSampler.app/.../Resources/bin/ptsl_markers_helper` | Bundled helper |

Separate from OverCue: `~/Documents/OverCue/Samples/` is **not** used.
