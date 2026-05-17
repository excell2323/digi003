#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
DEXT_BUNDLE_ID="com.axelheckert.driver.FireWireOHCIProbe"
APP="$BUILD_DIR/FireWireOHCIProbeLoader.app"
DEXT="$APP/Contents/Library/SystemExtensions/$DEXT_BUNDLE_ID.dext"
IDENTITY="${1:-${SIGN_IDENTITY:-}}"

if [ -z "$IDENTITY" ]; then
    echo "Usage: SIGN_IDENTITY='Apple Development: Name (TEAMID)' ./scripts/sign-with-identity.sh" >&2
    echo "" >&2
    security find-identity -v -p codesigning >&2 || true
    exit 2
fi

if [ ! -d "$APP" ]; then
    echo "Missing $APP. Run ./scripts/build-host-app.sh first." >&2
    exit 1
fi

clean_attrs() {
    target="$1"
    xattr -cr "$target" 2>/dev/null || true
    find "$target" -name '._*' -delete 2>/dev/null || true
    find "$target" -name '.DS_Store' -delete 2>/dev/null || true
    find "$target" -exec xattr -d com.apple.FinderInfo {} \; 2>/dev/null || true
    find "$target" -exec xattr -d 'com.apple.fileprovider.fpfs#P' {} \; 2>/dev/null || true
}

"$PROJECT_DIR/scripts/embed-profiles.sh"

clean_attrs "$APP"
rm -rf "$DEXT/_CodeSignature"

codesign --force --sign "$IDENTITY" \
    --entitlements "$PROJECT_DIR/Resources/FireWireOHCIProbe.entitlements" \
    "$DEXT"

clean_attrs "$APP"
rm -rf "$APP/Contents/_CodeSignature"

codesign --force --sign "$IDENTITY" \
    --entitlements "$PROJECT_DIR/HostApp/FireWireOHCIProbeLoader.entitlements" \
    "$APP"

clean_attrs "$APP"

codesign --verify --deep --strict --verbose=2 "$APP"
codesign --display --entitlements :- "$APP"
codesign --display --entitlements :- "$DEXT"
