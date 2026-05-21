#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
APP="$BUILD_DIR/Digi003MIDIBridge.app"
APP_CONTENTS="$APP/Contents"
APP_MACOS="$APP_CONTENTS/MacOS"
DO_ADHOC_SIGN="${ADHOC_SIGN:-0}"

clean_attrs() {
    target="$1"
    xattr -cr "$target" 2>/dev/null || true
    find "$target" -name '._*' -delete 2>/dev/null || true
    find "$target" -name '.DS_Store' -delete 2>/dev/null || true
    find "$target" -exec xattr -d com.apple.FinderInfo {} \; 2>/dev/null || true
    find "$target" -exec xattr -d 'com.apple.fileprovider.fpfs#P' {} \; 2>/dev/null || true
}

"$PROJECT_DIR/scripts/build-tools.sh"

rm -rf "$APP"
mkdir -p "$APP_MACOS"
cp "$PROJECT_DIR/BridgeApp/Info.plist" "$APP_CONTENTS/Info.plist"
cp "$PROJECT_DIR/Tools/bin/digi003-midi-bridge" "$APP_MACOS/Digi003MIDIBridge"
chmod 755 "$APP_MACOS/Digi003MIDIBridge"

clean_attrs "$APP"
rm -rf "$APP_CONTENTS/_CodeSignature"

if [ "$DO_ADHOC_SIGN" = "1" ]; then
    codesign --force --sign - \
        --entitlements "$PROJECT_DIR/BridgeApp/Digi003MIDIBridge.entitlements" \
        "$APP"
    codesign --verify --deep --strict --verbose=2 "$APP"
fi

echo "Built MIDI bridge app: $APP"
