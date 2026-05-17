#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
DEXT_BUNDLE_ID="com.axelheckert.driver.FireWireOHCIProbe"
DEXT_NAME="$DEXT_BUNDLE_ID.dext"
APP="$BUILD_DIR/FireWireOHCIProbeLoader.app"
APP_CONTENTS="$APP/Contents"
APP_MACOS="$APP_CONTENTS/MacOS"
APP_SYSEXT="$APP_CONTENTS/Library/SystemExtensions"
DEXT="$BUILD_DIR/$DEXT_NAME"
MAC_SDK="$(xcrun --sdk macosx --show-sdk-path)"
SWIFTC="$(xcrun --sdk macosx --find swiftc)"
DO_ADHOC_SIGN="${ADHOC_SIGN:-0}"

clean_attrs() {
    target="$1"
    xattr -cr "$target" 2>/dev/null || true
    find "$target" -name '._*' -delete 2>/dev/null || true
    find "$target" -name '.DS_Store' -delete 2>/dev/null || true
    find "$target" -exec xattr -d com.apple.FinderInfo {} \; 2>/dev/null || true
    find "$target" -exec xattr -d 'com.apple.fileprovider.fpfs#P' {} \; 2>/dev/null || true
}

BUILD_DIR="$BUILD_DIR" "$PROJECT_DIR/scripts/build.sh"

rm -rf "$APP"
mkdir -p "$APP_MACOS" "$APP_SYSEXT"
cp "$PROJECT_DIR/HostApp/Info.plist" "$APP_CONTENTS/Info.plist"
cp -R "$DEXT" "$APP_SYSEXT/$DEXT_NAME"

"$SWIFTC" \
    -sdk "$MAC_SDK" \
    -target arm64-apple-macos15.0 \
    -framework AppKit \
    -framework SystemExtensions \
    "$PROJECT_DIR/HostApp/FireWireOHCIProbeLoader.swift" \
    -o "$APP_MACOS/FireWireOHCIProbeLoader"

clean_attrs "$APP"
rm -rf "$APP_SYSEXT/$DEXT_NAME/_CodeSignature"

if [ "$DO_ADHOC_SIGN" = "1" ]; then
    codesign --force --sign - \
        --entitlements "$PROJECT_DIR/Resources/FireWireOHCIProbe.entitlements" \
        "$APP_SYSEXT/$DEXT_NAME"
fi

clean_attrs "$APP"
rm -rf "$APP_CONTENTS/_CodeSignature"

if [ "$DO_ADHOC_SIGN" = "1" ]; then
    codesign --force --sign - \
        --entitlements "$PROJECT_DIR/HostApp/FireWireOHCIProbeLoader.entitlements" \
        "$APP"
fi

clean_attrs "$APP"

if [ "$DO_ADHOC_SIGN" = "1" ]; then
    codesign --verify --deep --strict --verbose=2 "$APP"
fi
echo "Built host app: $APP"
