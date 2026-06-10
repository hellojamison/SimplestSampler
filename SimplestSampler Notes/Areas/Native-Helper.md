# Native Helper

Scope:
- Vendored `native/` + `scripts/build-native-helper.js`
- `PTSLHelperClient` spawn-only CLI
- Bundle path: `Contents/Resources/bin/ptsl_markers_helper`
- PTSL registration as **SimplestSampler**

Track here:
- Helper build failures (CMake, gRPC compile time)
- CLI commands used by capture
- Codesign / notarization with embedded binary
- SDK path / `PTSL_SDK_CPP` setup

Commands used for capture:
- `--ping`, `--get-active-protocol`, `--get-session-path`
- `--get-timeline-selection`, `--get-selected-clip-file`, `--get-selected-clip-segments`
- `--consolidate-clip`
