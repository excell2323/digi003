# FireWireOHCIProbe

Experimental macOS DriverKit/AudioDriverKit driver work for the Digidesign/Avid
Digi 003 on Apple Silicon through a Thunderbolt-to-FireWire OHCI controller.

## Status

Current developer beta:

```text
Digi 003 FireWire Driver 0.2.194 beta 1
Driver version: 0.2.194/394
Known working local commit: 9e5c148
```

This is the first build that is useful as a community test milestone:

- Core Audio input and output expose 8 channels.
- 44.1 kHz and 48 kHz are selectable from Core Audio/Pro Tools.
- The driver switches Digi 003 local-rate index, AM824 CIP SFC/FDF, and packet
  cadence with the selected sample rate.
- 44.1 kHz and 48 kHz have both been locally verified by ear.
- Pro Tools can use the Digi 003 as an audio device.
- The CoreMIDI bridge exposes the control-surface port for V-Control/Pro Tools
  HUI workflows.
- Motor fader, LED, and display feedback work in the local development setup.

This is still a developer beta, not a ready-to-install public release. Normal
distribution is blocked until Apple grants distribution-capable DriverKit
entitlements and the app/driver are signed with Developer ID and notarized.

## Distribution Notice

Other users should not expect to install the current build by downloading a
zip. A DriverKit system extension must be shipped inside a macOS app bundle,
installed from `/Applications`, signed with valid Apple signing material, and
activated through the SystemExtensions framework.

The provisioning profiles are not installed separately by the user. They must be
embedded into the signed app and driver extension and must match the signing
team and entitlements. A development provisioning profile is useful for local
testing, but it does not make a public build installable on arbitrary Macs.

For a public installer we still need:

- Apple-approved DriverKit entitlements for distribution.
- Developer ID signing for the host app and driver extension.
- Matching embedded provisioning profiles for both bundles.
- Apple notarization.
- A package that installs the host app and MIDI bridge launch agent.

See `SIGNING.md` for the current signing requirements and blockers.

## Hardware Target

- Device: Digidesign/Avid Digi 003
- FireWire path: Apple Thunderbolt-to-FireWire adapter chain
- PCI vendor: `0x11c1`
- PCI device: `0x5901`
- macOS provider: `IOPCIDevice`
- Required transport entitlement: `com.apple.developer.driverkit.transport.pci`
- DriverKit deployment target: `25.4`
- Architecture: `arm64`

## Current Driver Features

- Matches the `pci11c1,5901` PCI function.
- Opens an exclusive DriverKit PCI session.
- Enables the OHCI MMIO path and reads controller diagnostics.
- Initializes enough OHCI state for bus reset, self-ID, async transactions, and
  isochronous DMA.
- Performs Digi 003 async register/config transactions.
- Starts duplex isochronous transmit and receive streams.
- Publishes an 8-channel Core Audio input stream.
- Publishes an 8-channel Core Audio output stream.
- Supports 44.1 kHz and 48 kHz hardware modes.
- Tracks RX CIP/DBC/SYT, OHCI-cycle, ring-buffer, and output-push diagnostics
  through IORegistry properties.
- Provides a DriverKit user client for control-surface feedback from the MIDI
  bridge in local development builds.

## Known Limitations

- This is a beta driver and may still click under heavy system load.
- Sleep/wake and overnight stability still need more testing.
- Lower Pro Tools hardware buffer sizes may be sensitive to CPU spikes; 64
  samples has worked locally, and larger buffers should be tested when offered.
- Control-surface mapping is mostly usable but not final.
- The current local build uses broad development user-client access so the MIDI
  bridge can feed display, LED, and motor-fader feedback into the driver.
- A public installer cannot be produced until Apple distribution entitlements
  and notarization are in place.

## Build

First finish Xcode's first launch setup if needed:

```sh
sudo xcodebuild -runFirstLaunch
```

Build the driver, host app, and MIDI bridge:

```sh
./scripts/build-tools.sh
./scripts/build-host-app.sh
```

Ad-hoc signing only proves that the bundle can be sealed locally. It is not
enough to activate this DriverKit system extension:

```sh
./scripts/sign-adhoc.sh
```

For local development on a machine with matching Apple Development certificates
and provisioning profiles:

```sh
SIGN_IDENTITY='Apple Development: Your Name (TEAMID)' ./scripts/sign-with-identity.sh
./scripts/install-host-app.sh
/Applications/FireWireOHCIProbeLoader.app/Contents/MacOS/FireWireOHCIProbeLoader
```

If macOS asks for approval, approve the extension in System Settings, then
check:

```sh
systemextensionsctl list
ioreg -l -r -c FireWireOHCIProbe
```

## CoreMIDI Bridge

The experimental CoreMIDI bridge exposes the internal Digi 003 control-surface
port as:

```text
Avid 003 Port 3 (Control)
```

Start it before launching V-Control Pro and Pro Tools:

```sh
./scripts/start-midi-bridge.sh
open -a "V-Control Pro"
open -a "Pro Tools"
```

Stop it with:

```sh
./scripts/stop-midi-bridge.sh
```

For the current V-Control/HUI setup:

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

See `CONTROL_SURFACE.md` for the current physical control map.

## Sample Rate Test

The current driver advertises exactly these nominal sample rates:

```text
44100 Hz
48000 Hz
```

Expected IORegistry values:

```text
44.1 kHz:
  ProbeAudioRuntimeSampleRate        = 44100
  ProbeAudioRuntimeDigiLocalRateIndex = 0
  ProbeAudioRuntimeCIPSFC             = 1
  ProbeDigiLiveRxFDF                  = 1

48 kHz:
  ProbeAudioRuntimeSampleRate        = 48000
  ProbeAudioRuntimeDigiLocalRateIndex = 1
  ProbeAudioRuntimeCIPSFC             = 2
  ProbeDigiLiveRxFDF                  = 2
```

## Test Matrix For Beta Builds

Before calling a build good, test:

- 44.1 kHz playback for at least 15 minutes.
- 48 kHz playback for at least 15 minutes.
- Input monitoring/recording on channel 1.
- Output playback through Pro Tools and a local audio file.
- Pro Tools hardware buffer sizes, especially 64 and 128 samples.
- Fader moves from Digi 003 to Pro Tools.
- Fader feedback from Pro Tools to Digi 003.
- Bank/Nudge mode buttons and LEDs.
- Display track names and volume values.
- Sleep/wake or a full reboot followed by driver reload.

## Development Notes

See `NOTES.md` for historical measurements, transport experiments, and working
hypotheses. Some older sections describe pre-audio-output builds and are kept
as lab notes rather than current status.
