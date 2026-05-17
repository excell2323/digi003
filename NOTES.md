# Digi 003 Driver Notes

## Current State

This repository contains an experimental macOS DriverKit/AudioDriverKit driver for the Digidesign/Avid Digi 003 through a Thunderbolt-to-FireWire OHCI controller on Apple Silicon.

Current active local version:

- Driver: `com.axelheckert.driver.FireWireOHCIProbe`
- Host app: `com.axelheckert.FireWireOHCIProbeLoader`
- Version: `0.2.136/336`
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

Captures/coreaudio-digi003-test-0.2.63-cmdptr-catchup-scan-10s.wav
total_repeated_frames=26979
after_1s_repeated_frames=19328
ring_produced_frames=416226
ring_consumed_frames=416052
ring_repeated_frames=25116
```

Interpretation:

This is the new best live CoreAudio capture by a large margin. The 3-second captures can be nearly clean after the startup silence. A 10-second capture shows the remaining issue more clearly: the harvester still falls behind the 44.1 kHz consumer over longer runs, producing about 416k frames while CoreAudio consumes 441k frames. Remaining repeat frames are now a worker/backpressure problem more than a receive-ring shape problem.

### 0.2.64 low-water worker experiment

This build keeps the 0.2.63 catch-up scan and sets `ProbeDigiLiveWorkerLowWaterFrames = 4096`, so the background harvest worker skips its normal 1 ms sleep whenever the audio ring has less than 4096 frames buffered. The goal is to reduce long-run underruns without doing FireWire harvesting directly in the CoreAudio IO callback.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.64-lowwater4096-10s.wav
total_repeated_frames=77835
after_1s_repeated_frames=72040
ring_produced_frames=359478
ring_repeated_frames=82334
empty_poll_count=34373
worker_low_water_no_sleep_count=79359
```

Interpretation:

This is worse than 0.2.63. The low-water no-sleep rule also fired when the previous harvest returned no packet, causing a tight empty-poll loop that starved useful work instead of improving throughput.

### 0.2.65 successful-harvest low-water experiment

This build keeps the low-water idea but makes it conservative: `ProbeDigiLiveWorkerLowWaterFrames = 2048`, and the worker skips sleep only when the previous harvest succeeded. If no packet is ready, it sleeps normally. The goal is to gain a little long-run headroom without recreating the 0.2.64 empty-poll storm.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.65-lowwater2048-successonly-10s.wav
total_repeated_frames=34710
after_1s_repeated_frames=26371
ring_produced_frames=426474
ring_repeated_frames=35986
ring_overrun_frames=4476
empty_poll_count=1144
```

Interpretation:

This is still worse than 0.2.63. It avoids the 0.2.64 empty-poll storm, but produces bursts too late and can overfill the ring after underruns have already happened.

### 0.2.66 callback low-water harvest experiment

This build returns `ProbeDigiLiveWorkerLowWaterFrames` to `0` and enables `ProbeAudioRuntimeInputCallbackHarvestEnabled = 1` with at most two harvest attempts from the CoreAudio read callback when ring fill is below the callback request plus 2048 frames. The goal is to recover just-in-time before an underrun without spinning the background worker.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.66-callbackharvest2-10s.wav
total_repeated_frames=21431
after_1s_repeated_frames=13944
ring_produced_frames=422365
ring_repeated_frames=20039
callback_harvest_success_count=4922
callback_harvest_attempt_count=11897
```

Interpretation:

This improves the 10-second run compared with 0.2.63 and avoids the 0.2.64/0.2.65 worker scheduling regressions. The ring still stays low, so the next test raises the callback harvest cap from two attempts to four.

### 0.2.67 callback harvest depth experiment

This build keeps 0.2.66 but raises `ProbeAudioRuntimeInputCallbackHarvestMaxAttempts` from 2 to 4. The goal is to see whether a little more just-in-time harvest depth can cover the remaining long-run underrun bursts without blocking the IO callback too much.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.67-callbackharvest4-10s.wav
total_repeated_frames=21379
after_1s_repeated_frames=13824
last_5s_repeated_frames=9344
per_second_repeated_frames=7555 0 0 3136 1344 2816 2077 3618 468 364
ring_produced_frames=438256
ring_consumed_frames=417326
ring_repeated_frames=23842
ring_overrun_frames=4546
ring_current_fill_frames=16384
callback_harvest_success_count=4932
callback_harvest_attempt_count=12282
```

Repeat run:

```text
Captures/coreaudio-digi003-test-0.2.67-callbackharvest4-run2-10s.wav
total_repeated_frames=23328
after_1s_repeated_frames=20273
last_5s_repeated_frames=15921
per_second_repeated_frames=3055 0 0 430 3922 1984 848 5443 5357 2289
```

Live diagnostics during the repeat run showed `ring_overrun_frames=0` while the recorder was still active and the ring running low (`ring_current_fill_frames` near 35 by the late capture window). The full ring and overrun counters appeared after the recorder exited, because harvesting continued while no CoreAudio client consumed frames.

Interpretation:

The 10-second capture shows the repeat problem is not only an initial startup artifact. 0.2.67 is sometimes marginally better than 0.2.66 in the WAV-level repeated-frame metric, but the repeat run is worse. A deeper callback harvest can move the underrun bursts around and improve some windows, but it is not a stable sync solution by itself. The active-capture failure mode is still low ring fill/underrun, not sustained overrun.

### 0.2.68 active-capture catch-up experiment

This build keeps the 0.2.67 callback harvest depth and adds active-capture diagnostics around the CoreAudio read callback. It also lets the background harvest worker use a short catch-up path only while recent read callbacks are active and the ring is below 4096 frames. If fill is below 1024 frames it skips the worker sleep after a successful harvest; otherwise it uses a 125 us delay instead of the normal 1 ms sleep. Empty polls still sleep normally so this should avoid the 0.2.64 empty-poll storm.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.68-activecatchup-10s.wav
total_repeated_frames=34171
after_1s_repeated_frames=26724
last_5s_repeated_frames=22161
per_second_repeated_frames=7447 0 1024 3264 274 6829 3031 4264 3001 5035
active_underrun_frames=31170
active_overrun_frames=0
worker_active_catchup_no_sleep_count=2794
worker_active_catchup_delay_count=422
```

