---
schema_version: 1
title: Debug build wrapper script
type: task
status: verified
date: 2026-06-11
tags:
  - build
  - tooling
  - runbook
area: build
last_verified: 2026-06-11
freshness: reverify-before-use
related:
  - [[Runbooks/Build and Run]]
  - [[Home]]
---

# Debug build wrapper script

## Goal

Add a practical one-command way to rebuild the `SimplestSampler` Debug app and optionally relaunch it after UI work.

## Context

- Existing docs used the manual command sequence from `README.md` and [[Runbooks/Build and Run]].
- Current app builds use `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug -derivedDataPath build build`.
- The new wrapper should stay close to that flow and fail fast on any `xcodebuild` error.

## Changes

- Added `scripts/build-app.sh`.
- Script defaults to `SimplestSampler` + `Debug`, writes products under `build/`, and exits non-zero on any failed command.
- Added `--open`, `--clean`, `--derived-data-path`, `--configuration`, and `--help` options.
- Resolved the built app bundle path from `xcodebuild -showBuildSettings` before opening it.
- Updated [[Runbooks/Build and Run]] and [[Home]] to point at the wrapper.

## Verification

### Working

- `scripts/build-app.sh` → `** BUILD SUCCEEDED **`
- `scripts/build-app.sh --open` → `** BUILD SUCCEEDED **`
- Resolved app bundle: `/Users/jamisonrabbe/Projects/SimplestSampler/build/Build/Products/Debug/SimplestSampler.app`
- `open` executed successfully from the wrapper after the verified build

## Not verified

- Live UI behavior after launch was not manually inspected in this session.

## Tried and failed

- None.

## Risks

- The script assumes the project/scheme names remain `SimplestSampler`; reverify if the Xcode project structure changes.
- `xcodebuild -showBuildSettings` still emits the usual multiple-destination warning on this machine, but the resolved Debug app path is correct.
