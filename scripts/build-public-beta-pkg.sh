#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
DIST_DIR="${DIST_DIR:-$PROJECT_DIR/Packages/PublicBeta}"
APP="$BUILD_DIR/FireWireOHCIProbeLoader.app"
BRIDGE_APP="$BUILD_DIR/Digi003MIDIBridge.app"
VERSION="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleShortVersionString' "$PROJECT_DIR/HostApp/Info.plist")"
BUILD_NUMBER="$(/usr/libexec/PlistBuddy -c 'Print :CFBundleVersion' "$PROJECT_DIR/HostApp/Info.plist")"
PKG_VERSION="${PKG_VERSION:-$VERSION.$BUILD_NUMBER}"
PKG_ID="${PKG_ID:-com.axelheckert.digi003}"
PKG_NAME="${PKG_NAME:-Digi003-FireWire-$PKG_VERSION-beta.pkg}"
INSTALLER_IDENTITY="${INSTALLER_IDENTITY:-Developer ID Installer: Axel Heckert (7H3ND356AV)}"
LABEL="com.axelheckert.digi003-midi-bridge"

"$PROJECT_DIR/scripts/build-host-app.sh"
"$PROJECT_DIR/scripts/build-midi-bridge-app.sh"
"$PROJECT_DIR/scripts/sign-developer-id.sh"

PKG_WORK="$BUILD_DIR/PublicBetaPkg"
PKGROOT="$PKG_WORK/root"
PKGSCRIPTS="$PKG_WORK/scripts"
SUPPORT_DIR="$PKGROOT/Library/Application Support/Digi003"
LAUNCH_AGENTS_DIR="$PKGROOT/Library/LaunchAgents"

rm -rf "$PKG_WORK"
mkdir -p "$PKGROOT/Applications" "$SUPPORT_DIR" "$LAUNCH_AGENTS_DIR" "$PKGSCRIPTS" "$DIST_DIR"

ditto --noextattr --noqtn "$APP" "$PKGROOT/Applications/FireWireOHCIProbeLoader.app"
ditto --noextattr --noqtn "$BRIDGE_APP" "$PKGROOT/Applications/Digi003MIDIBridge.app"
cp "$PROJECT_DIR/scripts/digi003-midi-bridge-agent.sh" "$SUPPORT_DIR/digi003-midi-bridge-agent.sh"
chmod 755 "$SUPPORT_DIR/digi003-midi-bridge-agent.sh"

cat > "$LAUNCH_AGENTS_DIR/$LABEL.plist" <<EOF
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN"
 "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key>
  <string>$LABEL</string>
  <key>ProgramArguments</key>
  <array>
    <string>/Library/Application Support/Digi003/digi003-midi-bridge-agent.sh</string>
  </array>
  <key>EnvironmentVariables</key>
  <dict>
    <key>POLL_MS</key>
    <string>5</string>
    <key>FEEDBACK_TO_DRIVER</key>
    <string>1</string>
    <key>WAIT_FOR_VCONTROL</key>
    <string>0</string>
  </dict>
  <key>RunAtLoad</key>
  <true/>
  <key>KeepAlive</key>
  <true/>
  <key>StandardOutPath</key>
  <string>/tmp/digi003-midi-bridge.log</string>
  <key>StandardErrorPath</key>
  <string>/tmp/digi003-midi-bridge.err</string>
</dict>
</plist>
EOF

cat > "$PKGSCRIPTS/postinstall" <<'EOF'
#!/bin/sh
set -eu

label="com.axelheckert.digi003-midi-bridge"
plist="/Library/LaunchAgents/$label.plist"
console_user="$(stat -f %Su /dev/console 2>/dev/null || true)"

if [ -n "$console_user" ] && [ "$console_user" != "root" ] && [ "$console_user" != "loginwindow" ]; then
    uid="$(id -u "$console_user" 2>/dev/null || true)"
    if [ -n "$uid" ]; then
        launchctl bootout "gui/$uid" "$plist" 2>/dev/null || true
        launchctl bootstrap "gui/$uid" "$plist" 2>/dev/null || true
        launchctl kickstart -k "gui/$uid/$label" 2>/dev/null || true
        launchctl asuser "$uid" /usr/bin/open -g "/Applications/FireWireOHCIProbeLoader.app" 2>/dev/null || true
    fi
fi

exit 0
EOF
chmod 755 "$PKGSCRIPTS/postinstall"

PKG_PATH="$DIST_DIR/$PKG_NAME"

pkgbuild \
    --root "$PKGROOT" \
    --scripts "$PKGSCRIPTS" \
    --identifier "$PKG_ID" \
    --version "$PKG_VERSION" \
    --install-location / \
    --sign "$INSTALLER_IDENTITY" \
    --timestamp \
    "$PKG_PATH"

pkgutil --check-signature "$PKG_PATH"
spctl -a -vvv -t install "$PKG_PATH" || true

echo "Built public beta package: $PKG_PATH"
echo "Next notarization step:"
echo "  xcrun notarytool submit \"$PKG_PATH\" --keychain-profile <profile> --wait"
echo "  xcrun stapler staple \"$PKG_PATH\""
