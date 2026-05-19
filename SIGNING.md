# Signing Requirements

Ad-hoc signing is not enough for this project. macOS refuses to activate a DriverKit system extension with restricted entitlements when it is ad-hoc signed:

```text
Adhoc signed app with restricted entitlements detected
The file is adhoc signed but contains restricted entitlements
```

That is expected for System Extensions and DriverKit PCI transport. The host app and the embedded dext need real Apple signing material, and the entitlements must be allowed by the certificate/provisioning profile.

## Required Entitlements

Host app:

```xml
com.apple.developer.system-extension.install
```

Driver extension:

```xml
com.apple.developer.driverkit
com.apple.developer.driverkit.transport.pci
```

The PCI entitlement is the difficult one. The I/O Registry for `pci11c1,5901` explicitly reports:

```text
IOServiceDEXTEntitlements = (("com.apple.developer.driverkit.transport.pci"))
```

## Local Status Checked

Initial status:

```text
security find-identity -v -p codesigning
0 valid identities found

profiles list -type provisioning
No provisioning profiles appear to be installed.
```

Status after installing the Apple WWDR G3 intermediate certificate:

```text
security find-identity -v -p codesigning
1 valid identities found
Apple Development: Your Name (TEAMID)
```

The official Apple WWDR G3 intermediate was installed from:

```text
https://www.apple.com/certificateauthority/AppleWWDRCAG3.cer
```

## Working Local Setup

The host app and dext are now signed with Team ID `7H3ND356AV` and matching development provisioning profiles:

- Host app bundle ID: `com.axelheckert.FireWireOHCIProbeLoader`
- Driver extension bundle ID: `com.axelheckert.driver.FireWireOHCIProbe`
- Host profile includes `com.apple.developer.system-extension.install`
- DEXT profile includes `com.apple.developer.driverkit.transport.pci`

Rebuild:

```sh
cd FireWireOHCIProbe
./scripts/build-host-app.sh
```

Sign with the real identity:

```sh
security find-identity -v -p codesigning
SIGN_IDENTITY='Apple Development: Your Name (TEAMID)' ./scripts/sign-with-identity.sh
./scripts/install-host-app.sh
/Applications/FireWireOHCIProbeLoader.app/Contents/MacOS/FireWireOHCIProbeLoader
```

If activation gets stuck on an old version during replacement, unplug and replug the Thunderbolt-to-FireWire adapter so IOKit drops the old `IOPCIDevice` match and starts the new DEXT instance.

## Control Feedback UserClient Access

The CoreMIDI control bridge can receive feedback from V-Control/Pro Tools, but
an unsigned command-line bridge cannot open the DriverKit debug user client:

```text
IOServiceOpen(...) -> 0xe00002e2 / kIOReturnNotPermitted
```

For motor fader, LED, and display feedback, the process that opens the driver
must be signed with a provisioning profile that includes:

```xml
com.apple.developer.driverkit.userclient-access
```

The value should list the DEXT bundle ID:

```xml
<key>com.apple.developer.driverkit.userclient-access</key>
<array>
    <string>com.axelheckert.driver.FireWireOHCIProbe</string>
</array>
```

This belongs on the client app/helper that opens the `IOUserClient`, most
likely `com.axelheckert.FireWireOHCIProbeLoader` or a dedicated MIDI bridge
helper inside that app bundle. Adding the key locally is not sufficient; the
Apple provisioning profile must contain it too.

Development alternative: the DEXT can be granted
`com.apple.developer.driverkit.allow-any-userclient-access`, which lets any
app connect to the driver user client. That is useful for quick local testing,
but it is broader than the app-scoped `userclient-access` path and also
requires a matching DEXT provisioning profile.
