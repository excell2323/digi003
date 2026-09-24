#!/bin/sh
set -eu

{
    dir="$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
    [ -d "$dir" ] && find "$dir" -maxdepth 1 -type f \( -name '*.provisionprofile' -o -name '*.mobileprovision' \) -print
    dir="$HOME/Library/MobileDevice/Provisioning Profiles"
    [ -d "$dir" ] && find "$dir" -maxdepth 1 -type f \( -name '*.provisionprofile' -o -name '*.mobileprovision' \) -print
} | while IFS= read -r profile; do
    tmp="$(mktemp)"
    if security cms -D -i "$profile" > "$tmp" 2>/dev/null ||
        openssl smime -verify -inform der -noverify -in "$profile" -out "$tmp" >/dev/null 2>/dev/null; then
        app_id="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$tmp" 2>/dev/null || true)"
        if [ -z "$app_id" ]; then
            app_id="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:com.apple.application-identifier' "$tmp" 2>/dev/null || true)"
        fi
        name="$(/usr/libexec/PlistBuddy -c 'Print :Name' "$tmp" 2>/dev/null || true)"
        uuid="$(/usr/libexec/PlistBuddy -c 'Print :UUID' "$tmp" 2>/dev/null || true)"
        provisions_all_devices="$(/usr/libexec/PlistBuddy -c 'Print :ProvisionsAllDevices' "$tmp" 2>/dev/null || true)"
        provisioned_devices="$(/usr/libexec/PlistBuddy -c 'Print :ProvisionedDevices' "$tmp" >/dev/null 2>&1 && echo yes || true)"
        echo "== $profile =="
        echo "Name: $name"
        echo "UUID: $uuid"
        echo "AppID: $app_id"
        [ -n "$provisions_all_devices" ] && echo "ProvisionsAllDevices: $provisions_all_devices"
        [ -n "$provisioned_devices" ] && echo "ProvisionedDevices: yes"
        echo "Entitlements:"
        /usr/libexec/PlistBuddy -c 'Print :Entitlements' "$tmp" 2>/dev/null | sed 's/^/  /'
        echo
    fi
    rm -f "$tmp"
done
