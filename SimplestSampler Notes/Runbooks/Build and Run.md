# Build and Run

## Xcode

```bash
open /Users/jamisonrabbe/Projects/SimplestSampler/SimplestSampler.xcodeproj
```

Scheme: **SimplestSampler** → ⌘R

First build runs **Build Native Helper** (CMake + gRPC; can take several minutes).

## Command line

```bash
cd /Users/jamisonrabbe/Projects/SimplestSampler
node scripts/build-native-helper.js   # optional pre-build
xcodebuild -project SimplestSampler.xcodeproj -scheme SimplestSampler -configuration Debug -derivedDataPath build build
open build/Build/Products/Debug/SimplestSampler.app
```

## Capture debug

Set scheme environment: `SIMPLESTSAMPLER_CAPTURE_DEBUG=1`

## AAX AudioSuite plugin

Requires AAX SDK 2.9.0 (default: `~/Downloads/aax-sdk-2-9-0`). SDK is **not** committed.

```bash
cd /Users/jamisonrabbe/Projects/SimplestSampler
export AAX_SDK_ROOT="$HOME/Downloads/aax-sdk-2-9-0"

# One-shot build (configures SDK + plugin, builds AAX_Export, AAXLibrary, SimplestSamplerAudioSuite)
node scripts/build-aax-plugin.js
```

Manual steps (equivalent):

```bash
cmake -B out/aax-sdk-build -S "$AAX_SDK_ROOT" -G Xcode -DAAX_BUILD_EXAMPLES=OFF
cmake --build out/aax-sdk-build --config Release --target AAX_Export AAXLibrary

cmake -B out/aax -S aax -G Xcode \
  -DAAX_SDK_ROOT="$AAX_SDK_ROOT" \
  -DAAX_SDK_DIR=out/aax-sdk-build
cmake --build out/aax --config Release --target SimplestSamplerAudioSuite
```

Output bundle:

`out/aax/SimplestSamplerAudioSuite/Release/SimplestSamplerAudioSuite.aaxplugin`

`node scripts/build-aax-plugin.js` builds and installs to `/Library/Application Support/Avid/Audio/Plug-Ins/` (adhoc codesign). Pro Tools does **not** scan `~/Library/Application Support/Avid/Audio/Plug-Ins/`.

Override install dir: `AAX_PLUGIN_INSTALL_DIRECTORY=...` · skip install: `AAX_SKIP_INSTALL=1`

### Dev PT smoke test (manual)

1. Select clip or edit range in Pro Tools
2. AudioSuite → **Other** (or search) → **SimplestSampler**
3. Confirm footer button reads **Capture** (not Analyze)
4. Choose target slot in plugin parameters → Capture
5. Confirm WAV under `~/Library/Application Support/SimplestSampler/Generated Sampler Captures/`
6. With SimplestSampler.app running, confirm Active tab updates from `plugin-active-slots.json`

### Before retail release

- Register 4-char Manufacturer + Product IDs at [developer.avid.com/audio](https://developer.avid.com/audio); update `aax/SimplestSamplerAudioSuite/Source/SimplestSampler_AS_Defs.h`
- PACE signing via `audiosdk@avid.com`

## Reset app state

```bash
defaults delete com.jamisonrabbe.SimplestSampler 2>/dev/null
rm -rf ~/Library/Application\ Support/SimplestSampler
```
