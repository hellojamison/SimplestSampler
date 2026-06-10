# Phase 2 PT capture

## Goal

Full OverCue-equivalent Pro Tools sampler capture in Swift (direct + multichannel + consolidate fallbacks).

## Context

- Plan: `.cursor/plans/phase_2_pt_capture_7a991caa.plan.md`
- Vendored OverCue `native/` without editing OverCue repo
- `targetSlotIndex` hook existed on `SamplerViewModel`

## Changes

- `native/`, `scripts/build-native-helper.js`, Xcode Build Native Helper phase
- Services: `PTSLHelperClient`, `ProToolsCaptureService`, `MultichannelWaveRenderer`, `ActiveSlotService`, `ConsolidateFallbackService`, `FrontmostAppService`, `PTTimecodeMath`
- README Phase 2 section, `SIMPLESTSAMPLER_CAPTURE_DEBUG=1`
- Generated captures: `~/Library/Application Support/SimplestSampler/Generated Sampler Captures/`

## Verification

- `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build` — **BUILD SUCCEEDED**
- Helper bundled: `SimplestSampler.app/Contents/Resources/bin/ptsl_markers_helper`
- Dev binary: `bin/mac-arm64/ptsl_markers_helper` (~15 MB, SimplestSampler PTSL identity)
- Static decision-tree review matches OverCue paths (direct → multichannel → consolidate)
- **Live PT capture working** (user report, 2026-06-06) — Capture succeeds in real Pro Tools session

## Not verified

- Remaining manual PT test matrix (multichannel, consolidate fallback, pre-25, consolidate timeout, frontmost gating)
- First-time helper build duration / CMake on fresh clone
- Accessibility consolidate fallback with real PT session

## Tried and failed

- Capture could hang indefinitely when PTSL helper subprocess ignored SIGTERM (`waitUntilExit` blocked forever). Fixed by matching OverCue spawn timeout behavior (SIGKILL fallback) plus 90s capture cap.

## Risks / Follow-ups

- Large gRPC helper build time
- Spawn-only helper (no persistent server) — monitor PT connection churn
- Codesign/notarization with embedded helper binary
