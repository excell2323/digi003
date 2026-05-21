# Public Beta Distribution Checklist

This file tracks the path from local developer beta to a public macOS beta that
other Digi 003 owners can install without being added as registered test
devices.

## Current Gate

The current build is a local development build. It is useful for source release
and close testing, but it is not a normal public binary distribution until
Apple grants distribution-capable DriverKit entitlements and the app/driver are
Developer-ID signed and notarized.

Requested in Apple Developer on 2026-05-21:

```text
com.axelheckert.driver.FireWireOHCIProbe:
- DriverKit Family Audio
- DriverKit PCI (PrimaryMatch)

com.axelheckert.Digi003MIDIBridge:
- DriverKit UserClient Access
  -> com.axelheckert.driver.FireWireOHCIProbe
```

Apple references:

- https://developer.apple.com/system-extensions/
- https://developer.apple.com/documentation/driverkit/requesting-entitlements-for-driverkit-development
- https://developer.apple.com/help/account/provisioning-profiles/create-a-driverkit-development-provisioning-profile/
- https://developer.apple.com/help/account/create-certificates/create-developer-id-certificates/
- https://developer.apple.com/documentation/security/notarizing-macos-software-before-distribution

## Bundle IDs

Keep these stable for the public beta:

```text
Host app:
com.axelheckert.FireWireOHCIProbeLoader

Driver extension:
com.axelheckert.driver.FireWireOHCIProbe

Future MIDI bridge app/helper:
com.axelheckert.Digi003MIDIBridge
```

The current command-line MIDI bridge works for local development. Before a
polished public beta, package it as a signed helper/app so its user-client
access entitlement is scoped and auditable.

Project scaffold now exists:

```text
BridgeApp/Info.plist
BridgeApp/Digi003MIDIBridge.entitlements
scripts/build-midi-bridge-app.sh
scripts/sign-midi-bridge-app.sh
scripts/install-midi-bridge-app.sh
```

## Entitlements To Request

Host app:

```text
com.apple.developer.system-extension.install
```

Driver extension:

```text
com.apple.developer.driverkit
com.apple.developer.driverkit.transport.pci
com.apple.developer.driverkit.family.audio
```

Preferred public user-client path:

```text
com.apple.developer.driverkit.userclient-access
```

The `userclient-access` entitlement belongs on the companion app/helper that
opens the DriverKit user client. Its value should list:

```text
com.axelheckert.driver.FireWireOHCIProbe
```

Development-only fallback currently used locally:

```text
com.apple.developer.driverkit.allow-any-userclient-access
```

For public distribution, prefer the scoped `userclient-access` approach if
Apple approves it.

## Apple Entitlement Request Draft

Use this as the request text in Apple Developer when requesting DriverKit /
System Extension entitlements:

```text
We are developing a macOS DriverKit replacement for the discontinued
Avid/Digidesign Digi 003 FireWire audio interface on Apple Silicon Macs.

The original Avid driver is an Intel-era kernel extension and does not provide
a usable Apple Silicon DriverKit/Core Audio path. The goal of this project is
to preserve working legacy audio hardware for musicians and small studios.

The driver is delivered as a macOS DriverKit system extension inside a host
application. It targets the FireWire OHCI controller reached through Apple's
Thunderbolt-to-FireWire adapter chain and implements Digi 003 audio streaming
with AudioDriverKit/Core Audio.

Observed hardware path:
- Device: Avid/Digidesign Digi 003 FireWire audio interface
- FireWire path: Apple Thunderbolt-to-FireWire adapter chain
- OHCI PCI vendor ID: 0x11c1
- OHCI PCI device ID: 0x5901
- macOS provider: IOPCIDevice

Requested entitlements:
- com.apple.developer.system-extension.install for the host application
- com.apple.developer.driverkit for the DriverKit extension
- com.apple.developer.driverkit.transport.pci for the OHCI PCI transport
- com.apple.developer.driverkit.family.audio for AudioDriverKit/Core Audio
- com.apple.developer.driverkit.userclient-access for the signed companion
  MIDI/HUI bridge app/helper that sends control-surface display, LED, and
  motor-fader feedback to the driver user client

The MIDI/HUI bridge will use user-client access only for our driver extension:
com.axelheckert.driver.FireWireOHCIProbe

The public beta will be Developer-ID signed, notarized, and distributed outside
the Mac App Store as a macOS installer package. Users will still explicitly
approve the system extension in macOS System Settings.
```

## Public Beta Work Plan

1. Request Apple distribution-capable DriverKit/System Extension entitlements.
2. After approval, confirm the entitlements appear on the Bundle IDs in Apple
   Developer.
3. Create or download Developer ID certificates:
   - Developer ID Application
   - Developer ID Installer
4. Create distribution provisioning profiles for:
   - host app
   - DriverKit extension
   - MIDI bridge helper/app, once bundled
5. Update signing scripts to support:
   - Apple Development local builds
   - Developer ID public builds
   - distribution provisioning profiles
6. Package the MIDI bridge as a signed helper/app instead of relying on a loose
   command-line binary.
7. Build a `.pkg` installer that installs:
   - `/Applications/FireWireOHCIProbeLoader.app`
   - the MIDI bridge helper/app or support files
   - launchd job for the MIDI bridge
8. Notarize the `.pkg` with `xcrun notarytool`.
9. Staple the notarization ticket with `xcrun stapler`.
10. Verify with Gatekeeper:

```sh
spctl -a -vvv -t install Digi003.pkg
pkgutil --check-signature Digi003.pkg
```

11. Publish a GitHub pre-release with:
   - notarized `.pkg`
   - source archive
   - release notes
   - tester checklist

## Tester-Facing Requirements

The public beta should clearly state:

```text
Requirements:
- Apple Silicon Mac
- macOS version matching the DriverKit deployment target
- Digi 003 connected through FireWire/Thunderbolt
- User approval of the System Extension in System Settings
- Reboot may be required after first install

Known beta limitations:
- Heavy system load may still cause audio clicks
- Sleep/wake stability needs more testing
- Control-surface mapping is still beta
- 44.1 kHz and 48 kHz are the currently supported sample rates
```
