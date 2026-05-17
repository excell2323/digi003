#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
DEXT_BUNDLE_ID="com.axelheckert.driver.FireWireOHCIProbe"
DEXT="$BUILD_DIR/$DEXT_BUNDLE_ID.dext"

if [ ! -d "$DEXT" ]; then
    echo "Missing $DEXT. Run ./scripts/build.sh first." >&2
    exit 1
fi

xattr -cr "$DEXT" 2>/dev/null || true
codesign --force --sign - --entitlements "$PROJECT_DIR/Resources/FireWireOHCIProbe.entitlements" "$DEXT"
codesign --verify --strict --verbose=2 "$DEXT"
codesign --display --entitlements :- "$DEXT"
