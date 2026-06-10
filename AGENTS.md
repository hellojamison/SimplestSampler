# SimplestSampler — agent context

Native macOS Swift rebuild of OverCue's Sampler. OverCue at `/Users/jamisonrabbe/Projects/OverCue` is **read-only reference**.

## Notes

Operational history: [`SimplestSampler Notes/Home.md`](SimplestSampler%20Notes/Home.md)

Keep concise time-stamped notes in `SimplestSampler Notes/` for every substantive task.

Use [`SimplestSampler Notes/Notes System.md`](SimplestSampler%20Notes/Notes%20System.md) as the source of truth for note organization. Daily notes live at the vault root as `YYYY-MM-DD.md`. Larger work should use task notes or structured folders: `Decisions/`, `Incidents/`, `Releases/`, `Runbooks/`, `Known-Good/`, and `Dashboards/`.

Every task should update:

1. today's daily note (`SimplestSampler Notes/YYYY-MM-DD.md`)
2. one relevant task / release / incident / decision note

Always separate:

- **Working** (verified)
- **Not verified**
- **Tried and failed**

Notes must include direct evidence where possible: exact commands, logs, files, test results, package hashes, or manual verification steps.

Notes are a map, not the source of truth for mutable facts. Reverify stale facts before relying on them.

Validate when touching note structure: `python3 scripts/validate_notes.py`

Follow [`.cursor/rules/project-notes.mdc`](.cursor/rules/project-notes.mdc) on substantive sessions.

## Build

See [`README.md`](README.md) and [`SimplestSampler Notes/Runbooks/Build and Run.md`](SimplestSampler%20Notes/Runbooks/Build%20and%20Run.md).
