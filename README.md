# SimplestSampler

Native macOS Swift rebuild of OverCue's Sampler window. This is a standalone app with its own sample library and does not modify OverCue.

## Requirements

- macOS 13.0 or later
- Xcode 15 or later
- Node.js (for native PTSL helper build)
- CMake (`brew install cmake`)
- Pro Tools with PTSL enabled (for capture)

## Build and Run

### Xcode

```bash
open SimplestSampler.xcodeproj
```

Select the **SimplestSampler** scheme, then Run (⌘R). The **Build Native Helper** run script builds `ptsl_markers_helper` on first compile (requires CMake; first build may take several minutes while gRPC is compiled).

### Command line

```bash
# Optional: pre-build the native helper
node scripts/build-native-helper.js

xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build
```

The built app is at:

`build/Build/Products/Debug/SimplestSampler.app`

### Native helper setup

The vendored helper lives under `native/` and is built by `scripts/build-native-helper.js` (copied from OverCue). It registers with Pro Tools as **SimplestSampler**.

- Dev binary: `bin/mac-arm64/ptsl_markers_helper` (or `mac-x64` on Intel)
- Bundled path: `SimplestSampler.app/Contents/Resources/bin/ptsl_markers_helper`

If the helper build is skipped (no Node/CMake), place a prebuilt binary at `bin/mac-$(uname -m)/ptsl_markers_helper` before running.

## Phase 1 Features

- Output device picker (System Default + Core Audio devices)
- 4 Active slots with play, rename, duration label, Store/Delete, Cap/Trigger shortcut editors
- Stored tab with persistent library
- Drag-and-drop audio onto Active slots
- Playback with trim range support (`playbackStartSeconds` / `playbackEndSeconds`)
- Volume slider 0–140% (default 80%, gain = volume/80)
- Option-click volume area resets to 80%
- Global shortcuts while the app is running (defaults: Cmd+F13–F15 capture slots 1–3, F13–F15 play slots 1–3)

## Phase 2 — Pro Tools Capture

Full OverCue-equivalent capture parity:

1. **Direct path (PT 25.x+)**: selected clip file + timeline edit-range math, or single segment resolution
2. **Multichannel**: stereo / multi-mono selections rendered to an interleaved WAV in `Generated Sampler Captures/`
3. **Consolidate fallback**: `--consolidate-clip` helper command, then poll for new clip or session Audio Files entry; AppleScript AX fallback (Shift+Option+3) when consolidate is unsupported

### Capture behavior

- **Capture** toolbar button: always available when SimplestSampler is focused
- **Capture slot shortcuts** (global): require Pro Tools frontmost when SimplestSampler is not focused; always work when the app is focused
- **Play slot shortcuts**: global always
- Target slot from shortcut or toolbar; auto-place fills first empty slot or replaces oldest capture
- Dedupes by `sourceIdentity` and refreshes existing slot when the same source is captured again
- Does not auto-play after capture (loads into the slot for playback)

### Requirements

- Pro Tools running with a session open
- Valid clip or edit selection (Link Track and Edit Selection for range captures)
- Accessibility permission if consolidate AX fallback is needed

### Debug logging

Set `SIMPLESTSAMPLER_CAPTURE_DEBUG=1` in the scheme environment to mirror capture decision logging to stderr.

## Data Locations

- Stored samples: `~/Library/Application Support/SimplestSampler/Samples/`
- Generated PT captures: `~/Library/Application Support/SimplestSampler/Generated Sampler Captures/`
- Library metadata: `~/Library/Application Support/SimplestSampler/Samples/.simplestsampler-library.json`
- Preferences and session state: `UserDefaults`

## Manual Pro Tools Test Matrix

| Scenario | PT version | Expected |
|----------|------------|----------|
| Single mono clip, full file | 25.x | Direct path, no trim |
| Edit selection range on one clip | 25.x | `playbackStart/End` set |
| Stereo multi-mono same range | 25.x | Generated interleaved WAV |
| Capture into slot 2 via shortcut | 25.x | Lands in slot 2 |
| Toolbar Capture (no slot) | 25.x | Auto-place empty/oldest |
| Legacy pre-25 session | 24.x / older | Consolidate path |
| Direct source fails | 25.x | Consolidate fallback within timeout |
| No selection | any | Actionable error message |
| Global capture shortcut with Finder frontmost | any | Silent no-op |

## Project notes

Operational history and session handoff live in [`SimplestSampler Notes/Home.md`](SimplestSampler%20Notes/Home.md) (Obsidian-friendly). Agents should update the daily note and [[Home]] current state after substantive work.

## Reference

Behavior is modeled after OverCue's sampler implementation:

- `/Users/jamisonrabbe/Projects/OverCue/public/sampler.html`
- `/Users/jamisonrabbe/Projects/OverCue/public/js/sampler.js`
- `/Users/jamisonrabbe/Projects/OverCue/main.js` (sampler sections)

OverCue files are read-only reference; this project does not modify them.
