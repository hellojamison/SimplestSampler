# Notes System

Operational history with evidence for **SimplestSampler** — not a diary.

A note should never say “fixed bug” without evidence. It should say what changed and how we know it worked.

Use the **repo** as source of code truth. Use **`SimplestSampler Notes/`** as source of operational history.

## Purpose

Capture project state that is easy to lose in chat, terminal scrollback, or memory:

- what changed
- why it changed
- what was verified
- what failed
- what is still unknown
- what commands, logs, hashes, routes, files, or test results support the claim

## Folder layout

```text
SimplestSampler Notes/
  Home.md
  Notes System.md
  YYYY-MM-DD.md
  YYYY-MM-DD-slug.md          # task notes at vault root (or in typed folders)
  Areas/                      # long-lived feature context (project extension)
  Phases/                     # phase checklists / exit criteria (project extension)
  Templates/
  Decisions/
  Incidents/
  Releases/
  Runbooks/
  Known-Good/
  Dashboards/
```

## Daily notes

Daily notes live at the vault root: `YYYY-MM-DD.md`.

Every substantive task adds a short timestamped entry. These are the timeline — not essays.

```markdown
# YYYY-MM-DD

- HH:MM: Summary. Working: verified facts. Not verified: gaps. Tried and failed: dead ends (or none). → [[YYYY-MM-DD-task-slug]]
```

Daily notes answer: **What happened today, and where should I look for details?**

## Task notes

For any real feature, bug, release, investigation, or workflow change, create or update a task-specific note.

Copy [[Templates/Task Note Template]]. Recommended filename: `YYYY-MM-DD-short-slug.md`.

The task note holds detail. The daily note links or points to it.

### Recommended sections

| Section | Content |
| ------- | ------- |
| Goal | What we are trying to accomplish |
| Context | Files, commands, prior decisions |
| Changes | What changed and where |
| Verification | **Working** — `xcodebuild`, manual PT checks, command output |
| Not verified | What may be true but has not been checked |
| Tried and failed | Approaches attempted that did not work |
| Risks / Follow-ups | Blockers, cleanup |

## Required distinctions

Every useful note separates these three — **never mix them in one paragraph**:

### Working

What has been directly verified.

### Not verified

What may be true but has not been checked yet.

### Tried and failed

Approaches that were attempted and did not work.

## Frontmatter

Use consistent YAML frontmatter so notes can be searched, filtered, and validated.

**Recommended required fields:**

```yaml
schema_version: 1
title:
type:
status:
date:
tags:
```

**Recommended optional fields:**

```yaml
area:
last_verified:
freshness:
superseded_by:
related:
```

**Good `type` values:** `daily`, `task`, `decision`, `incident`, `release`, `runbook`, `known-good`, `index`, `template`

**Good `status` values:** `draft`, `active`, `blocked`, `verified`, `stale`, `superseded`

Use `freshness: reverify-before-use` for facts likely to drift: deploy URLs, package versions, SDK behavior, release state, notarization, helper paths, PT version-specific capture behavior.

## Folder rules

| Folder | Use |
| ------ | --- |
| `Decisions/` | Architecture, product, workflow decisions. Decision, alternatives, why. |
| `Incidents/` | Failures, broken releases, tester reports, confusing bugs. Impact, evidence, root cause, mitigation, prevention. |
| `Releases/` | Packaged builds. Version, commit, packaging commands, signing/notarization, hashes, upload status, release notes. |
| `Runbooks/` | Repeatable procedures someone may need again. |
| `Known-Good/` | Verified baselines: working commands, env assumptions, supported versions, data paths. |
| `Dashboards/` | Index views: open loops, stale notes, recent work (optional). |
| `Templates/` | Copyable note templates. |
| `Areas/` | Long-lived feature context (sampler UI, PT capture, native helper). |
| `Phases/` | Phase checklists and exit criteria. |

## Evidence standard

Cite direct evidence where possible:

- exact command run (`xcodebuild -project … build`)
- test or build result (**BUILD SUCCEEDED** / failure snippet)
- log line summary
- file path changed
- package hash or helper binary path
- manual Pro Tools verification step

**Bad:** Looks fixed.

**Better:** `xcodebuild` Debug BUILD SUCCEEDED; helper at `Contents/Resources/bin/ptsl_markers_helper`. Not verified: live PT consolidate on pre-25 session.

## Mutable facts

Notes are a map, not the final source of truth.

If a fact can change, re-check the real surface before relying on an old note. Record `last_verified` when you re-check.

Examples: build status, helper bundle path, PT capture matrix results, release upload state, notarization.

## Validation

```bash
python3 scripts/validate_notes.py
python3 scripts/validate_notes.py --strict-all
```

- **Normal mode** — daily bullet format, template frontmatter, warnings on notes that should have frontmatter.
- **Strict mode** — audit: frontmatter required on structured task/decision/incident/release notes; use for reviews, not to block everyday work on legacy notes.

## Commit hygiene

Commit notes when they are part of the work, staged surgically. Do not sweep unrelated notes, build output, or generated artifacts into the same commit.

## Agent discipline

On every substantive session:

1. Update today's `SimplestSampler Notes/YYYY-MM-DD.md`
2. Create or update one task / release / incident / decision note for the actual work
3. Record changes while doing the work, not later from memory
4. Record failed attempts if they would save someone time
5. Before finishing, write what is **working** and what is **not verified**
6. Run `python3 scripts/validate_notes.py` when touching note structure or templates
7. Update [[Home]] **Current state** at session end
8. Touch `Areas/` or `Phases/` when baseline behavior changes

Follow `.cursor/rules/project-notes.mdc`. Use the `simplestsampler-notes` skill when logging from Codex.

OverCue at `/Users/jamisonrabbe/Projects/OverCue` is **read-only reference**. Never store secrets in notes.
