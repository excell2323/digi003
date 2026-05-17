# Digi 003 Driver Notes

## Current State

This repository contains an experimental macOS DriverKit/AudioDriverKit driver for the Digidesign/Avid Digi 003 through a Thunderbolt-to-FireWire OHCI controller on Apple Silicon.

Current active local version:

- Driver: `com.axelheckert.driver.FireWireOHCIProbe`
- Host app: `com.axelheckert.FireWireOHCIProbeLoader`
- Version: `0.2.63/263`
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

### 0.2.56 moving replay queue experiment

This build keeps the 0.2.55 prebuffer-period replay, then continues with a Linux-style moving replay queue while the live stream is running.

Implementation notes:

```text
moving replay queue                 = 512 RX packets
update size                         = 80 TX packets
required update total               = 441 data blocks at 44.1 kHz
TX update lead                      = 512 packets ahead of current OHCI IT command pointer
TX update method                    = rewrite future packet lengths/CIP DBC/header only
payload layout                      = unchanged fixed 6-data-block stride
```

The goal is to test whether the Digi 003 wants a continuously refreshed TX packet cadence rather than one static captured period. The update is intentionally conservative: it only commits an 80-packet update when the queued RX packet count totals exactly 441 data blocks, so the following packet's DBC phase stays compatible with the existing TX ring.

New diagnostics are exposed as `ProbeDigiLiveSequenceReplayMoving*` properties, including queue fill, update success count, bad-total count, bad command-pointer count, last update start index, and last sync return.

First test result:

```text
Captures/coreaudio-digi003-test-0.2.56-movingreplay-3s.wav
repeated_frames=83140
unique_frames=775
sequence_replay_ready=1
sequence_replay_active=0
sequence_replay_apply_attempts=0
moving_append_count=0
```

Interpretation:

This did not exercise moving replay. The prebuffer loop could reach the target ring fill before it had applied the captured 80-packet replay period, then recording continued with replay inactive. Version 0.2.57 keeps prebuffer harvesting while sequence replay is still pending, even if the audio ring is already above the normal prebuffer target.

### 0.2.57 prebuffer replay gate fix

This build keeps the 0.2.56 moving replay implementation and changes only the prebuffer gate: the loop now exits on ring-fill target only after the first 80-packet replay has either been applied or an apply attempt has failed. This should force the actual moving-replay experiment to run.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.57-prebuffergate-movingreplay-3s.wav
repeated_frames=90789
unique_frames=775
sequence_replay_ready=0
sequence_replay_period_count=23
sequence_replay_apply_attempts=0
moving_append_count=0
```

Interpretation:

The prebuffer gate fix worked structurally, but the replay capture was still too strict: it discarded the captured 5/6 cadence whenever RX DBC/cycle continuity was lost. Because DBC/cycle discontinuity is the defect we are trying to compensate for, using it as a hard prerequisite prevents the experiment from running on unstable captures.

### 0.2.58 relaxed replay capture continuity

This build keeps counting RX discontinuities but no longer discards initial or moving replay candidates solely because of DBC/cycle loss. It still rejects invalid data-block counts and still requires each moving 80-packet update to total exactly 441 data blocks at 44.1 kHz.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.58-relaxed-movingreplay-3s.wav
repeated_frames=123338
unique_frames=140
sequence_replay_active=1
sequence_replay_observed_total_data_blocks=439
moving_update_success_count=2
moving_bad_total_count=32
moving_bad_command_ptr_count=78
```

Interpretation:

This finally exercised the replay path, but it also proved that relaxed continuity alone is unsafe: the initial static replay period totalled 439 data blocks instead of 441, so it poisoned the TX clock cadence before the moving queue could help.

### 0.2.59 require valid initial replay total

This build still relaxes continuity, but it only marks the initial 80-packet replay period ready when the total is exactly 441 data blocks. Bad initial periods are counted as `ProbeDigiLiveSequenceReplayBadTotalCount` with the last bad total published as `ProbeDigiLiveSequenceReplayLastBadTotalDataBlocks`.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.59-validtotal-movingreplay-3s.wav
repeated_frames=124106
unique_frames=129
sequence_replay_active=1
sequence_replay_observed_total_data_blocks=441
moving_update_success_count=5
moving_bad_total_count=29
moving_bad_command_ptr_count=50
```

Interpretation:

The initial replay total was valid, but the capture was still much worse than 0.2.55. The moving update path is now the main suspect: on-the-fly descriptor/header rewriting while OHCI IT is running can leave the command pointer unreadable or the transmit context unstable.

### 0.2.60 static replay isolation

This build keeps the 0.2.59 valid-total initial replay gate, but disables moving replay updates (`ProbeDigiLiveSequenceReplayMovingEnabled = 0`). It is an isolation run to confirm whether the running TX-ring rewrite is the regression.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.60-static-validtotal-nomoving-3s.wav
repeated_frames=66794
unique_frames=1095
sequence_replay_active=1
sequence_replay_observed_total_data_blocks=441
moving_enabled=0
```

