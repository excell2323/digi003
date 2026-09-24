#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
TEAM_ID="7H3ND356AV"
HOST_APP_ID="$TEAM_ID.com.axelheckert.FireWireOHCIProbeLoader"
DEXT_BUNDLE_ID="com.axelheckert.driver.FireWireOHCIProbe"
DEXT_APP_ID="$TEAM_ID.$DEXT_BUNDLE_ID"
BRIDGE_APP_ID="$TEAM_ID.com.axelheckert.Digi003MIDIBridge"
APP="$BUILD_DIR/FireWireOHCIProbeLoader.app"
DEXT="$APP/Contents/Library/SystemExtensions/$DEXT_BUNDLE_ID.dext"
BRIDGE_APP="$BUILD_DIR/Digi003MIDIBridge.app"
IDENTITY="${1:-${SIGN_IDENTITY:-Developer ID Application: Axel Heckert (7H3ND356AV)}}"
PROFILE_SEARCH_DIRS="${PROFILE_SEARCH_DIRS:-$HOME/Downloads:$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles:$HOME/Library/MobileDevice/Provisioning Profiles}"

if [ ! -d "$APP" ]; then
    echo "Missing $APP. Run ./scripts/build-host-app.sh first." >&2
    exit 1
fi

if [ ! -d "$BRIDGE_APP" ]; then
    echo "Missing $BRIDGE_APP. Run ./scripts/build-midi-bridge-app.sh first." >&2
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

decode_profile() {
    profile="$1"
    output="$2"
    security cms -D -i "$profile" > "$output" 2>/dev/null ||
        openssl smime -verify -inform der -noverify -in "$profile" -out "$output" >/dev/null 2>/dev/null
}

profile_app_id() {
    plist="$1"
    app_id="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$plist" 2>/dev/null || true)"
    if [ -z "$app_id" ]; then
        app_id="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.application-identifier' "$plist" 2>/dev/null || true)"
    fi
    echo "$app_id"
}

profile_is_developer_id() {
    plist="$1"
    provisions_all_devices="$(/usr/libexec/PlistBuddy -c 'Print :ProvisionsAllDevices' "$plist" 2>/dev/null || true)"
    if [ "$provisions_all_devices" != "true" ]; then
        return 1
    fi
    if /usr/libexec/PlistBuddy -c 'Print :ProvisionedDevices' "$plist" >/dev/null 2>&1; then
        return 1
    fi
    return 0
}

find_profile() {
    target_app_id="$1"
    shift

    old_ifs="$IFS"
    IFS=:
    for dir in $PROFILE_SEARCH_DIRS; do
        IFS="$old_ifs"
        [ -d "$dir" ] || {
            IFS=:
            continue
        }
        find "$dir" -maxdepth 1 -type f \( -name '*.provisionprofile' -o -name '*.mobileprovision' \) | while IFS= read -r profile; do
            tmp="$(mktemp)"
            if decode_profile "$profile" "$tmp"; then
                app_id="$(profile_app_id "$tmp")"
                missing=0
                for required_entitlement in "$@"; do
                    entitlement_value="$(/usr/libexec/PlistBuddy -c "Print :Entitlements:$required_entitlement" "$tmp" 2>/dev/null || true)"
                    if [ -z "$entitlement_value" ]; then
                        missing=1
                        break
                    fi
                done
                if [ "$app_id" = "$target_app_id" ] &&
                    [ "$missing" -eq 0 ] &&
                    profile_is_developer_id "$tmp"; then
                    rm -f "$tmp"
                    echo "$profile"
                    exit 0
                fi
            fi
            rm -f "$tmp"
        done
        IFS=:
    done
    IFS="$old_ifs"
}

host_profile="${HOST_PROFILE:-$(find_profile "$HOST_APP_ID" "com.apple.developer.system-extension.install" | head -n 1 || true)}"
dext_profile="${DEXT_PROFILE:-$(find_profile "$DEXT_APP_ID" "com.apple.developer.driverkit" "com.apple.developer.driverkit.family.audio" "com.apple.developer.driverkit.transport.pci" | head -n 1 || true)}"
bridge_profile="${BRIDGE_PROFILE:-$(find_profile "$BRIDGE_APP_ID" "com.apple.developer.driverkit.userclient-access" | head -n 1 || true)}"

if [ -z "$host_profile" ]; then
    echo "Missing Developer ID provisioning profile for $HOST_APP_ID with system-extension.install" >&2
    exit 1
fi

if [ -z "$dext_profile" ]; then
    echo "Missing Developer ID provisioning profile for $DEXT_APP_ID with DriverKit, Audio, and PCI entitlements" >&2
    exit 1
fi

if [ -z "$bridge_profile" ]; then
    echo "Missing Developer ID provisioning profile for $BRIDGE_APP_ID with driverkit.userclient-access" >&2
    exit 1
fi

cp "$host_profile" "$APP/Contents/embedded.provisionprofile"
cp "$dext_profile" "$DEXT/embedded.provisionprofile"
cp "$bridge_profile" "$BRIDGE_APP/Contents/embedded.provisionprofile"

echo "Embedded host profile: $host_profile"
echo "Embedded dext profile: $dext_profile"
echo "Embedded bridge profile: $bridge_profile"

clean_attrs "$APP"
rm -rf "$DEXT/_CodeSignature"

codesign --force --timestamp --options runtime --sign "$IDENTITY" \
    --entitlements "$PROJECT_DIR/Resources/FireWireOHCIProbe.developer-id.entitlements" \
    "$DEXT"

clean_attrs "$APP"
rm -rf "$APP/Contents/_CodeSignature"

codesign --force --timestamp --options runtime --sign "$IDENTITY" \
    --entitlements "$PROJECT_DIR/HostApp/FireWireOHCIProbeLoader.developer-id.entitlements" \
    "$APP"

clean_attrs "$BRIDGE_APP"
rm -rf "$BRIDGE_APP/Contents/_CodeSignature"

codesign --force --timestamp --options runtime --sign "$IDENTITY" \
    --entitlements "$PROJECT_DIR/BridgeApp/Digi003MIDIBridge.entitlements" \
    "$BRIDGE_APP"

clean_attrs "$APP"
clean_attrs "$BRIDGE_APP"

codesign --verify --deep --strict --verbose=2 "$APP"
codesign --verify --deep --strict --verbose=2 "$BRIDGE_APP"
codesign --display --entitlements :- "$APP"
codesign --display --entitlements :- "$DEXT"
codesign --display --entitlements :- "$BRIDGE_APP"
