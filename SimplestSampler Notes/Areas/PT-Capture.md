# PT Capture

Scope:
- `ProToolsCaptureService`, `ConsolidateFallbackService`, `MultichannelWaveRenderer`
- `ActiveSlotService` placement (`rememberSamplerCapture` parity)
- Capture toolbar + per-slot capture shortcuts
- `FrontmostAppService` gating for global capture shortcuts

## Known-good

- Live PT capture succeeds (user verified 2026-06-06)

Track here:
- Direct vs multichannel vs consolidate path decisions
- Protocol/version-specific behavior (pre-25 vs 25.x)
- Error messages (Link Track and Edit Selection, slot unavailable)
- `SIMPLESTSAMPLER_CAPTURE_DEBUG=1` traces

Reference (read-only): OverCue `main.js` — `captureSelectedPtConsolidatedFileInSampler`, `resolveSamplerFileFromSelectedPtRange`.
