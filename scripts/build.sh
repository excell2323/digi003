#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
DEXT_BUNDLE_ID="com.axelheckert.driver.FireWireOHCIProbe"
DEXT_NAME="$DEXT_BUNDLE_ID.dext"
SDK="$(xcrun --sdk driverkit --show-sdk-path)"
DEPLOYMENT_TARGET="25.4"
ARCH="${ARCH:-arm64}"
CLANG="$(xcrun --sdk driverkit --find clang)"
IIG="$(xcrun --sdk driverkit --find iig)"
SRC_DIR="$PROJECT_DIR/Source"

rm -rf "$BUILD_DIR"
mkdir -p "$BUILD_DIR/DerivedSources" "$BUILD_DIR/Objects" "$BUILD_DIR/$DEXT_NAME"

"$IIG" \
    --def "$SRC_DIR/FireWireOHCIProbe.iig" \
    --header "$BUILD_DIR/DerivedSources/FireWireOHCIProbe.h" \
    --impl "$BUILD_DIR/DerivedSources/FireWireOHCIProbe.iig.cpp" \
    --framework-name FireWireOHCIProbe \
    --deployment-target "$DEPLOYMENT_TARGET" \
    -- \
    -isysroot "$SDK" \
    -D__IIG=1 \
    -x c++ \
    -std=c++17 \
    -I"$BUILD_DIR/DerivedSources" \
    -F"$SDK/System/DriverKit/System/Library/Frameworks"

mkdir -p "$BUILD_DIR/DerivedSources/FireWireOHCIProbe"
cp "$BUILD_DIR/DerivedSources/FireWireOHCIProbe.h" "$BUILD_DIR/DerivedSources/FireWireOHCIProbe/FireWireOHCIProbe.h"

"$CLANG"++ \
    -target "$ARCH"-apple-driverkit"$DEPLOYMENT_TARGET" \
    -isysroot "$SDK" \
    -std=c++17 \
    -fblocks \
    -fvisibility=hidden \
    -I"$BUILD_DIR/DerivedSources" \
    -F"$SDK/System/DriverKit/System/Library/Frameworks" \
    -iframework "$SDK/System/DriverKit/System/Library/Frameworks" \
    -c "$SRC_DIR/FireWireOHCIProbe.cpp" \
    -o "$BUILD_DIR/Objects/FireWireOHCIProbe.o"

"$CLANG"++ \
    -target "$ARCH"-apple-driverkit"$DEPLOYMENT_TARGET" \
    -isysroot "$SDK" \
    -std=c++17 \
    -fblocks \
    -fvisibility=hidden \
    -I"$BUILD_DIR/DerivedSources" \
    -F"$SDK/System/DriverKit/System/Library/Frameworks" \
    -iframework "$SDK/System/DriverKit/System/Library/Frameworks" \
    -c "$BUILD_DIR/DerivedSources/FireWireOHCIProbe.iig.cpp" \
    -o "$BUILD_DIR/Objects/FireWireOHCIProbe.iig.o"

"$CLANG"++ \
    -target "$ARCH"-apple-driverkit"$DEPLOYMENT_TARGET" \
    -isysroot "$SDK" \
    -fuse-ld=ld \
    -fblocks \
    -framework AudioDriverKit \
    -framework DriverKit \
    -framework PCIDriverKit \
    "$BUILD_DIR/Objects/FireWireOHCIProbe.o" \
    "$BUILD_DIR/Objects/FireWireOHCIProbe.iig.o" \
    -o "$BUILD_DIR/$DEXT_NAME/FireWireOHCIProbe"

cp "$PROJECT_DIR/Resources/Info.plist" "$BUILD_DIR/$DEXT_NAME/Info.plist"

xattr -cr "$BUILD_DIR/$DEXT_NAME" 2>/dev/null || true

echo "Built: $BUILD_DIR/$DEXT_NAME"
