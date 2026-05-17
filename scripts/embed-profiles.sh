#!/bin/sh
set -eu

PROJECT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-/tmp/FireWireOHCIProbe-build}"
APP="$BUILD_DIR/FireWireOHCIProbeLoader.app"
HOST_APP_ID="7H3ND356AV.com.axelheckert.FireWireOHCIProbeLoader"
DEXT_BUNDLE_ID="com.axelheckert.driver.FireWireOHCIProbe"
DEXT_APP_ID="7H3ND356AV.$DEXT_BUNDLE_ID"
DEXT="$APP/Contents/Library/SystemExtensions/$DEXT_BUNDLE_ID.dext"

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

[ -d "$APP" ] || {
    echo "Missing $APP. Run ./scripts/build-host-app.sh first." >&2
    exit 1
}

host_profile="$(find_profile "$HOST_APP_ID" "com.apple.developer.system-extension.install" | head -n 1 || true)"
dext_profile="$(
    find_profile \
        "$DEXT_APP_ID" \
        "com.apple.developer.driverkit.transport.pci" \
        "com.apple.developer.driverkit.family.audio" |
    head -n 1 || true
)"

if [ -z "$host_profile" ]; then
    echo "Missing provisioning profile for $HOST_APP_ID with com.apple.developer.system-extension.install" >&2
    exit 1
fi

if [ -z "$dext_profile" ]; then
    echo "Missing provisioning profile for $DEXT_APP_ID with com.apple.developer.driverkit.transport.pci and com.apple.developer.driverkit.family.audio" >&2
    exit 1
fi

cp "$host_profile" "$APP/Contents/embedded.provisionprofile"
cp "$dext_profile" "$DEXT/embedded.provisionprofile"

echo "Embedded host profile: $host_profile"
echo "Embedded dext profile: $dext_profile"