Interpretation:

This is worse than 0.2.67. The active diagnostics were useful: during the capture the failure mode is still active underrun, not active overrun. The catch-up worker path also increased empty polls and did not keep the ring above the callback request, so the behavior is disabled again in 0.2.69 while keeping the diagnostics.

### 0.2.69 active diagnostics only

This build keeps the 0.2.67 callback harvest behavior and disables the 0.2.68 active-catch-up worker path (`kDigiLiveActiveCaptureCatchUpEnabled = 0`). The active-capture diagnostics remain so future 10-second captures can distinguish active underrun/overrun from post-capture ring fill.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.69-active-diag-only-10s.wav
total_repeated_frames=37911
after_1s_repeated_frames=31260
last_5s_repeated_frames=23452
per_second_repeated_frames=6651 0 0 1920 5888 3712 5056 4528 10128 28
```

Interpretation:

Even the extra IO-callback diagnostics are too expensive in the current hot path and made the capture worse than 0.2.67. 0.2.70 removes the active-capture diagnostic/catch-up code and returns to the 0.2.67 runtime behavior with only the callback harvest depth of four.

### 0.2.70 return to best runtime behavior

This build removes the 0.2.68/0.2.69 active-capture instrumentation from the realtime path. It keeps `ProbeAudioRuntimeInputCallbackHarvestEnabled = 1`, `ProbeAudioRuntimeInputCallbackHarvestMaxAttempts = 4`, and `ProbeDigiLiveWorkerLowWaterFrames = 0`.

Test result:

```text
Captures/coreaudio-digi003-test-0.2.70-return-067-runtime-10s.wav
total_repeated_frames=32424
after_1s_repeated_frames=25408
last_5s_repeated_frames=14464
per_second_repeated_frames=7016 0 3904 1580 5460 0 3748 5467 4032 1216
ring_repeated_frames=23945
ring_overrun_frames=4463
callback_harvest_success_count=4846
callback_harvest_attempt_count=12108
```

Interpretation:

0.2.70 removes the measurable realtime-path overhead from 0.2.68/0.2.69 and is better than those experiments, but this run is still worse than the best 0.2.67 run. The remaining instability is probably run-to-run timing/jitter in the live harvest path rather than only a fixed algorithmic regression.

### 0.2.71 IR event gate bypass experiment

This build keeps the 0.2.70 runtime behavior but sets `kDigiLiveRequireIREventBeforeSync = 0`. The worker still polls and clears the OHCI IR event bit for diagnostics, but a missed event no longer prevents a DMA sync/descriptor harvest. The goal is to test whether missed/coalesced event bits are causing late RX-ring reads and catch-up skips.

### 0.2.97 fast init and full-sync baseline

This build skips the slow debug-only async Config ROM/register probe and the old startup duplex probe when the known Digi 003 path is enabled (`ProbeFastKnownDigi003InitEnabled = 1`). The driver now reaches Stage 20 immediately in local tests instead of spending tens of seconds in Stage 13. The live audio start still performs the required Digi begin transactions.

Runtime remains the best long-run baseline shape: 2048 IR descriptors, full descriptor sync after harvested packets, 65536-frame audio ring, 49152-frame diagnostic prebuffer, callback harvest enabled, and worker low-water at 49152 frames.

```text
Captures/coreaudio-digi003-test-0.2.97-fastinit-fullsync-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0

Captures/coreaudio-digi003-test-0.2.97-fastinit-fullsync-30s.wav
total_repeated_frames=55879
after_1s_repeated_frames=50304
last_10s_repeated_frames=31040
last_5s_repeated_frames=20160
```

Interpretation:

Fast init is a clear win for iteration speed and does not break the 10-second clean-after-start behavior. The 30-second run still shows late underrun/repeat bursts, so the remaining issue is not startup probing; it is still live RX continuity and the catch-up path.

### 0.2.98 catch-up threshold experiment

This build keeps the 0.2.97 fast-init/full-sync baseline and raises `ProbeDigiLiveIRCommandPtrCatchUpMinPackets` from 8 to 32. The goal is to avoid treating short command-pointer/read-index races as packet loss.

```text
Captures/coreaudio-digi003-test-0.2.98-catchup32-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0

