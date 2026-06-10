# Phase 1 Swift app

## Goal

Native macOS Swift rebuild of OverCue Sampler (playback, slots, stored library) as standalone app with separate library path.

## Context

- Workspace: `/Users/jamisonrabbe/Projects/SimplestSampler`
- Reference: OverCue `sampler.html`, `sampler.js`, `main.js` (read-only)
- GitHub: `hellojamison/SimplestSampler`

## Changes

- SwiftUI app: `ContentView`, `SlotRowView`, `SamplerViewModel`, audio services
- Stored library: `~/Library/Application Support/SimplestSampler/Samples/`
- Global shortcuts via Carbon `GlobalShortcutManager`
- Launch rehydrate for `loadedCaptureId`

## Verification

- `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build` — succeeded
- Static code review of Phase 1 checklist (agent)

## Not verified

- Full manual checklist on user hardware (F13 keys, Option-click volume, window frame persist, store-with-trim)

## Tried and failed

- none

## Risks / Follow-ups

- UI polish deferred
- Phase 2 capture stubs replaced in follow-up task note
