# Swift standalone with separate library

**Date:** 2026-06-06  
**Status:** accepted

## Context

OverCue already ships a sampler in Electron. User wanted a standalone native app without modifying OverCue.

## Decision

- Build **SimplestSampler** as native macOS Swift/SwiftUI app
- Use **separate** library at `~/Library/Application Support/SimplestSampler/` (not OverCue's `~/Documents/OverCue/Samples/`)
- OverCue remains read-only reference for behavior parity

## Consequences

- Users of both apps maintain two stored libraries unless we add import/sync later (out of scope)
- Faster path to native app identity; Phase 2 requires vendoring PTSL helper

## Alternatives considered

- Electron fork (faster parity, heavier runtime) — rejected
- Shared OverCue library path — rejected
