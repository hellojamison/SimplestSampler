# SimplestSampler

Native macOS Swift rebuild of OverCue's Sampler window. This is a standalone app with its own sample library and does not modify OverCue.

## Requirements

- macOS 13.0 or later
- Xcode 15 or later

## Build and Run

### Xcode

```bash
open SimplestSampler.xcodeproj
```

Select the **SimplestSampler** scheme, then Run (⌘R).

### Command line

```bash
xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build
```

The built app is at:

`build/Build/Products/Debug/SimplestSampler.app`

## Phase 1 Features

- Output device picker (System Default + Core Audio devices)
- 4 Active slots with play, rename, duration label, Store/Delete, Cap/Trigger shortcut editors
- Stored tab with persistent library
- Drag-and-drop audio onto Active slots
- Playback with trim range support (`playbackStartSeconds` / `playbackEndSeconds`)
- Volume slider 0–140% (default 80%, gain = volume/80)
- Option-click volume area resets to 80%
- Global shortcuts while the app is running (defaults: Cmd+F13–F15 capture slots 1–3, F13–F15 play slots 1–3)
- Capture button shows Phase 2 message (no Pro Tools integration yet)

## Data Locations

- Stored samples: `~/Library/Application Support/SimplestSampler/Samples/`
- Library metadata: `~/Library/Application Support/SimplestSampler/Samples/.simplestsampler-library.json`
- Preferences and session state: `UserDefaults`

## Reference

Behavior is modeled after OverCue's sampler implementation:

- `/Users/jamisonrabbe/Projects/OverCue/public/sampler.html`
- `/Users/jamisonrabbe/Projects/OverCue/public/js/sampler.js`
- `/Users/jamisonrabbe/Projects/OverCue/main.js` (sampler sections)

OverCue files are read-only reference; this project does not modify them.

## Phase 2 (Deferred)

- Pro Tools capture via PTSL helper
- Capture slot global shortcuts wired to real PT capture
- Multichannel segment rendering and consolidate fallback paths
