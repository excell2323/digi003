# Digi 003 Control Surface Map

This document separates the physical Digi 003 front-panel layout from the
slot-0 MIDI/control bytes observed on the FireWire stream.

## Physical Blocks

The current photo gives these working blocks:

1. Channel strips 1..8
   - top encoder and LED ring per channel
   - channel strip buttons
   - touch/motor fader

2. Left mode/view block
   - buttons to the left of the faders
   - likely channel view / insert / send / pan / automation selection controls

3. Top encoder assignment block
   - buttons around the top-right rotary controls
   - likely assignment, page, and parameter selection controls

4. Right center control block
   - automation, edit/window, navigation, and utility-style button groups

5. Transport and navigation block
   - RTZ, REW, FFWD, STOP, PLAY, REC
   - arrow left/right/up/down
   - jog wheel
   - spring-loaded shuttle encoder/ring

6. Bottom soft-key block
   - row of white soft/function keys beneath the jog area

7. Analog monitor/preamp controls
   - physical analog gain/monitor controls may not emit slot-0 control bytes
   - buttons with LEDs still need probing

## Confirmed Stream Map

Channel-strip notes use data2 bit 6 as the press flag and data2 low bits as
the strip index:

```text
90 00 40+n / 90 00 00+n = channel n SELECT press/release
90 01 40+n / 90 01 00+n = channel n SOLO press/release
90 02 40+n / 90 02 00+n = channel n MUTE press/release
90 03 40+n / 90 03 00+n = channel n FADER TOUCH press/release
```

Fader movement and motor targets are control-change messages with the strip
index in the low three bits of the CC number:

```text
B0 cc value
channel_index = cc & 0x07
motor target10:
  coarse = target10 >> 3
  cc     = ((target10 & 0x07) << 3) | channel_index
```

Transport group uses data2 low nibble `0xe`:

```text
90 06 4e/0e = RTZ press/release
90 07 4e/0e = REW press/release
90 08 4e/0e = FFWD press/release
90 09 4e/0e = STOP press/release
90 0a 4e/0e = PLAY press/release
90 0b 4e/0e = REC press/release
```

Navigation group uses data2 low nibble `0xd`:

```text
90 05 4d/0d = Arrow Left press/release
90 06 4d/0d = Arrow Right press/release
90 07 4d/0d = Arrow Up press/release
90 08 4d/0d = Arrow Down press/release
```

Jog and shuttle:

```text
B0 4e 41 / B0 4e 3f = jog wheel relative ticks
B0 5e xx             = spring-loaded shuttle raw value 0..15
```

Jog and shuttle CCs are intentionally not echoed back to the console.

Additional block probes:

```text
90 00..13 4a/0a = Mode/View and Insert/Send Position block
90 00 4a/0a     = PAN
90 01 4a/0a     = SEND
90 02 4a/0a     = INSERT
90 03 4a/0a     = A/F
90 04 4a/0a     = B/G
90 05 4a/0a     = C/H
90 06 4a/0a     = D/I
90 07 4a/0a     = E/J
90 08 4a/0a     = SHIFT
90 09 4a/0a     = OPT/ALT
90 0a 4a/0a     = CTRL/WIN
90 0b 4a/0a     = COMMAND/CTRL
90 0c 4a/0a     = DEFAULT
90 0d 4a/0a     = INPUT
90 0e 4a/0a     = WRITE
90 0f 4a/0a     = READ
90 10 4a/0a     = TOUCH
90 11 4a/0a     = OFF
90 12 4a/0a     = LATCH
90 13 4a/0a     = SUSPEND
90 00..07 4b/0b = 8-button Encoder Assignment block
90 00 4b/0b     = EQ
90 01 4b/0b     = DYNAMICS
90 02 4b/0b     = INSERT
90 03 4b/0b     = PAN/SEND
90 04 4b/0b     = PAGE LEFT
90 05 4b/0b     = PAGE RIGHT
90 06 4b/0b     = MASTER BYPASS
90 07 4b/0b     = ESC
B0 40..47 41     = rotary encoder 1..8 clockwise ticks
B0 40..47 3f     = rotary encoder 1..8 counter-clockwise ticks
```

Original driver behavior note:
- The four modifier keys (`SHIFT`, `OPT/ALT`, `CTRL/WIN`, `COMMAND/CTRL`) behave like real keyboard modifiers globally, while also being visible in the Digi 003 control stream.
- Our current driver maps and echoes them as Digi control messages. A later keyboard bridge should translate their press/release state to macOS modifier down/up events for original-driver parity.

The remaining printed labels for the Mode/View and Encoder Assignment buttons
are still being confirmed from the front-panel photo and live two-press tests.

## Next Probe Order

Probe the remaining blocks left-to-right, top-to-bottom, with enough pause
between presses to keep the stream readable:

1. left mode/view block
2. top encoder assignment block
3. right center control block
4. bottom soft-key block
5. each top encoder clockwise/counter-clockwise and press, if the encoder also
   clicks
6. LED feedback for every mapped button
