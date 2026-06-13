#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR=$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)
REPO_ROOT=$(cd "$SCRIPT_DIR/.." && pwd)

PROJECT="SimplestSampler.xcodeproj"
SCHEME="SimplestSampler"
CONFIGURATION="Debug"
DERIVED_DATA_PATH="$REPO_ROOT/build"

usage() {
  cat <<'USAGE'
Usage: scripts/run-app.sh

Opens the last built SimplestSampler.app without rebuilding.

If the app is missing, run: scripts/build-app.sh
USAGE
}

if [[ "${1:-}" == "--help" || "${1:-}" == "-h" ]]; then
  usage
  exit 0
fi

if [[ $# -gt 0 ]]; then
  echo "Unknown option: $1" >&2
  usage >&2
  exit 1
fi

find_app_path() {
  local build_settings built_products_dir full_product_name

  build_settings=$(xcodebuild \
    -project "$REPO_ROOT/$PROJECT" \
    -scheme "$SCHEME" \
    -configuration "$CONFIGURATION" \
    -derivedDataPath "$DERIVED_DATA_PATH" \
    -showBuildSettings 2>/dev/null || true)

  built_products_dir=$(printf '%s\n' "$build_settings" | awk -F ' = ' '/^[[:space:]]*BUILT_PRODUCTS_DIR = / { print $2; exit }')
  full_product_name=$(printf '%s\n' "$build_settings" | awk -F ' = ' '/^[[:space:]]*FULL_PRODUCT_NAME = / { print $2; exit }')

  if [[ -n "$built_products_dir" && -n "$full_product_name" && -d "$built_products_dir/$full_product_name" ]]; then
    echo "$built_products_dir/$full_product_name"
    return 0
  fi

  return 1
}

APP_PATH=""
if APP_PATH=$(find_app_path); then
  :
else
  echo "SimplestSampler.app not found. Build first with: scripts/build-app.sh" >&2
  exit 1
fi

echo "Opening $APP_PATH"
open "$APP_PATH"
