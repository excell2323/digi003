# FireWireOHCIProbe

Experimental macOS DriverKit/AudioDriverKit driver work for the Digidesign/Avid Digi 003 on Apple Silicon through a Thunderbolt-to-FireWire OHCI controller.

The project started as a minimal OHCI-1394 PCI probe. It now brings up the Lucent/Agere OHCI controller, performs async FireWire transactions with the Digi 003, starts duplex isochronous streams, and exposes an experimental Core Audio input device. Audio capture works, but the live harvest is not stable yet: DBC/OHCI-cycle diagnostics show packet continuity loss that still needs to be fixed.

## Current Target

- PCI vendor: `0x11c1`
- PCI device: `0x5901`
- macOS provider: `IOPCIDevice`
- Required entitlement shown by I/O Registry: `com.apple.developer.driverkit.transport.pci`
- DriverKit SDK found locally: `DriverKit25.5.sdk`
- Current macOS DriverKit runtime: `25.4`, so the probe is built with deployment target `25.4`.
- The driver is built for `arm64`; macOS rejects development `arm64e` DriverKit binaries as preview ABI.
- Local active test version: `0.2.48/248`.

## What This Driver Currently Does

- Matches the `pci11c1,5901` PCI function.
- Opens an exclusive DriverKit PCI session.
- Reads PCI vendor/device/class/revision registers.
- Reads BAR0 metadata.
- Reads the OHCI version register at BAR0 offset `0x00`.
- Enables PCI `Memory Space` and `Bus Lead` command bits before MMIO reads.
- Publishes probe diagnostics as `Probe...` properties in IORegistry.
- Initializes enough OHCI state for bus reset, self-ID, async transactions, and isochronous DMA experiments.
- Reads Digi 003 config/register state through async transactions.
- Starts Digi 003 duplex isochronous TX/RX.
- Publishes an experimental 8-channel Core Audio input stream.
- Tracks RX CIP/DBC/SYT and OHCI-cycle continuity diagnostics.
- Closes the session on stop.

If this cannot be signed/loaded with the PCI DriverKit entitlement, a real FireWire stack cannot be delivered as a normal third-party DriverKit driver on this machine without Apple granting that entitlement.

## Build

First finish Xcode's first launch setup if `xcodebuild` reports missing components:

```sh
sudo xcodebuild -runFirstLaunch
```

Then try:

```sh
cd FireWireOHCIProbe
./scripts/build.sh
./scripts/sign-adhoc.sh
```

The build script intentionally does not install or load the driver. `sign-adhoc.sh` only proves that the bundle can be sealed locally; it is not a production or distribution signature. Loading a PCI DriverKit extension requires a host app/system extension flow and, in practice, Apple-granted signing for `com.apple.developer.driverkit.transport.pci`.

See `SIGNING.md` for the current signing blocker and the required Apple Developer setup.

## Host App Activation Test

Build the tiny host app:

```sh
./scripts/build-host-app.sh
./scripts/list-profiles.sh
./scripts/embed-profiles.sh
./scripts/install-host-app.sh
```

The DriverKit bundle installed under `Contents/Library/SystemExtensions` must keep the full bundle identifier as its filename:
`com.axelheckert.driver.FireWireOHCIProbe.dext`. Apple rejects system extensions whose filename does not match their bundle identifier.

Run it from Terminal so the activation result is visible:

```sh
/Applications/FireWireOHCIProbeLoader.app/Contents/MacOS/FireWireOHCIProbeLoader
```

If macOS asks for approval, approve the extension in System Settings, then check:

```sh
systemextensionsctl list
log show --last 10m --style compact --predicate 'eventMessage CONTAINS[c] "FireWireOHCIProbe" OR process == "sysextd"'
```

## Digi 003 Control Surface Bridge

The experimental CoreMIDI bridge exposes the internal Digi 003 port-E control
messages as a virtual MIDI source/destination named:

```text
Avid 003 Port 3 (Control)
```

Build and start it before launching V-Control Pro:

```sh
./scripts/build-tools.sh
./scripts/start-midi-bridge.sh
open -a "V-Control Pro"
open -a "Pro Tools"
```

Stop the background bridge with:

```sh
./scripts/stop-midi-bridge.sh
```

V-Control Pro's Digi 003 setup should use `Avid 003 Port 3 (Control)` for its
surface input/output. Pro Tools should use V-Control's own HUI ports instead:

```text
Setup > MIDI > MIDI Input Devices:
  enable V-Control
  leave V-Control Midi Mode disabled

Setup > Peripherals > MIDI Controllers:
  Type         HUI
  Receive From V-Control
  Send To      V-Control
  Ch's         8
```

The bridge currently forwards Digi 003 button/fader/encoder messages to
CoreMIDI. Reverse DAW feedback to LEDs, displays, and motor faders requires the
DEXT to be signed with `com.apple.developer.driverkit.allow-any-userclient-access`
or the bridge app/helper to be signed with `com.apple.developer.driverkit.userclient-access`.
By default, `scripts/start-midi-bridge.sh` still starts the bridge with
DriverKit feedback disabled while logging incoming DAW feedback to:

```text
~/Library/Logs/FireWireOHCIProbe/digi003-midi-feedback.log
```

To test DAW feedback after installing a DEXT with user-client access enabled:

```sh
FEEDBACK_TO_DRIVER=1 ./scripts/start-midi-bridge.sh
```

With feedback enabled, the bridge forwards standard 3-byte MIDI messages for
LED and fader state plus raw SysEx byte packets for Digi 003 display updates.

## Sample Rate

The current development build is pinned to a true 48 kHz hardware mode. The
driver writes Digi 003 local-rate index `1`, advertises only 48 kHz to CoreAudio,
and sends AM824 CIP packets with SFC `2`.

## Next Milestones

1. Stabilize live RX harvest so DBC/cycle lost counts approach zero.
2. Make the worker adaptive with low-water burst harvesting and larger descriptor drains.
3. Move TX scheduling toward Linux/ASFireWire-style dynamic packet scheduling.
4. Add MIDI/control-surface and mixer/control support.

See `NOTES.md` for current measurements and working hypotheses.

## Verified Local Status

As of the `0.0.4/4` build, the DriverKit system extension is signed, activated, and matched:

```text
7H3ND356AV com.axelheckert.driver.FireWireOHCIProbe (0.0.4/4) [activated enabled]
```

The probe opens the Lucent/Agere OHCI-1394 controller and reads BAR0 after enabling the PCI command bits:

```text
ProbeVendorID              = 0x11c1
ProbeDeviceID              = 0x5901
ProbeClassCode             = 0x0c0010
ProbeBAR0Raw               = 0x00100004
ProbeBAR0Size              = 0x1000
ProbeCommandBefore         = 0x0000
ProbeCommandAfter          = 0x0006
ProbeOHCIVersion           = 0x00010010
ProbeMemoryReadSucceeded   = 1
```

The important discovery was that BAR0 reads returned `0xffffffff` until the driver explicitly enabled `kIOPCICommandMemorySpace` and `kIOPCICommandBusLead` after `IOPCIDevice::Open()`.
