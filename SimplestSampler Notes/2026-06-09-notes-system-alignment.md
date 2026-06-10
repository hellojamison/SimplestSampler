---
schema_version: 1
title: Notes system alignment
type: task
status: verified
date: 2026-06-09
tags: [notes, meta]
area: documentation
last_verified: 2026-06-09
---

# Notes system alignment

## Goal

Align `SimplestSampler Notes/` with the project notes specification: frontmatter, evidence standard, folder rules, validator, and contributor instructions.

## Context

Vault existed with daily notes, templates, Areas/Phases, and agent rules — but lacked `Dashboards/`, YAML frontmatter on templates, full spec in `Notes System.md`, and `scripts/validate_notes.py`.

## Changes

- Expanded `SimplestSampler Notes/Notes System.md` (purpose, frontmatter, evidence, mutable facts, validation, commit hygiene)
- Templates: YAML frontmatter + Release/Runbook/Daily templates
- `SimplestSampler Notes/Dashboards/Open Loops.md` index
- `scripts/validate_notes.py` (normal + `--strict-all`)
- `AGENTS.md` starter instruction block
- `.cursor/rules/project-notes.mdc` validator + evidence references

## Verification

- `python3 scripts/validate_notes.py` — OK (warnings only for legacy task notes without frontmatter)

## Not verified

- Obsidian Bases views on Dashboards (manual setup if desired)

## Tried and failed

- none

## Risks / Follow-ups

- Legacy root task notes (`2026-06-06-*.md`, `2026-06-09-dark-mode.md`) lack frontmatter; add when editing those notes or run `--strict-all` in audits only
