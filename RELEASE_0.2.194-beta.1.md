# Digi 003 FireWire Driver 0.2.194 Beta 1

This is the first developer-beta milestone for the Apple Silicon Digi 003
FireWire DriverKit project.

## What Works

- 8-channel Core Audio input.
- 8-channel Core Audio output.
- Selectable 44.1 kHz and 48 kHz sample rates.
- Digi 003 local-rate index follows the selected sample rate.
- AM824 CIP SFC/FDF and packet cadence follow the selected sample rate.
- Pro Tools can use the Digi 003 as an audio device in local testing.
- CoreMIDI bridge exposes the Digi 003 control-surface port.
- V-Control/Pro Tools HUI workflow works in the local development setup.
- Motor fader, LED, and display feedback are working in local testing.

## Verified Locally

44.1 kHz:

```text
ProbeAudioRuntimeSampleRate         = 44100
ProbeAudioRuntimeDigiLocalRateIndex = 0
ProbeAudioRuntimeCIPSFC             = 1
ProbeDigiLiveRxFDF                  = 1
```

48 kHz:

```text
ProbeAudioRuntimeSampleRate         = 48000
ProbeAudioRuntimeDigiLocalRateIndex = 1
ProbeAudioRuntimeCIPSFC             = 2
ProbeDigiLiveRxFDF                  = 2
```

Both rates were verified by ear during playback.

## Known Limitations

- This is a developer beta, not an end-user installer.
- Public installation is blocked until Apple grants distribution-capable
  DriverKit entitlements and the app/driver are Developer-ID signed and
  notarized.
- Heavy system load may still cause audio clicks.
- Sleep/wake and overnight stability need more testing.
- Control-surface mapping and MIDI-mode support are not final.
- The current local development feedback path uses broad DriverKit user-client
  access and needs a distribution-safe entitlement strategy.

## Installation Status

This release cannot yet be distributed as a simple zip for arbitrary Macs.

The driver must be embedded inside a host app under:

```text
Contents/Library/SystemExtensions/com.axelheckert.driver.FireWireOHCIProbe.dext
```

The host app and DEXT must be signed with matching Apple signing material and
matching embedded provisioning profiles. A public build also needs notarization.

## Suggested Tester Checklist

- Test 44.1 kHz playback for at least 15 minutes.
- Test 48 kHz playback for at least 15 minutes.
- Record input channel 1.
- Test Pro Tools hardware buffers at 64, 128, and 256 samples when available.
- Move all eight faders into Pro Tools.
- Confirm fader feedback from Pro Tools to the Digi 003.
- Confirm Bank/Nudge LEDs.
- Confirm display track names and volume values.
- Reboot and confirm the driver, CoreAudio device, and MIDI bridge come back.
- Test sleep/wake separately and report whether the stream resumes.
