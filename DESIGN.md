---
name: SimplestSampler
description: Warm, compact macOS sampler UI for Pro Tools capture and playback
colors:
  light-bg-top: "#F4FBFF"
  light-bg-bottom: "#E3EEF3"
  light-panel: "#FFFFFFD6"
  light-text: "#193646"
  light-muted: "#5D7684"
  light-accent: "#6F8F80"
  light-accent-soft: "#C7D8CF"
  light-capture-top: "#F0B9BC"
  light-capture-bottom: "#D8898F"
  light-slot-bg: "#F8FCFDEA"
  light-slot-selected: "#6F817AEB"
  light-border: "#274E5E24"
  dark-bg-top: "#3B3835"
  dark-bg-bottom: "#282624"
  dark-panel: "#2E2B29EB"
  dark-text: "#DDD7D1"
  dark-muted: "#B3ADA7"
  dark-accent: "#2C7A5F"
  dark-accent-soft: "#8CAB9B"
  dark-capture-top: "#8A4545"
  dark-capture-bottom: "#6B3434"
  dark-slot-bg: "#403C39E6"
  dark-slot-selected: "#346E56F5"
  error-light: "#B33333"
  error-dark: "#E49C9C"
typography:
  title:
    fontFamily: "SF Pro, -apple-system, system-ui, sans-serif"
    fontSize: "15px"
    fontWeight: 700
    lineHeight: 1.2
  toolbar:
    fontFamily: "SF Pro, -apple-system, system-ui, sans-serif"
    fontSize: "13px"
    fontWeight: 600
    lineHeight: 1.2
  label:
    fontFamily: "SF Pro, -apple-system, system-ui, sans-serif"
    fontSize: "11px"
    fontWeight: 600
    lineHeight: 1.2
  body:
    fontFamily: "SF Pro, -apple-system, system-ui, sans-serif"
    fontSize: "13px"
    fontWeight: 600
    lineHeight: 1.3
  meta:
    fontFamily: "SF Pro, -apple-system, system-ui, sans-serif"
    fontSize: "10px"
    fontWeight: 500
    lineHeight: 1.2
  mono-meta:
    fontFamily: "SF Mono, ui-monospace, Menlo, monospace"
    fontSize: "10px"
    fontWeight: 500
    lineHeight: 1.2
rounded:
  chip: "8px"
  row: "11px"
  panel: "12px"
  volume: "12px"
  rename-field: "6px"
  tab-capsule: "999px"
spacing:
  window: "8px"
  section: "8px"
  row: "6px"
  chip-h: "9px"
  chip-v: "5px"
  row-h: "9px"
  row-v: "6px"
  toolbar-gap: "6px"
components:
  button-toolbar:
    backgroundColor: "{colors.light-panel}"
    textColor: "{colors.light-text}"
    rounded: "{rounded.chip}"
    padding: "9px 12px"
  button-capture:
    backgroundColor: "{colors.light-capture-top}"
    textColor: "{colors.light-text}"
    rounded: "{rounded.chip}"
    padding: "9px 16px"
  chip-surface:
    backgroundColor: "{colors.light-panel}"
    textColor: "{colors.light-muted}"
    rounded: "{rounded.chip}"
    padding: "5px 9px"
  slot-row:
    backgroundColor: "{colors.light-slot-bg}"
    textColor: "{colors.light-text}"
    rounded: "{rounded.row}"
    padding: "6px 9px"
  slot-row-selected:
    backgroundColor: "{colors.light-slot-selected}"
    textColor: "#F5FFF9"
    rounded: "{rounded.row}"
    padding: "6px 9px"
  tab-active:
    backgroundColor: "{colors.light-accent-soft}"
    textColor: "{colors.light-text}"
    rounded: "{rounded.tab-capsule}"
    padding: "6px 12px"
---

# Design System: SimplestSampler

## Overview

**Creative North Star: "The Session Bench"**

SimplestSampler looks and feels like dependable bench gear in an edit suite: warm neutrals, soft gradients, rounded continuous corners, and a single confident teal accent. The layout is a vertical stack — output bar, toolbar, tab capsule, scrollable slot list — with no nested cards or decorative chrome. Information density is high but legible; every row is a self-contained unit (play, metadata, volume, shortcuts, actions).

The system inherits OverCue sampler DNA (earth-tone dark theme, rose capture gradient, sage accent) and extends it to a native SwiftUI app plus a matching Cocoa AAX panel. Visual hierarchy favors the slot list over chrome; the Capture action is the only strong warm gradient on screen.

**Key Characteristics:**

- Warm tinted neutrals (not cool gray SaaS defaults)
- System SF Pro typography at 10–15px scale for compact macOS utility UI
- Tonal layering and 1px hairline borders instead of drop shadows
- Continuous corner radii (8px chips, 11px rows, 12px panels)
- Dual theme (light airy / dark workshop) with shared semantic roles
- Capture gradient as the sole high-saturation warm accent

## Colors

The palette splits into **environment** (background gradient, panels), **work** (slots, chips, tabs), **signal** (accent teal, capture rose), and **state** (selected fill, playing tint, error).

### Primary

- **Sage Bench** (`#6F8F80` light / `#2C7A5F` dark): Primary accent for selected-tab hints, rename borders, and positive emphasis. Reads as calm "go" without neon green.

### Secondary

- **Rose Capture** (`#F0B9BC` → `#D8898F` light / `#8A4545` → `#6B3434` dark): Exclusive to Capture toolbar button and shortcut-capturing state. Gradient top-to-bottom, never used for passive surfaces.

