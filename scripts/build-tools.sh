#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
TOOLS_DIR="$PROJECT_DIR/Tools"
BIN_DIR="$TOOLS_DIR/bin"
MAC_SDK="$(xcrun --sdk macosx --show-sdk-path)"
CLANG="$(xcrun --sdk macosx --find clang)"

mkdir -p "$BIN_DIR"

"$CLANG" \
    -isysroot "$MAC_SDK" \
    -target arm64-apple-macos15.0 \
    -Wall \
    -Wextra \
    -O2 \
    "$TOOLS_DIR/record-digi-input.c" \
    -framework AudioToolbox \
    -framework CoreAudio \
    -framework CoreFoundation \
    -o "$BIN_DIR/record-digi-input"

"$CLANG" \
    -isysroot "$MAC_SDK" \
    -target arm64-apple-macos15.0 \
    -Wall \
    -Wextra \
    -O2 \
    "$TOOLS_DIR/play-digi-output.c" \
    -framework AudioToolbox \
    -framework CoreAudio \
    -framework CoreFoundation \
    -o "$BIN_DIR/play-digi-output"

"$CLANG" \
    -isysroot "$MAC_SDK" \
    -target arm64-apple-macos15.0 \
    -Wall \
    -Wextra \
    -O2 \
    "$TOOLS_DIR/digi-debug-send.c" \
    -framework CoreFoundation \
    -framework IOKit \
    -o "$BIN_DIR/digi-debug-send"

"$CLANG" \
    -isysroot "$MAC_SDK" \
    -target arm64-apple-macos15.0 \
    -Wall \
    -Wextra \
    -O2 \
    "$TOOLS_DIR/digi003-midi-bridge.c" \
    -framework CoreMIDI \
    -framework CoreFoundation \
    -framework IOKit \
    -o "$BIN_DIR/digi003-midi-bridge"

echo "Built tools in $BIN_DIR"
