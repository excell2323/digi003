#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
LABEL="${LABEL:-com.axelheckert.digi003-midi-bridge}"
BRIDGE="$PROJECT_DIR/Tools/bin/digi003-midi-bridge"
POLL_MS="${POLL_MS:-5}"
LAUNCH_AGENTS_DIR="$HOME/Library/LaunchAgents"
PLIST="$LAUNCH_AGENTS_DIR/$LABEL.plist"
LOG_DIR="$HOME/Library/Logs/FireWireOHCIProbe"

if [ ! -x "$BRIDGE" ]; then
    "$PROJECT_DIR/scripts/build-tools.sh"
fi

mkdir -p "$LAUNCH_AGENTS_DIR" "$LOG_DIR"

launchctl bootout "gui/$(id -u)" "$PLIST" 2>/dev/null || true
launchctl remove "$LABEL" 2>/dev/null || true

cat > "$PLIST" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>$BRIDGE</string>
    <string>--poll-ms</string>
    <string>$POLL_MS</string>
  </array>
  <key>RunAtLoad</key>
  <true/>
  <key>StandardOutPath</key>
  <string>$LOG_DIR/digi003-midi-bridge.log</string>
  <key>StandardErrorPath</key>
  <string>$LOG_DIR/digi003-midi-bridge.err</string>
</dict>
</plist>
EOF

launchctl bootstrap "gui/$(id -u)" "$PLIST"
launchctl kickstart -k "gui/$(id -u)/$LABEL"

echo "Started MIDI bridge launchd job: $LABEL"
echo "CoreMIDI port: Avid 003 Port 3 (Control)"
echo "Logs: $LOG_DIR/digi003-midi-bridge.log"