Interpretation:

Static valid-total replay is stable again and slightly better than 0.2.55. This confirms that the running TX-ring rewrite was the regression; the next experiments should focus on RX harvesting/ring shape rather than changing live transmit descriptors.

### 0.2.61 ASFireWire-style receive ring experiment

This build keeps 0.2.60 static valid-total replay and switches the live IR receive path to one `INPUT_LAST` descriptor per packet. The descriptor receives the OHCI isochronous header plus payload in one contiguous packet buffer (`ProbeDigiLiveSingleDescriptorReceiveEnabled = 1`). This is a clean-room architectural test inspired by ASFireWire's receive-ring shape; no ASFireWire source code is copied.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.61-singleir-asfwstyle-3s.wav
repeated_frames=81418
unique_frames=882
single_descriptor_receive_enabled=1
sequence_replay_active=1
sequence_replay_observed_total_data_blocks=441
harvest_packet_count=10681
rx_dbc_lost_count=1224
rx_cycle_lost_count=1183
```

Interpretation:

The single-descriptor IR shape is worse than the 0.2.60 split-header/payload receive ring. It receives valid 5/6 data-block packets, but harvest throughput drops and repeated frames rise sharply. The useful clue is not the descriptor shape itself, but the command pointer: the hardware IR command pointer can run ahead while the software read index waits on an empty descriptor, implying a late-drain/lost-position problem.

### 0.2.62 command-pointer catch-up experiment

This build returns to the 0.2.60 split-header/payload receive descriptors (`ProbeDigiLiveSingleDescriptorReceiveEnabled = 0`) and adds an IR command-pointer cursor. When the harvester sees an empty descriptor but the OHCI IR command pointer says hardware has advanced by at least eight packets, the software read index jumps forward to the hardware command pointer instead of waiting for the next full ring wrap.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.62-cmdptr-catchup-3s.wav
repeated_frames=47457
unique_frames=1352
single_descriptor_receive_enabled=0
catch_up_count=629
catch_up_skipped_packets=11178
harvest_packet_count=19652
harvest_frame_count=108350
ring_repeated_frames=39939
```

Interpretation:

This is the first large improvement in the live CoreAudio path. Command-pointer catch-up prevents the harvester from waiting for another full ring wrap after it lands on an already-missed empty descriptor. It still skips many packets, so the next refinement is to scan forward for the next filled descriptor before jumping all the way to the hardware command pointer.

### 0.2.63 catch-up scan experiment

This build keeps the 0.2.62 command-pointer cursor, but changes empty-descriptor recovery from a blind jump into a scan. On an empty descriptor, the harvester scans forward up to 256 packets and resumes at the first non-empty descriptor it finds; only if no filled descriptor is found does it jump to the hardware command pointer.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.63-cmdptr-catchup-scan-3s.wav
repeated_frames=4975
unique_frames=2049
ring_repeated_frames=1161
harvest_packet_count=24473
harvest_frame_count=134926
catch_up_count=1548
catch_up_scan_found_count=1548
catch_up_skipped_packets=1885
empty_poll_count=0

Captures/coreaudio-digi003-test-0.2.63-cmdptr-catchup-scan-run2-3s.wav
repeated_frames=5483
unique_frames=2043
```

Interpretation:

This is the new best live CoreAudio capture by a large margin. The harvester now stays close to the expected 44.1 kHz frame rate, and scanning recovers many descriptors that the blind 0.2.62 jump would have skipped. Remaining repeat frames are now small enough that the next work should focus on tighter ring/backpressure behavior and the remaining RX DBC/cycle discontinuities rather than wholesale receive-ring redesign.

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

1. Tune 0.2.63 catch-up/backpressure to reduce the remaining small underrun/repeat count.
2. Reduce RX DBC/cycle lost counts now that harvest throughput is near real-time.
3. Add Digi "double-oh-three" playback encoding before enabling non-silent output.
4. Add MIDI/control-surface and mixer/control support after audio input stability improves.