Captures/coreaudio-digi003-test-0.2.98-catchup32-30s.wav
total_repeated_frames=63576
after_1s_repeated_frames=57029
last_10s_repeated_frames=33285
last_5s_repeated_frames=14661
rx_dbc_lost_count=27331
empty_catch_up_skipped_packets=27224
```

Interpretation:

This is mixed. The 10-second behavior remains clean after startup, and the final 5-second window improves compared with 0.2.97, but the total/after-1s/last-10s repeated-frame counts are worse. DBC/cycle loss and skipped packets dropped, which suggests the catch-up threshold is connected to the right failure mode, but the threshold alone is not a complete stability fix.

Rejected experiments:

- `0.2.94`: 3072 IR descriptors with corrected memory layout loaded and linked, but 30-second audio was worse than the 2048-descriptor baseline.
- `0.2.95`/`0.2.96`: selective descriptor/data range sync experiments rearmed only the first RX ring pass and then starved the stream. Keep full descriptor sync for now.
- `0.2.99`: enabling the existing sequence replay/moving replay implementation made the stream collapse into silence/repeats after the replay apply. Linux uses on-the-fly sequence replay, so this remains conceptually important, but our current hard apply/restart path is not usable as-is.

### 0.2.100 restored fast-init baseline

This restores the usable 10-second behavior after the failed 0.2.99 replay test. It keeps fast init enabled, sequence replay disabled, moving replay disabled, and returns the catch-up threshold to 8 packets.

```text
Captures/coreaudio-digi003-test-0.2.100-restored-fastinit-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
ring_underrun_frames=0
```

### 0.2.101 event-gate off rejection

This repeated the older event-gate bypass idea on the fast-init/full-sync baseline by setting `kDigiLiveRequireIREventBeforeSync = 0`.

```text
Captures/coreaudio-digi003-test-0.2.101-eventgate-off-10s.wav
after_1s_repeated_frames=4305
last_5s_repeated_frames=4305
ring_underrun_frames=9741
rx_dbc_lost_count=11411
rx_cycle_lost_count=11419
```

Interpretation:

Bypassing the IR event gate is worse than the 0.2.100 baseline, which had no repeats after the first second in the 10-second test. Version 0.2.102 restores the event-gated fast-init baseline with only the version number advanced so macOS can upgrade the installed system extension cleanly.

### 0.2.102 restored event-gated fast-init baseline

This is the current installed checkpoint. It restores `kDigiLiveRequireIREventBeforeSync = 1`, keeps sequence replay disabled, keeps catch-up at 8 packets, and keeps fast init enabled.

```text
Captures/coreaudio-digi003-test-0.2.102-restored-eventgate-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
ring_underrun_frames=0
```

Interpretation:

This returns the short-run capture behavior to the usable 0.2.100 shape after the rejected 0.2.101 event-gate bypass test.

### 0.2.103 low-water event-gate bypass rejection

This kept the normal IR event gate, but allowed missed-event bypass only when the audio ring fell below 16384 frames.

```text
Captures/coreaudio-digi003-test-0.2.103-lowwater-ir-bypass-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0

Captures/coreaudio-digi003-test-0.2.103-lowwater-ir-bypass-30s.wav
total_repeated_frames=109603
after_1s_repeated_frames=104000
last_10s_repeated_frames=57664
last_5s_repeated_frames=36785
ir_event_gate_bypass_count=26457
drain_busy_count=9809
```

Interpretation:

The targeted bypass did not rescue late underruns; once the ring went low it triggered a large number of extra sync attempts and made the 30-second capture worse. Version 0.2.104 restores the event-gated 0.2.102 behavior with only the version advanced.

0.2.104 control capture:

```text
Captures/coreaudio-digi003-test-0.2.104-restored-after-lowwater-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
```

### 0.2.105 moving-only sequence replay rejection

This attempted Linux-style moving sequence replay without the older stop/restart apply path. RX packet data-block counts were queued continuously and future TX descriptors were updated while the stream was running.

```text
Captures/coreaudio-digi003-test-0.2.105-moving-only-10s.wav
nonzero_frames=0
total_repeated_frames=440999
moving_append_count=22057
moving_update_success_count=4
moving_bad_command_ptr_count=1022
moving_bad_total_count=89
ring_underrun_frames=319812
rx_dbc_lost_count=1715
rx_cycle_lost_count=1717
```

Interpretation:

Updating future TX descriptors while running is still conceptually the right Linux-shaped direction, but this implementation is not safe: it often cannot map the live IT command pointer and collapses capture to silence. Version 0.2.106 restores the moving-replay-disabled baseline while keeping the code path available for a more careful rework.

0.2.106 control capture:

```text
Captures/coreaudio-digi003-test-0.2.106-restored-after-moving-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
```

### 0.2.107 worker-only harvest rejection

This disabled Core Audio callback harvesting (`kAudioCallbackHarvestEnabled = 0`) and left only the background worker draining live RX packets.

```text
Captures/coreaudio-digi003-test-0.2.107-worker-only-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
drain_busy_count=0

