---
title: Simple-ish tab with categories
date: 2026-06-10
tags: [sampler, ui]
---

# Simple-ish tab with categories

## Goal

Third main tab **Simple-ish** that lists stored samples with user-defined categories (filter, assign, rename/delete categories).

## Context

Stored tab remains a flat library list. Simple-ish reuses the same `storedCaptures` data with `categoryId` on each capture and `categories` in `.simplestsampler-library.json`.

## Changes

- `SamplerCapture.categoryId`, `StoredCategory`, `SimpleishCategoryFilter` in `SamplerCapture.swift`
- `StoredLibraryMetadata.categories` + CRUD in `StoredLibraryService.swift`
- `SamplerViewModel`: `activeTab == "simpleish"`, `storedCategories`, `simpleishCaptures`, category APIs
- `ContentView`: third tab pill; category bar + filtered rows
- `SimpleishCategoryBar.swift`: filter chips, add/rename/delete categories
- `SlotRowView`: optional category `Menu` per row on Simple-ish tab; drag row onto category chips to assign
- `SlotRowView` + `SamplerTheme`: visible rename editing state (white field, dark text, accent border/shadow) on Active/Stored/Simple-ish rows
- `SlotRowView`: click-away rename commit — `dismissRenameField()` on row tap while editing, on selection change to another row, and before row action buttons; blur still commits via `onChange(of: nameFocused)`
- Session persists `simpleishCategoryFilter` in `SamplerSessionState`

## Verification

- `xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug build` → **BUILD SUCCEEDED**

## Not verified

- Live UI: create category, assign sample (menu or drag-to-chip), filter chips, delete category clears assignments
- Rename highlight readability on selected vs unselected rows (light + dark appearance)
- Click-away rename commit on Active (single-click field) and Stored/Simple-ish (double-click rename)
- Tab restore on relaunch (`activeTab`, category filter)
- Three-tab layout at minimum window width

## Tried and failed

- (none)

## Risks

- Category names only; no drag-reorder or nested folders yet
- Deleting a category moves captures to uncategorized (does not delete samples)
