#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
APP="$BUILD_DIR/Digi003MIDIBridge.app"
BRIDGE_APP_ID="7H3ND356AV.com.axelheckert.Digi003MIDIBridge"
IDENTITY="${1:-${SIGN_IDENTITY:-}}"

if [ -z "$IDENTITY" ]; then
    echo "Usage: SIGN_IDENTITY='Apple Development: Name (TEAMID)' ./scripts/sign-midi-bridge-app.sh" >&2
    echo "" >&2
    security find-identity -v -p codesigning >&2 || true
    exit 2
fi

if [ ! -d "$APP" ]; then
    echo "Missing $APP. Run ./scripts/build-midi-bridge-app.sh first." >&2
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

find_profile() {
    target_app_id="$1"
    shift
    {
        [ -d "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles" ] &&
            find "$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles" -maxdepth 1 -type f \( -name '*.provisionprofile' -o -name '*.mobileprovision' \)
        [ -d "$HOME/Library/MobileDevice/Provisioning Profiles" ] &&
            find "$HOME/Library/MobileDevice/Provisioning Profiles" -maxdepth 1 -type f \( -name '*.provisionprofile' -o -name '*.mobileprovision' \)
    } 2>/dev/null | while IFS= read -r profile; do
        tmp="$(mktemp)"
        if security cms -D -i "$profile" > "$tmp" 2>/dev/null; then
            app_id="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$tmp" 2>/dev/null || true)"
            if [ -z "$app_id" ]; then
                app_id="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.application-identifier' "$tmp" 2>/dev/null || true)"
            fi
            missing=0
            for required_entitlement in "$@"; do
                entitlement_value="$(/usr/libexec/PlistBuddy -c "Print :Entitlements:$required_entitlement" "$tmp" 2>/dev/null || true)"
                if [ -z "$entitlement_value" ]; then
                    missing=1
                    break
                fi
            done
            rm -f "$tmp"
            if [ "$app_id" = "$target_app_id" ] && [ "$missing" -eq 0 ]; then
                echo "$profile"
                exit 0
            fi
        else
            rm -f "$tmp"
        fi
    done
}

bridge_profile="$(
    find_profile \
        "$BRIDGE_APP_ID" \
        "com.apple.developer.driverkit.userclient-access" |
    head -n 1 || true
)"

if [ -z "$bridge_profile" ]; then
    echo "Missing provisioning profile for $BRIDGE_APP_ID with com.apple.developer.driverkit.userclient-access" >&2
    echo "This is expected until Apple approves the MIDI bridge UserClient Access request." >&2
    exit 1
fi

cp "$bridge_profile" "$APP/Contents/embedded.provisionprofile"

clean_attrs "$APP"
rm -rf "$APP/Contents/_CodeSignature"

codesign --force --sign "$IDENTITY" \
    --entitlements "$PROJECT_DIR/BridgeApp/Digi003MIDIBridge.entitlements" \
    "$APP"

clean_attrs "$APP"

codesign --verify --deep --strict --verbose=2 "$APP"
codesign --display --entitlements :- "$APP"
