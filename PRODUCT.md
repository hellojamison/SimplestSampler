# Product

## Register

product

## Users

Post-production sound editors, mixers, and music editors who work in Pro Tools and need fast, repeatable access to short audio clips during a session. They use SimplestSampler while Pro Tools is frontmost or while the standalone app is focused — often under time pressure, with headphones on, glancing between the DAW timeline and a compact sampler window or AudioSuite plugin panel.

## Product Purpose

SimplestSampler is a native macOS sampler that captures audio from Pro Tools selections into playable slots, with optional persistence to a stored library. It exists to remove friction from the capture → audition → reuse loop that OverCue's sampler solved in Electron, rebuilt as a focused macOS tool with PTSL capture parity and an in-DAW AudioSuite surface.

Success looks like: capture lands in the intended slot on the first try; playback is instant and reliable; shortcuts work globally without stealing focus from the session; the UI stays readable in long sessions (light or dark); app and AAX plugin stay visually and behaviorally aligned.

## Brand Personality

**Practical · Warm · Unhurried**

Voice is direct and utilitarian — labels say what they do ("Capture", "Store", "Active slots"). Emotional goal is calm competence: the tool feels like bench equipment in a well-lit edit suite, not a consumer toy or a flashy plugin marketing panel. Warm earth-and-teal tones (inherited from OverCue) signal familiarity for existing users without shouting for attention.

## Anti-references

- Generic SaaS dashboards (card grids, hero metrics, purple gradients, glassmorphism)
- Neon DAW skins, hyper-saturated accent floods, or "gamer RGB" capture buttons
- Tiny illegible controls crammed into plugin chrome
- Marketing landing-page aesthetics inside the working UI (tracked eyebrows, gradient text, decorative motion)
- Visual drift between the standalone app and the AAX plugin panel

## Design Principles

1. **Speed is the feature** — Every screen optimizes for capture, play, and slot targeting in fewest clicks; decoration never competes with the slot list.
2. **Session-safe** — Global shortcuts and capture behavior respect Pro Tools focus; errors are actionable, not theatrical.
3. **One visual language, two surfaces** — SwiftUI app and Cocoa AAX GUI share palette, spacing rhythm, and slot-row affordances from `SamplerTheme.swift`.
4. **Density without clutter** — Compact rows, clear hierarchy (slot number → name → duration → actions), chips and panels instead of nested cards.
5. **Theme follows the room** — System/light/dark modes with warm neutrals; capture actions use a distinct rose gradient so they read as "record" without alarm-red panic.

## Accessibility & Inclusion

- Target WCAG 2.1 AA contrast for body text and controls in both light and dark themes
- Respect macOS Reduce Motion; avoid gating content visibility on entrance animations
- Color is never the sole indicator of state (selected slots use fill + text contrast, not hue alone)
- Keyboard and shortcut-first workflows for users who avoid mouse during playback
