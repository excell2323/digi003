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