### Tertiary

- **Soft Sage Wash** (`#C7D8CF` light / `#8CAB9B` dark): Playing-slot tint, tab-active background in light mode, low-contrast state fills.

### Neutral

- **Mist Gradient** (`#F4FBFF` → `#E3EEF3` light): App window background; cool-tinted sky fade, not cream/paper.
- **Workshop Floor** (`#3B3835` → `#282624` dark): Dark window background; warm brown-gray from OverCue `darkTheme.css`.
- **Ink** (`#193646` light / `#DDD7D1` dark): Primary text.
- **Bench Label** (`#5D7684` light / `#B3ADA7` dark): Muted labels, secondary meta.
- **Hairline** (`#274E5E24` light / `#FFFFFF1C` dark): 1px borders on chips, panels, rows.

### Named Rules

**The One Warm Gradient Rule.** Only Capture (and capturing shortcut state) may use the rose gradient. No other buttons, badges, or headers get warm gradients.

**The Teal Sparingly Rule.** Full saturated accent fill appears on selected slot rows and key positive actions — not on every icon or label.

## Typography

**Display Font:** SF Pro (system) — not used at display scale; largest UI type is 15px bold panel titles.

**Body Font:** SF Pro (system) — all UI labels, slot names, toolbar buttons.

**Label/Mono Font:** SF Mono for duration/timecode meta in slot rows only.

**Character:** Native macOS utility sizing — semibold for actionable text, regular/medium for meta. No custom webfonts; hierarchy is size and weight, not family pairing.

### Hierarchy

- **Title** (700, 15px, 1.2): Panel headers (AAX plugin title, section emphasis).
- **Toolbar** (600, 13px, 1.2): Play, Capture, Stop, primary actions.
- **Body** (600, 13px, 1.3): Slot names, tab labels when active.
- **Label** (600, 11px, 1.2): Field labels ("Output", "Active slots"), category chips.
- **Meta** (500, 10px, 1.2): Durations, shortcut hints, footnotes.
- **Mono meta** (500, 10px monospaced): Time durations in slot rows.

### Named Rules

**The System Scale Rule.** Stay within 9–15px for working UI. No hero type, no tracked uppercase eyebrows.

## Elevation

Flat-by-default. Depth comes from **tonal layering** (panel over gradient background, chip over panel), **1px hairline borders**, and **selected-row fill shifts** — not drop shadows.

AAX Cocoa panel uses solid `ThemeBackground()` fill; SwiftUI app uses vertical `backgroundGradient`. Neither surface uses `box-shadow` or glass blur.

### Named Rules

**The No-Shadow Rule.** Do not add drop shadows to slots, chips, or buttons. If something needs emphasis, change fill or border contrast.

## Components

### Buttons

- **Shape:** Continuous rounded rect, 8px (`chipCornerRadius`).
- **Toolbar:** White/translucent fill (light) or `#5C5854` (dark); 13px semibold; pressed state reduces opacity.
- **Capture:** Rose vertical gradient fill; same radius; disabled when capture in progress.
- **Icon (gear, add slot):** 34×34pt hit target, chip surface styling.

### Chips

- **Style:** Translucent white or dark slot-bg fill, 1px border, 8px radius.
- **Use:** Output bar, category filters (Simple-ish tab), compact metadata containers.

### Cards / Containers

- **Panel modifier (`samplerPanel`):** 10px padding, 12px radius, panel background, 1px border — used sparingly for grouped settings.
- **Not used:** Nested cards inside slot rows.

### Slot rows

- **Corner:** 11px continuous radius (matches AAX `SimplestSamplerSlotRowView`).
- **Default:** `slotBackground` fill, ink text, muted meta.
- **Selected:** `slotSelected` fill, `slotSelectedText` (#F5FFF9).
- **Playing:** `slotPlaying` soft sage wash overlay.
- **Layout:** Play 22×22 → name/duration column → volume slider → shortcut editors → Store/Delete actions.
- **Spacing:** 6px row gap, 9px horizontal padding.

### Tabs

- **Style:** Capsule bar (`tabBarBackground`) with pill buttons inside.
- **Active:** Accent-soft fill (light) or implicit weight shift; 12px semibold label.

### Inputs / Fields

- **Rename field:** White fill, 6px radius, accent-tinted border on focus; 11px semibold.

### Navigation

- Three tabs: Active, Stored, Simple-ish — horizontal capsule, left-aligned, not sidebar nav.

### AAX plugin panel

- **Size:** 300×340pt fixed panel.
- **Parity:** Dark theme colors mirror `SamplerTheme.dark`; slot rows use same 11px radius and selected teal fill.

## Do's and Don'ts

### Do:

- **Do** use `SamplerTheme.swift` as the single source of truth for SwiftUI colors, spacing, and radii.
- **Do** mirror dark-theme hex values in `SimplestSampler_AS_GUI.mm` when changing the AAX panel.
- **Do** keep Capture as the only rose-gradient control.
- **Do** use 1px `theme.border` strokes on chips and panels for definition without shadow.
- **Do** respect macOS system light/dark/system appearance via `SamplerThemeMode`.

### Don't:

- **Don't** introduce card-in-card layouts or identical icon+heading+text grids.
- **Don't** use purple gradients, glassmorphism, gradient text, or neon accent floods.
- **Don't** add marketing-page tropes (tracked uppercase eyebrows, hero metrics, decorative motion).
- **Don't** let the AAX plugin drift to generic Avid slider chrome — keep custom slot-bank GUI.
- **Don't** use drop shadows or side-stripe accent borders on slot rows.
