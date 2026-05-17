#!/bin/sh
set -eu

{
    dir="$HOME/Library/Developer/Xcode/UserData/Provisioning Profiles"
    [ -d "$dir" ] && find "$dir" -maxdepth 1 -type f \( -name '*.provisionprofile' -o -name '*.mobileprovision' \) -print
    dir="$HOME/Library/MobileDevice/Provisioning Profiles"
    [ -d "$dir" ] && find "$dir" -maxdepth 1 -type f \( -name '*.provisionprofile' -o -name '*.mobileprovision' \) -print
} | while IFS= read -r profile; do
    tmp="$(mktemp)"
    if security cms -D -i "$profile" > "$tmp" 2>/dev/null; then
        app_id="$(/usr/libexec/PlistBuddy -c 'Print :Entitlements:application-identifier' "$tmp" 2>/dev/null || true)"
        name="$(/usr/libexec/PlistBuddy -c 'Print :Name' "$tmp" 2>/dev/null || true)"
        uuid="$(/usr/libexec/PlistBuddy -c 'Print :UUID' "$tmp" 2>/dev/null || true)"
        echo "== $profile =="
        echo "Name: $name"
        echo "UUID: $uuid"
        echo "AppID: $app_id"
        echo "Entitlements:"
        /usr/libexec/PlistBuddy -c 'Print :Entitlements' "$tmp" 2>/dev/null | sed 's/^/  /'
        echo
    fi
    rm -f "$tmp"
done