Captures/coreaudio-digi003-test-0.2.107-worker-only-30s.wav
total_repeated_frames=75342
after_1s_repeated_frames=68887
last_10s_repeated_frames=37079
last_5s_repeated_frames=19131
drain_busy_count=0
```

Interpretation:

Removing callback harvest eliminates worker/callback contention, but it does not improve the long-run capture quality. It is slightly worse than the callback-enabled baseline in total repeats and final 5 seconds. Version 0.2.108 restores callback harvest.

0.2.108 control capture:

```text
Captures/coreaudio-digi003-test-0.2.108-restored-after-worker-only-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
```

### 0.2.109 receive IRQ interval 4 rejection

This reduced `kDigiLiveReceiveIRQInterval` from 8 to 4 to test whether more frequent receive events would reduce event-gate misses and late ring starvation.

```text
Captures/coreaudio-digi003-test-0.2.109-irq4-10s.wav
after_1s_repeated_frames=1272
last_5s_repeated_frames=1272
ring_underrun_frames=3235
ir_event_gate_skip_count=2418
drain_busy_count=1742
```

Interpretation:

More frequent receive IRQ descriptors did not help; it caused underruns within the 10-second run. Version 0.2.110 restores the 8-packet receive IRQ interval.

0.2.110 control capture:

```text
Captures/coreaudio-digi003-test-0.2.110-restored-after-irq4-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
```

### 0.2.112 ASFireWire-style stream diagnostics

Added RX StreamProcessor diagnostics inspired by ASFireWire's IEC 61883 receive path:

```text
ProbeDigiLiveRxSID
ProbeDigiLiveRxDBS
ProbeDigiLiveRxSPH
ProbeDigiLiveRxFMT
ProbeDigiLiveRxFDF
ProbeDigiLiveRxEventCount
ProbeDigiLiveRxPayloadRemainderCount
ProbeDigiLiveRxStreamProcessorPacketCount
ProbeDigiLiveRxStreamProcessorValidPacketCount
ProbeDigiLiveRxDBCDelta
ProbeDigiLiveRxMaxDBCDelta
ProbeDigiLiveRxCadencePeriod
ProbeDigiLiveRxCadenceBestPhase
ProbeDigiLiveRxCadenceBestPhaseMismatchCount
```

10-second capture:

```text
Captures/coreaudio-digi003-test-0.2.112-streamphase-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
ProbeDigiLiveRxCadenceBestPhase=51
ProbeDigiLiveRxCadenceBestPhaseMismatchCount=0
```

30-second capture:

```text
Captures/coreaudio-digi003-test-0.2.112-streamphase-30s.wav
repeated_frames=90875
after_1s_repeated_frames=85892
last_10s_repeated_frames=50808
last_5s_repeated_frames=28484
ProbeDigiLiveRxCadenceBestPhase=64
ProbeDigiLiveRxCadenceBestPhaseMismatchCount=0
ProbeDigiLiveRxDBCLostCount=23041
ProbeDigiLiveRxCycleLostCount=23062
ProbeDigiLiveIREmptyCatchUpCount=26644
ProbeDigiLiveIREmptyCatchUpSkippedPackets=33633
```

Interpretation:

The Digi 003 receive stream is the expected IEC 61883-6/AM824 layout: DBS 19, FMT 0x10, FDF 1, SPH 0, and a valid 80-packet / 441-data-block cadence. The cadence can start at different phases, so the phase-aligned mismatch counter is the useful check. The remaining 30-second failure is not caused by unknown sample cadence; it is still a live IR harvest continuity problem.

### 0.2.113 receive command-pointer catchup disabled rejection

A/B test disabling `kDigiLiveIRCommandPtrCatchUpEnabled`.

```text
Captures/coreaudio-digi003-test-0.2.113-catchup-off-10s.wav
repeated_frames=202007
after_1s_repeated_frames=196508
last_5s_repeated_frames=122786
ProbeAudioRuntimeRingUnderrunFrames=180421
ProbeDigiLiveIRBacklogPackets=58333
ProbeDigiLiveIREmptyCatchUpCount=0
```

Interpretation:

Disabling catchup is clearly worse. It prevents skipped-packet correction, but the software cursor then falls far behind and the audio ring starves. Version `0.2.114/314` restores catchup enabled and keeps only the stream diagnostics.

0.2.114 restored control capture:

```text
Captures/coreaudio-digi003-test-0.2.114-restored-streamdiag-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
ProbeDigiLiveIRCommandPtrCatchUpEnabled=1
```

### 0.2.115 segment catchup rejection

Added a segment catchup experiment that only advances the software IR cursor when an
8-packet run is ready. This was inspired by ASFireWire's sequential IR drain model,
but it performs worse on the current Digi 003 path.

```text
Captures/coreaudio-digi003-test-0.2.115-segment-catchup-10s.wav
repeated_frames=12791
after_1s_repeated_frames=9008
last_5s_repeated_frames=9008
ProbeDigiLiveIRSegmentCatchUpAttemptCount=6143
ProbeDigiLiveIRSegmentCatchUpSuccessCount=4112
ProbeDigiLiveIRSegmentCatchUpFailureCount=2031
ProbeAudioRuntimeRingRepeatedFrames=7083
```

Interpretation:

Segment-level catchup is too conservative for the current polling loop. It reduces
some speculative reads, but it also delays the software cursor enough to create
audible repeats inside a 10-second capture. The code remains present for diagnostics
and disabled by default.

### 0.2.116 segment catchup disabled control

Disabled the segment-catchup experiment and restored the 0.2.114 cursor behavior.

```text
Captures/coreaudio-digi003-test-0.2.116-segment-disabled-10s.wav
repeated_frames=5615
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeDigiLiveIRSegmentCatchUpEnabled=0
ProbeAudioRuntimeRingUnderrunFrames=0
```

30-second capture:

```text
Captures/coreaudio-digi003-test-0.2.116-segment-disabled-30s.wav
repeated_frames=67197
after_1s_repeated_frames=61682
last_10s_repeated_frames=36786
last_5s_repeated_frames=21962
ProbeAudioRuntimeRingUnderrunFrames=76162
ProbeDigiLiveRxDBCLostCount=23086
ProbeDigiLiveRxCycleLostCount=23100
```

Interpretation:

The short run is clean after the initial start-up fill, but the long run still
starves. This confirms the remaining issue is long-run harvest scheduling/latency,
not the IEC 61883 cadence parser.

### 0.2.117 RX payload range sync rejection

Added descriptor and payload-range DMA sync diagnostics to test whether the DriverKit
full-buffer sync was too expensive. The active experiment synced descriptor memory
first, then only the packet payload before parsing.

```text
Captures/coreaudio-digi003-test-0.2.117-rx-range-sync-10s.wav
repeated_frames=432806
after_1s_repeated_frames=396900
last_5s_repeated_frames=220500
ProbeDigiLiveHarvestPacketCount=2042
ProbeAudioRuntimeRingRepeatedFrames=430305
ProbeDigiLiveRxDescriptorSyncCount=17406
ProbeDigiLiveRxPayloadSyncCount=2042
```

Interpretation:

Per-payload range sync in the hot path is not viable here. The harvest loop falls
behind almost immediately. Version 0.2.118 restores full-buffer sync for safety while
keeping the sync diagnostics.

### 0.2.118 full-sync restore and ASFireWire comparison

Restored the safe receive DMA mode:

```text
ProbeDigiLiveReceiveFullBufferSyncEnabled=1
ProbeDigiLiveReceivePayloadRangeSyncEnabled=0
ProbeDigiLiveIRSegmentCatchUpEnabled=0
```

10-second capture:

```text
Captures/coreaudio-digi003-test-0.2.118-fullsync-restore-10s.wav
repeated_frames=5327
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
```

30-second capture:

```text
Captures/coreaudio-digi003-test-0.2.118-fullsync-restore-30s.wav
repeated_frames=63431
after_1s_repeated_frames=58368
after_2s_repeated_frames=58368
last_10s_repeated_frames=32192
last_5s_repeated_frames=15680
ProbeAudioRuntimeRingUnderrunFrames=75817
ProbeDigiLiveRxDBCLostCount=23047
ProbeDigiLiveRxCycleLostCount=23070
ProbeDigiLiveIREmptyCatchUpCount=26858
ProbeDigiLiveRxDescriptorSyncBytes=53200551936
```

ASFireWire status and lessons:

- DeepWiki says ASFireWire has working OHCI init, DMA management, interrupt
  dispatch, bus reset/self-ID, async transactions, isoch transmit, AV/C, IRM, and CMP.
- Their isoch transmit path is functional; isoch receive is explicitly still in
  progress, with basic DMA setup complete and experimental stream processing.
- Their receive model is still useful: OHCI IR ring -> sequential completed-descriptor
  drain -> IEC 61883/CIP validation -> AM824 decode -> CoreAudio queue.
- The major architectural difference from our current Digi path is interrupt-driven
  IR draining. ASFireWire dispatches OHCI interrupt snapshots and handles IsoRecv
  context events; our current successful short-run path still depends heavily on
  polling from the CoreAudio callback and refresh worker.

References:

- https://deepwiki.com/mrmidi/ASFireWire/1-home
- https://deepwiki.com/mrmidi/ASFireWire/2.5-isochronous-audio-stack
- https://deepwiki.com/mrmidi/ASFireWire/5.2-iec-61883-audio-streaming

### 0.2.119-0.2.125 DriverKit interrupt harvest experiments

Added an ASFireWire-style `IOInterruptDispatchSource` scaffold:

```text
FireWireOHCIProbe::InterruptOccurred()
IOPCIDevice::ConfigureInterrupts(MSIX, fallback MSI)
IOInterruptDispatchSource::Create(...)
OHCI IntEvent/IsoRecv/IsoXmit snapshot diagnostics
optional Digi harvest when the IsoRecv context bit fires
```

0.2.119 proved that DriverKit OHCI interrupts can be delivered. It also exposed a
stop-path bug: disabling the interrupt source during stream stop could leave the
driver in `stopping`, so the recorder wrote audio but hung before finalizing the WAV
header. 0.2.120 fixed the stop path by marking the stream not running before teardown
and waiting briefly for any active harvest before releasing DMA memory.

0.2.121 fixed a second issue: when the interrupt source remained enabled after audio
stop, the old global diagnostic interrupt mask allowed `cycleSynch` interrupts to
storm. The fix was to limit the global OHCI mask to isoch events only while the
interrupt experiment is enabled, and clear the global mask at stop.

Measured results:

```text
Captures/coreaudio-digi003-test-0.2.119-irq-harvest-10s.wav
after_1s_repeated_frames=0
last_5s_repeated_frames=0
ProbeOHCIInterruptReady=1
ProbeOHCIInterruptIsoRecvCount=1141
```

```text
Captures/coreaudio-digi003-test-0.2.121-irq-maskfix-30s.wav
repeated_frames=87971
after_1s_repeated_frames=86656
last_10s_repeated_frames=47232
last_5s_repeated_frames=28523
ProbeOHCIInterruptIsoRecvCount=2618
ProbeAudioRuntimeRingUnderrunFrames=89046
```

Callback-off with IRQ still active reduced lock contention but made the long run
worse:

```text
Captures/coreaudio-digi003-test-0.2.122-irq-callback-off-30s.wav
repeated_frames=125093
after_1s_repeated_frames=117246
last_10s_repeated_frames=63918
last_5s_repeated_frames=29529
ProbeAudioRuntimeInputCallbackHarvestEnabled=0
ProbeDigiLiveDrainBusyCount=692
ProbeAudioRuntimeRingUnderrunFrames=91027
```

IRQ every packet was rejected immediately:

```text
Captures/coreaudio-digi003-test-0.2.123-irq-every-packet-10s.wav
repeated_frames=12755
after_1s_repeated_frames=10568
last_5s_repeated_frames=10568
ProbeDigiLiveReceiveIRQInterval=1
ProbeOHCIInterruptIsoRecvCount=10512
ProbeAudioRuntimeRingUnderrunFrames=10099
```

0.2.124/0.2.125 restore the safer polling/callback behavior and keep the interrupt
scaffold compiled but disabled by default:

```text
ProbeOHCIInterruptDispatchEnabled=0
ProbeOHCIInterruptReady=0
ProbeOHCIInterruptCount=0
ProbeDigiLiveReceiveIRQInterval=8
ProbeAudioRuntimeInputCallbackHarvestEnabled=1
```

0.2.125 control captures:

```text
Captures/coreaudio-digi003-test-0.2.125-safe-restore-10s.wav
repeated_frames=4943
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
```

```text
Captures/coreaudio-digi003-test-0.2.125-safe-restore-30s.wav
repeated_frames=99427
after_1s_repeated_frames=94056
last_10s_repeated_frames=60008
last_5s_repeated_frames=33256
ProbeAudioRuntimeRingUnderrunFrames=89938
ProbeDigiLiveRxDBCLostCount=24510
ProbeDigiLiveRxCycleLostCount=24544
```

Interpretation:

The ASFireWire interrupt model is structurally correct and useful, but directly
calling the current Digi harvester from the DriverKit interrupt source does not fix
the 30-second starvation. The IRQ source fires much less usefully than expected at
the 8-packet interval, while every-packet IRQ creates too much contention/empty work.
The next fix should not be "more harvest callers"; it should make the single harvest
path cheaper and more deterministic, or change the ring policy so CoreAudio cannot
drain below the producer during long active captures.

### 0.2.126-0.2.128 low-water and empty-catchup rejections

To avoid spending more test cycles on already weak directions, two narrow follow-up
experiments were rejected after 10-second captures only:

```text
Captures/coreaudio-digi003-test-0.2.126-lowwater-bypass-10s.wav
after_1s_repeated_frames=6656
last_5s_repeated_frames=6656
ProbeDigiLiveIREventLowWaterBypassEnabled=1
ProbeDigiLiveIREventLowWaterBypassFrames=32768
ProbeDigiLiveIREventGateBypassCount=5730
ProbeAudioRuntimeRingUnderrunFrames=7589
ProbeDigiLiveRxDBCLostCount=11020
ProbeDigiLiveRxCycleLostCount=11040
```

```text
Captures/coreaudio-digi003-test-0.2.127-empty-defer-10s.wav
after_1s_repeated_frames=2956
last_5s_repeated_frames=2956
ProbeDigiLiveIREmptyCatchUpDeferCount=1293
ProbeAudioRuntimeRingUnderrunFrames=7667
ProbeDigiLiveRxDBCLostCount=9139
ProbeDigiLiveRxCycleLostCount=9147
```

Interpretation:

Low-water event-gate bypass repeats the older 0.2.103 failure mode and should stay
off. Deferring empty-descriptor catchup while the ring still has reserve also does
not help; it can leave the callback starved before the worker catches up. 0.2.128
restores the 0.2.125 runtime behavior with only the bundle version advanced:

```text
Captures/coreaudio-digi003-test-0.2.128-safe-rollback-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
ProbeOHCIInterruptDispatchEnabled=0
ProbeDigiLiveIREventLowWaterBypassEnabled=0
```

### 0.2.129 TX max-payload initialization

Prepared the transmit ring for safer Linux-style on-the-fly sequence replay by
initializing all six reserved AM824 payload data blocks for every IT packet. The
active packet descriptor still advertises only the selected 5/6 data-block length,
but a later 5-to-6 replay update can no longer expose an uninitialized sixth block.

This does not enable sequence replay yet; it is a prerequisite for the next dry-run
or moving-replay experiment.

```text
Captures/coreaudio-digi003-test-0.2.129-tx-max-payload-init-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
ProbeDigiLiveSequenceReplayMovingEnabled=0
```

### 0.2.130 moving sequence replay dry-run

Enabled the moving replay queue in dry-run mode only. The driver now queues RX
data-block counts, validates 80-packet update windows, reads the live IT command
pointer, calculates the future update start and DBC range, and consumes valid dry-run
windows. It does not rewrite TX descriptors, sync TX DMA ranges, or wake the IT
context.

```text
Captures/coreaudio-digi003-test-0.2.130-moving-dryrun-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=196
ProbeDigiLiveSequenceReplayMovingDryRunPacketCount=15680
ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount=0
ProbeDigiLiveSequenceReplayMovingBadTotalCount=467
ProbeDigiLiveSequenceReplayMovingInvalidCount=760
ProbeDigiLiveSequenceReplayMovingLastTotalDataBlocks=439
ProbeDigiLiveSequenceReplayMovingLastCurrentPacketIndex=9488
ProbeDigiLiveSequenceReplayMovingLastUpdateStartIndex=10000
ProbeDigiLiveSequenceReplayMovingLastStartDBC=85
ProbeDigiLiveSequenceReplayMovingLastEndDBC=14
```

Interpretation:

The dry-run does not destabilize the 10-second input path, and the earlier moving
replay command-pointer failure is not reproduced when no live descriptor rewrite is
performed. The remaining blocker is the replay source sequence: raw RX windows are
often invalid or total 439 instead of 441 data blocks. The next replay step should
derive a phase-aligned 80-packet sequence from the validated RX cadence diagnostics
instead of blindly consuming every raw moving queue window.

### 0.2.131-0.2.133 phase-aligned moving replay dry-run

`0.2.131` changed the moving replay dry-run to use a phase-aligned synthetic
5/6 data-block sequence instead of replaying the raw moving queue counts. That
first variant required the global RX cadence detector to be ready, so it stayed
safe but did not exercise the replay path:

```text
Captures/coreaudio-digi003-test-0.2.131-cadencephase-dryrun-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=0
ProbeDigiLiveSequenceReplayMovingCadenceNotReadyCount=3931
ProbeDigiLiveRxCadenceReady=0
```

`0.2.132` added queue-local phase learning from 80-packet windows whose raw
total is 441 data blocks. Keeping a zero-mismatch requirement was still too
strict for live harvest windows; it rejected 1330 learnable windows and again did
not exercise the dry-run rewrite path.

```text
Captures/coreaudio-digi003-test-0.2.132-queuephase-dryrun-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=0
ProbeDigiLiveSequenceReplayMovingCadenceLearnRejectCount=1330
ProbeDigiLiveSequenceReplayMovingLastRawTotalDataBlocks=442
```

`0.2.133` keeps the phase-aligned synthetic dry-run but allows the queue learner
to pick the best phase even when the raw 80-packet window is not a perfect
ideal-pattern match. This finally exercises the intended moving replay path while
still not rewriting TX descriptors, syncing TX DMA, or waking the IT context.

```text
Captures/coreaudio-digi003-test-0.2.133-queuephase-loose-dryrun-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=700
ProbeDigiLiveSequenceReplayMovingDryRunPacketCount=56000
ProbeDigiLiveSequenceReplayMovingCadencePhaseUseCount=700
ProbeDigiLiveSequenceReplayMovingCadenceLearnCount=24
ProbeDigiLiveSequenceReplayMovingCadenceLearnPacketCount=1920
ProbeDigiLiveSequenceReplayMovingCadenceCachedUseCount=53
ProbeDigiLiveSequenceReplayMovingBadTotalCount=0
ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount=0
ProbeDigiLiveSequenceReplayMovingLastRawTotalDataBlocks=438
ProbeDigiLiveSequenceReplayMovingLastTotalDataBlocks=441
ProbeDigiLiveSequenceReplayMovingLastCadencePhase=37
ProbeDigiLiveSequenceReplayMovingLastCadenceSource=1
ProbeDigiLiveRxCadenceReady=1
ProbeDigiLiveRxCadenceBestPhase=37
ProbeDigiLiveRxCadenceBestPhaseMismatchCount=0
```

A 30-second follow-up confirms that the replay dry-run continues to run, but the
known long-run input harvest/ring starvation still returns:

```text
Captures/coreaudio-digi003-test-0.2.133-queuephase-loose-dryrun-30s.wav
repeated_frames=85631
after_1s_repeated_frames=80000
last_10s_repeated_frames=41088
last_5s_repeated_frames=25280
ProbeAudioRuntimeRingUnderrunFrames=78202
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=1726
ProbeDigiLiveSequenceReplayMovingDryRunPacketCount=138080
ProbeDigiLiveSequenceReplayMovingCadencePhaseUseCount=1726
ProbeDigiLiveSequenceReplayMovingCadenceLearnCount=425
ProbeDigiLiveSequenceReplayMovingCadenceCachedUseCount=854
ProbeDigiLiveSequenceReplayMovingBadTotalCount=0
ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount=0
```

Interpretation:

The moving replay command-pointer path and phase-aligned data-block generation
are no longer the immediate blocker in dry-run. The next risk to solve before
enabling live descriptor rewrites is the long-run input harvest starvation:
around 20-30 seconds the Core Audio ring can drain to near-empty even while the
replay dry-run is otherwise progressing.

`0.2.134` briefly disabled dry-run to perform live TX descriptor updates with the
phase-aligned moving replay path. Reject this build: even five successful live
updates were enough to silence the input capture completely after a Digi 003
power-cycle.

```text
Captures/coreaudio-digi003-test-0.2.134-moving-live-10s.wav
nonzero_frames=0
repeated_frames=440999
after_1s_repeated_frames=396899
last_5s_repeated_frames=220499
ProbeAudioRuntimeRingUnderrunFrames=319185
ProbeDigiLiveSequenceReplayMovingDryRunEnabled=0
ProbeDigiLiveSequenceReplayMovingUpdateSuccessCount=5
ProbeDigiLiveSequenceReplayMovingUpdatePacketCount=400
ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount=1015
ProbeDigiLiveSequenceReplayMovingLastSyncRet=3758097112
```

Interpretation:

Do not enable live TX descriptor rewrites again until the update window is made
hardware-pointer safe. The likely issue is not the synthetic 5/6 cadence itself
but writing descriptors too near the active IT command pointer or presenting a
transiently inconsistent descriptor/header/payload state. `0.2.135` restores
dry-run mode with the same phase diagnostics.

`0.2.135` restore test after the rejected live write build:

```text
Captures/coreaudio-digi003-test-0.2.135-dryrun-restore-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
ProbeDigiLiveSequenceReplayMovingDryRunEnabled=1
ProbeDigiLiveSequenceReplayMovingUpdateSuccessCount=0
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=708
ProbeDigiLiveSequenceReplayMovingDryRunPacketCount=56640
ProbeDigiLiveSequenceReplayMovingCadencePhaseUseCount=708
ProbeDigiLiveSequenceReplayMovingBadTotalCount=0
ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount=0
```

`0.2.136` keeps dry-run mode but raises the moving replay update lead from 512
to 4096 packets and adds a live-write guard diagnostic. The guard records the
distance from the current IT command pointer to the planned update window, rejects
windows that would wrap over the hardware pointer, and in dry-run publishes
whether a future live write would have been allowed.

```text
Captures/coreaudio-digi003-test-0.2.136-guarded-dryrun-10s.wav
after_1s_repeated_frames=0
after_2s_repeated_frames=0
last_5s_repeated_frames=0
ProbeAudioRuntimeRingUnderrunFrames=0
ProbeDigiLiveSequenceReplayMovingLeadPackets=4096
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=674
ProbeDigiLiveSequenceReplayMovingGuardEligibleCount=674
ProbeDigiLiveSequenceReplayMovingGuardRejectCount=0
ProbeDigiLiveSequenceReplayMovingGuardDryRunWouldWriteCount=674
ProbeDigiLiveSequenceReplayMovingGuardDryRunWouldRejectCount=0
ProbeDigiLiveSequenceReplayMovingLastStartDistancePackets=4096
ProbeDigiLiveSequenceReplayMovingLastEndDistancePackets=4175
ProbeDigiLiveSequenceReplayMovingLastGuardWouldWrite=1
ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount=0
```

The 30-second follow-up still shows the known long-run input harvest starvation,
but the guard remains consistently eligible and command-pointer-stable:

```text
Captures/coreaudio-digi003-test-0.2.136-guarded-dryrun-30s.wav
repeated_frames=68372
after_1s_repeated_frames=63397
last_10s_repeated_frames=39721
last_5s_repeated_frames=20517
ProbeAudioRuntimeRingUnderrunFrames=68511
ProbeDigiLiveSequenceReplayMovingDryRunSuccessCount=1790
ProbeDigiLiveSequenceReplayMovingGuardEligibleCount=1790
ProbeDigiLiveSequenceReplayMovingGuardRejectCount=0
ProbeDigiLiveSequenceReplayMovingBadCommandPtrCount=0
ProbeDigiLiveSequenceReplayMovingLastStartDistancePackets=4096
ProbeDigiLiveSequenceReplayMovingLastEndDistancePackets=4175
```

## Local Automation Notes

A narrow local sudoers rule is installed at `/etc/sudoers.d/firewire-ohci-probe` so Codex can continue DriverKit upgrade loops without repeated password prompts. It permits only:

```text
/usr/sbin/installer -pkg /Users/axelheckert/Documents/Codex/2026-05-15/files-mentioned-by-the-user-003/FireWireOHCIProbe/Packages/FireWireOHCIProbeLoader.pkg -target /
/usr/local/sbin/firewire-ohci-probe-kill-old <pid>
```

The helper validates that the PID belongs to `com.axelheckert.driver.FireWireOHCIProbe` before sending `kill -9`. The previous malformed `/etc/sudoers` line was removed, with a backup saved as `/etc/sudoers.codex-backup-20260517130552`.

The development provisioning profiles must be signed with the matching
`Apple Development: axelheckert@icloud.com (L2YWACT7FH)` identity. Signing the
same profile with `Developer ID Application: Axel Heckert (7H3ND356AV)` launches
as an AMFI failure with `No matching profile found`.

New diagnostics:

```text
ProbeAudioRuntimeCaptureActive
ProbeAudioRuntimeInputCallbackLastPreHarvestFillFrames
ProbeAudioRuntimeInputCallbackLastPostHarvestFillFrames
ProbeAudioRuntimeInputCallbackLastPostConsumeFillFrames
ProbeAudioRuntimeInputCallbackMinPreHarvestFillFrames
ProbeAudioRuntimeInputCallbackMinPostConsumeFillFrames
ProbeAudioRuntimeInputCallbackLowFillCount
ProbeAudioRuntimeRingActiveRepeatedFrames
ProbeAudioRuntimeRingActiveUnderrunFrames
ProbeAudioRuntimeRingActiveOverrunFrames
ProbeAudioRuntimeRefreshWorkerActiveCatchUpDelayCount
ProbeAudioRuntimeRefreshWorkerActiveCatchUpNoSleepCount
```

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

1. Keep `ProbeOHCIInterruptDispatchEnabled=0` until the harvest path is made cheaper.
2. Reduce RX DBC/cycle lost counts and 30-second underruns without adding harvest callers.
3. Investigate producer/consumer ring policy: long-run audio repeats match ring starvation even when the device stream remains active.
4. Add Digi "double-oh-three" playback encoding before enabling non-silent output.
5. Add MIDI/control-surface and mixer/control support after audio input stability improves.
