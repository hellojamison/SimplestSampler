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

## Reset app state

```bash
defaults delete com.jamisonrabbe.SimplestSampler 2>/dev/null
rm -rf ~/Library/Application\ Support/SimplestSampler
```
