#!/bin/sh
set -eu

LABEL="${LABEL:-com.axelheckert.digi003-midi-bridge}"
PLIST="$HOME/Library/LaunchAgents/$LABEL.plist"

launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
launchctl remove "$LABEL" 2>/dev/null || true
pkill -f '/digi003-midi-bridge' 2>/dev/null || true

echo "Stopped MIDI bridge launchd job: $LABEL"
