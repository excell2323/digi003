# Digi 003 Driver Notes

## Current State

This repository contains an experimental macOS DriverKit/AudioDriverKit driver for the Digidesign/Avid Digi 003 through a Thunderbolt-to-FireWire OHCI controller on Apple Silicon.

Current active local version:

- Driver: `com.axelheckert.driver.FireWireOHCIProbe`
- Host app: `com.axelheckert.FireWireOHCIProbeLoader`
- Version: `0.2.51/251`
- Team ID used locally: `7H3ND356AV`
- Controller: `pci11c1,5901` / IEEE 1394 Open HCI

## Important Findings

- The FireWire controller can be matched and driven from DriverKit with the PCI entitlement.
- The Digi 003 responds to async config/register transactions.
- Duplex isochronous TX/RX can be started.
- The device requires transmit packets before receive packets are stable.
- Linux `snd-firewire-digi00x` indicates that Digi00x ignores CIP SYT in received packets; the data-block cadence is important for media clock recovery.
- The current TX cadence uses the Linux 44.1 kHz 5/6 data-block sequence and a 20480-packet ring so phase and DBC wrap cleanly.
- Robin Gareus' Digi 003 reverse-engineering notes and Linux `amdtp-dot.c` confirm that the stream is IEC 61883-6/AMDTP AM824 with a Digidesign-specific "double-oh-three" middle-byte XOR/salt encoding on playback/output.
- Linux applies the Digi encoding on the IT/write path and reads capture/input as plain AM824; this means the obfuscation is required for future audio output, but it does not explain the current RX harvest discontinuities.

## Latest RX Clock Diagnostic

Capture:

```text
Captures/coreaudio-digi003-test-0.2.48-rxclockdiag-3s.wav
```

Summary:

```text
frames=132300
channels=8
nonzero_frames=132232
repeated_frames=73789
unique_frames=1031
```

IORegistry counters after the run:

```text
ProbeDigiLiveRxDBCPacketCount       = 12449
ProbeDigiLiveRxDBCLostCount         = 1426
ProbeDigiLiveRxCyclePacketCount     = 12449
ProbeDigiLiveRxCycleLostCount       = 1425
ProbeDigiLiveRxDataBlocks5Count     = 6058
ProbeDigiLiveRxDataBlocks6Count     = 6390
ProbeDigiLiveRxUnexpectedDataBlockCount = 1
ProbeDigiLiveRxSYTZeroCount         = 12449
ProbeDigiLiveRxSYTNoInfoCount       = 0
```

Interpretation:

The 44.1 kHz 5/6 data-block cadence is present, but the live harvest is not draining the stream continuously enough. SYT is not useful for synchronization on this device; DBC and OHCI cycle continuity are the important clocks to watch.

## Harvest Experiments After 0.2.48

### 0.2.49 adaptive low-water busy polling

The low-water busy-polling experiment made the stream worse. Attempts increased sharply, but successful harvests dropped; this indicates that spinning around the same IR event gate is counterproductive.

```text
Captures/coreaudio-digi003-test-0.2.49-adaptive-harvest-3s.wav
repeated_frames=91331
unique_frames=649
harvest_packets=7806
```

### 0.2.50 drain256 only

Increasing the per-pass drain limit from 128 to 256 without busy polling was also worse than the 0.2.48 baseline.

```text
Captures/coreaudio-digi003-test-0.2.50-drain256-3s.wav
repeated_frames=83524
unique_frames=770
harvest_packets=11672
```

### 0.2.51 baseline128 with published tuning properties

Returned the drain limit to 128 and kept the diagnostic properties. The second run after the extension had settled returned to roughly the same class as 0.2.48.

```text
Captures/coreaudio-digi003-test-0.2.51-baseline128-run2-3s.wav
repeated_frames=73850
unique_frames=1029
harvest_packets=23882
```

Interpretation:

The best current path is not larger descriptor bursts or lower sleep. The remaining capture problem is stream continuity/drain timing: the driver still loses many DBC/cycle steps and Core Audio repeats frames to cover the gap.

## Stream Format References

- Robin Gareus, "Reverse engineering the Digidesign 003R protocol": https://gareus.org/wiki/digi003
- Linux kernel `sound/firewire/digi00x/amdtp-dot.c`: https://codebrowser.dev/linux/linux/sound/firewire/digi00x/amdtp-dot.c.html
- Proof-of-concept Digi 003 AMDTP encoder: https://github.com/x42/003amdtp

## Next Work

1. Reduce RX ring late-drain behavior until DBC/cycle lost counts approach zero.
2. Move from a static TX ring toward Linux/ASFireWire-style dynamic packet scheduling.
3. Add Digi "double-oh-three" playback encoding before enabling non-silent output.
4. Add MIDI/control-surface and mixer/control support after audio input stability improves.
