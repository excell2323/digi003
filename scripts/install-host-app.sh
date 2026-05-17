#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
SOURCE_APP="$BUILD_DIR/FireWireOHCIProbeLoader.app"
TARGET_APP="/Applications/FireWireOHCIProbeLoader.app"

if [ ! -d "$SOURCE_APP" ]; then
    echo "Missing $SOURCE_APP. Run ./scripts/build-host-app.sh first." >&2
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

rm -rf "$TARGET_APP"
ditto --noextattr --noqtn "$SOURCE_APP" "$TARGET_APP"
clean_attrs "$TARGET_APP"

codesign --verify --deep --strict --verbose=2 "$TARGET_APP"
echo "Installed host app: $TARGET_APP"
