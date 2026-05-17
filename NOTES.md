# Digi 003 Driver Notes

## Current State

This repository contains an experimental macOS DriverKit/AudioDriverKit driver for the Digidesign/Avid Digi 003 through a Thunderbolt-to-FireWire OHCI controller on Apple Silicon.

Current active local version:

- Driver: `com.axelheckert.driver.FireWireOHCIProbe`
- Host app: `com.axelheckert.FireWireOHCIProbeLoader`
- Version: `0.2.55/255`
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

### 0.2.52 callback harvest

Harvesting from the Core Audio input callback could help briefly, but it was not stable across runs.

```text
Captures/coreaudio-digi003-test-0.2.52-callbackharvest-3s.wav
repeated_frames=53766
unique_frames=1286
harvest_packets=13464

Captures/coreaudio-digi003-test-0.2.52-callbackharvest-run2-3s.wav
repeated_frames=91333
unique_frames=647
harvest_packets=23768
```

Interpretation:

The callback path sometimes catches useful packets, but it also competes with the existing receive event gate. The second run fell back into the same repeated-frame class as the bad harvest experiments.

### 0.2.53 callback retry

Continuing callback harvest attempts through `kIOReturnNotReady` made the behavior worse, so this experiment should not be kept as a base.

```text
Captures/coreaudio-digi003-test-0.2.53-callbackretry-3s.wav
repeated_frames=91333
unique_frames=647
harvest_packets=10816
harvest_frames=59624
callback_harvest_attempts=7315
callback_harvest_successes=39
ir_event_gate_skips=4980
```

Interpretation:

The retry loop amplifies event-gate misses instead of improving continuity. Version 0.2.54 restores the 0.2.51 baseline behavior with a higher system-extension version number.

### 0.2.55 sequence replay experiment

This build starts porting the Linux 5.14 Digi00x media-clock fix into the macOS driver. Linux fixed the Digi00x clicks/pops by caching the received packet sequence and replaying the number of data blocks per packet on the transmit side, because Digi00x devices ignore CIP SYT and recover clock from the data-block cadence.

Implementation notes:

```text
ProbeDigiLiveSequenceReplayEnabled = 1
replay period                      = 80 packets
TX payload layout                  = fixed 6-data-block stride per packet
replay source                      = first continuous RX 5/6 data-block period
apply timing                       = during CoreAudio prebuffer before IO starts
```

The first version replays a clean 80-packet cadence period and restarts the OHCI IT/IR contexts during prebuffer. It exposes `ProbeDigiLiveSequenceReplay*` IORegistry properties so the run can show whether the period was captured, whether it differed from the ideal cadence, and whether the replay restart succeeded.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.55-seqreplay-3s.wav
repeated_frames=67524
unique_frames=1020
harvest_packets=13371
harvest_frames=73710
sequence_replay_ready=1
sequence_replay_active=1
sequence_replay_apply_ret=0
rx_dbc_lost=1586
rx_cycle_lost=1573
```

Interpretation:

This is a meaningful improvement over the 0.2.51 baseline repeated-frame count (`67524` vs `73850`), and it proves the Linux sequence-replay idea is relevant on macOS too. It is not enough yet: DBC/cycle loss remains high, so the next step should be a true moving replay queue like Linux uses, not just a single 80-packet prebuffer-period replay.

## Stream Format References

- Robin Gareus, "Reverse engineering the Digidesign 003R protocol": https://gareus.org/wiki/digi003
- Linux kernel `sound/firewire/digi00x/amdtp-dot.c`: https://codebrowser.dev/linux/linux/sound/firewire/digi00x/amdtp-dot.c.html
- Proof-of-concept Digi 003 AMDTP encoder: https://github.com/x42/003amdtp
- Rust `firewire-digi00x-protocols` crate for async clock, optical, and monitor controls: https://docs.rs/firewire-digi00x-protocols/latest/firewire_digi00x_protocols/
- ZamAudio note that Linux 5.14 removed Digi 002/003R clicks/pops: https://www.zamaudio.com/?p=2567
- Linux fix commit `019af5923c8a` ("perform sequence replay for media clock recovery"): https://git.kernel.org/pub/scm/linux/kernel/git/tiwai/sound.git/commit/sound/firewire?h=for-next&id=019af5923c8a46b581fc2f2d670dcc0714a80bf0

## Async Control Reference

The Rust `firewire-digi00x-protocols` crate is supplemental runtime code for internal functions outside the isochronous packet stream. It is useful for later mixer/control-surface work, but it does not solve the current live RX harvest issue.

Known Digi 003 async register map from the crate:

```text
base address                         = 0xffffe0000000
media clock rate                     = 0x0110
external clock rate                  = 0x0114
sampling clock source                = 0x0118
optical interface mode               = 0x011c
monitor enable                       = 0x0124
external clock source detection      = 0x012c
monitor source gain base             = 0x0300
monitor destinations                 = 2
monitor sources per destination      = 18
monitor gain range                   = 0x00..0x80 (-48.0 dB..0.0 dB)
monitor destination stride           = 4
monitor source stride                = 8
```

## Next Work

1. Move from the 0.2.55 static 80-packet replay to a Linux-style moving replay queue.
2. Reduce RX ring late-drain behavior until DBC/cycle lost counts approach zero.
3. Add Digi "double-oh-three" playback encoding before enabling non-silent output.
4. Add MIDI/control-surface and mixer/control support after audio input stability improves.
