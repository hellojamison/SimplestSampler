#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

PROJECT="SimplestSampler.xcodeproj"
SCHEME="SimplestSampler"
CONFIGURATION="Debug"
DERIVED_DATA_PATH="$REPO_ROOT/build"
OPEN_APP=0
CLEAN_BUILD=0

usage() {
  cat <<'USAGE'
Usage: scripts/build-app.sh [options]

Builds the SimplestSampler macOS app with xcodebuild.

Options:
  --open                 Open the built .app after a successful build
  --clean                Run a clean build
  --derived-data-path    Override the derived data/build output path
  --configuration NAME   Build configuration (default: Debug)
  --help                 Show this help
USAGE
}

while [[ $# -gt 0 ]]; do
  case "$1" in
    --open)
      OPEN_APP=1
      shift
      ;;
    --clean)
      CLEAN_BUILD=1
      shift
      ;;
    --derived-data-path)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --derived-data-path" >&2
        usage >&2
        exit 1
      fi
      DERIVED_DATA_PATH="$2"
      shift 2
      ;;
    --configuration)
      if [[ $# -lt 2 ]]; then
        echo "Missing value for --configuration" >&2
        usage >&2
        exit 1
      fi
      CONFIGURATION="$2"
      shift 2
      ;;
    --help|-h)
      usage
      exit 0
      ;;
    *)
      echo "Unknown option: $1" >&2
      usage >&2
      exit 1
      ;;
  esac
done

mkdir -p "$DERIVED_DATA_PATH"

XCODE_ARGS=(
  -project "$PROJECT"
  -scheme "$SCHEME"
  -configuration "$CONFIGURATION"
  -derivedDataPath "$DERIVED_DATA_PATH"
)

BUILD_ACTIONS=(build)
if [[ $CLEAN_BUILD -eq 1 ]]; then
  BUILD_ACTIONS=(clean build)
fi

cd "$REPO_ROOT"

echo "Building $SCHEME ($CONFIGURATION)..."
xcodebuild "${XCODE_ARGS[@]}" "${BUILD_ACTIONS[@]}"

BUILD_SETTINGS=$(xcodebuild "${XCODE_ARGS[@]}" -showBuildSettings)
BUILT_PRODUCTS_DIR=$(printf '%s\n' "$BUILD_SETTINGS" | awk -F ' = ' '/^[[:space:]]*BUILT_PRODUCTS_DIR = / { print $2; exit }')
FULL_PRODUCT_NAME=$(printf '%s\n' "$BUILD_SETTINGS" | awk -F ' = ' '/^[[:space:]]*FULL_PRODUCT_NAME = / { print $2; exit }')

if [[ -z "$BUILT_PRODUCTS_DIR" || -z "$FULL_PRODUCT_NAME" ]]; then
  echo "Could not determine built app path from xcodebuild settings." >&2
  exit 1
fi

APP_PATH="$BUILT_PRODUCTS_DIR/$FULL_PRODUCT_NAME"

if [[ ! -d "$APP_PATH" ]]; then
  echo "Build finished but app bundle was not found at: $APP_PATH" >&2
  exit 1
fi

echo "App bundle: $APP_PATH"

if [[ $OPEN_APP -eq 1 ]]; then
  echo "Opening app..."
  open "$APP_PATH"
fi
