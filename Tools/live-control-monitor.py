#!/usr/bin/env python3
import argparse
import re
import subprocess
import sys
import time


CONTROL_PORT = 0xE
CHANNEL_NOTE_NAMES = {
    0x00: "SELECT",
    0x01: "SOLO",
    0x02: "MUTE",
    0x03: "FADER TOUCH",
}
TRANSPORT_NOTE_NAMES = {
    0x06: "RTZ",
    0x07: "REW",
    0x08: "FFWD",
    0x09: "STOP",
    0x0A: "PLAY",
    0x0B: "REC",
}
NAVIGATION_NOTE_NAMES = {
    0x05: "ARROW LEFT",
    0x06: "ARROW RIGHT",
    0x07: "ARROW UP",
    0x08: "ARROW DOWN",
}
MODE_VIEW_NOTE_FIRST = 0x00
MODE_VIEW_NOTE_COUNT = 20
MODE_VIEW_NOTE_NAMES = {
    0x00: "PAN",
    0x01: "SEND",
    0x02: "INSERT",
    0x03: "A/F",
    0x04: "B/G",
    0x05: "C/H",
    0x06: "D/I",
    0x07: "E/J",
    0x08: "SHIFT",
    0x09: "OPT/ALT",
    0x0A: "CTRL/WIN",
    0x0B: "COMMAND/CTRL",
    0x0C: "DEFAULT",
    0x0D: "INPUT",
    0x0E: "WRITE",
    0x0F: "READ",
    0x10: "TOUCH",
    0x11: "OFF",
    0x12: "LATCH",
    0x13: "SUSPEND",
}
ENCODER_ASSIGN_NOTE_COUNT = 8
ENCODER_ASSIGN_NOTE_NAMES = {
    0x00: "EQ",
    0x01: "DYNAMICS",
    0x02: "INSERT",
    0x03: "PAN/SEND",
    0x04: "PAGE LEFT",
    0x05: "PAGE RIGHT",
    0x06: "MASTER BYPASS",
    0x07: "ESC",
}
ABOVE_TRANSPORT_NOTE_COUNT = 32
ABOVE_TRANSPORT_NOTE_NAMES = {
    0x00: "ENTER",
    0x01: "UNDO",
    0x02: "SAVE",
    0x05: "REC ARM",
    0x06: "METER",
}
HARDWARE_MONITOR_NOTE_NAMES = {
    (0x0F, 0x00): "MIC/DI CH1",
    (0x0F, 0x01): "HPF CH1",
    (0x0F, 0x02): "MIC/DI CH2",
    (0x0F, 0x03): "HPF CH2",
    (0x0F, 0x04): "MIC/DI CH3",
    (0x0F, 0x05): "HPF CH3",
    (0x0F, 0x06): "MIC/DI CH4",
    (0x0F, 0x07): "HPF CH4",
    (0x0F, 0x0F): "AUX IN 7+8",
    (0x0F, 0x0E): "3/4 + HP2",
    (0x0F, 0x0D): "AUX IN",
    (0x0F, 0x0C): "ALT CR",
    (0x0F, 0x0B): "MONO",
    (0x0F, 0x0A): "MUTE",
    (0x0B, 0x08): "DISPLAY MODE",
}
TRANSPORT_SECTION_NOTE_NAMES = {
    (0x0D, 0x00): "FLIP",
    (0x0D, 0x01): "MASTER FADERS",
    (0x0D, 0x02): "BANK",
    (0x0D, 0x03): "NUDGE",
    (0x0D, 0x04): "ZOOM",
    (0x0E, 0x00): "PLUG-IN",
    (0x0E, 0x01): "MIX",
    (0x0E, 0x02): "EDIT",
    (0x0E, 0x03): "LOOP PLAY",
    (0x0E, 0x04): "LOOP REC",
    (0x0E, 0x05): "QUICK PUNCH",
    (0x0E, 0x0C): "MEM LOC",
    (0x0D, 0x0B): "MIDI RECALL",
    (0x0D, 0x0A): "MIDI EDIT",
    (0x0D, 0x09): "UTILITY",
    (0x0D, 0x0C): "FADER MUTE",
    (0x0D, 0x0D): "FOCUS",
}
ROTARY_ENCODER_CC_FIRST = 0x40
ROTARY_ENCODER_COUNT = 8


def read_ioreg():
    proc = subprocess.run(
        ["ioreg", "-r", "-n", "FireWireOHCIProbe", "-d", "1"],
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if proc.returncode != 0:
        return ""
    return proc.stdout


def parse_number(text, key, default=0):
    match = re.search(rf'"{re.escape(key)}" = ([0-9]+)', text)
    if match is None:
        return default
    return int(match.group(1))


def parse_data_words(text, key):
    data = re.search(rf'"{re.escape(key)}" = <([0-9a-fA-F\s]+)>', text)
    if data is None:
        return []

    hex_bytes = re.sub(r"\s+", "", data.group(1))
    raw = bytes.fromhex(hex_bytes)
    return [
        int.from_bytes(raw[i : i + 4], "little")
        for i in range(0, len(raw), 4)
        if i + 4 <= len(raw)
    ]


def ordered_ring_items(items, index, count):
    count = min(count, len(items))
    if count == len(items):
        order = list(range(index, len(items))) + list(range(0, index))
    else:
        order = list(range(0, count))
    return [(i, items[i]) for i in order if items[i] != 0]


def decode_message(message):
    return (
        (message >> 24) & 0x0F,
        (message >> 16) & 0xFF,
        (message >> 8) & 0xFF,
        message & 0xFF,
    )


def describe_message(message):
    port, status, data1, data2 = decode_message(message)
    command = status & 0xF0
    channel = status & 0x0F
    prefix = f"port={port:X} {status:02X} {data1:02X} {data2:02X}"

    if port != CONTROL_PORT:
        return f"{prefix} | MIDI/other port"

    if command in (0x80, 0x90):
        pressed = (data2 & 0x40) != 0
        group = data2 & 0x0F
        state = "press" if pressed else "release"
        if group == 0x0E and data1 in TRANSPORT_NOTE_NAMES:
            name = TRANSPORT_NOTE_NAMES[data1]
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if group == 0x0D and data1 in NAVIGATION_NOTE_NAMES:
            name = NAVIGATION_NOTE_NAMES[data1]
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if group == 0x0A and MODE_VIEW_NOTE_FIRST <= data1 < MODE_VIEW_NOTE_FIRST + MODE_VIEW_NOTE_COUNT:
            name = MODE_VIEW_NOTE_NAMES.get(data1, f"MODE/VIEW {data1:02X}")
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if group == 0x0B and data1 < ENCODER_ASSIGN_NOTE_COUNT:
            name = ENCODER_ASSIGN_NOTE_NAMES.get(data1, f"ENCODER ASSIGN {data1 + 1}")
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if group == 0x0C and data1 < ABOVE_TRANSPORT_NOTE_COUNT:
            name = ABOVE_TRANSPORT_NOTE_NAMES.get(data1, f"ABOVE TRANSPORT {data1:02X}")
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if (group, data1) in HARDWARE_MONITOR_NOTE_NAMES:
            name = HARDWARE_MONITOR_NOTE_NAMES[(group, data1)]
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if (group, data1) in TRANSPORT_SECTION_NOTE_NAMES:
            name = TRANSPORT_SECTION_NOTE_NAMES[(group, data1)]
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if group < 8 and data1 in CHANNEL_NOTE_NAMES:
            channel = (data2 & 0x07) + 1
            name = CHANNEL_NOTE_NAMES[data1]
            return f"{prefix} | CH{channel} {name} {state} value=0x{data2:02X}"
        return f"{prefix} | UNKNOWN NOTE 0x{data1:02X} {state} value=0x{data2:02X}"

    if command == 0xB0:
        if ROTARY_ENCODER_CC_FIRST <= data1 < ROTARY_ENCODER_CC_FIRST + ROTARY_ENCODER_COUNT:
            encoder = data1 - ROTARY_ENCODER_CC_FIRST + 1
            if data2 > 0x40:
                direction = "RIGHT"
                step = data2 - 0x40
            elif data2 < 0x40:
                direction = "LEFT"
                step = 0x40 - data2
            else:
                direction = "CENTER"
                step = 0
            return f"{prefix} | ENCODER {encoder} {direction} step={step} value={data2}"
        if data1 == 0x4E:
            if data2 > 0x40:
                direction = "RIGHT"
                step = data2 - 0x40
            elif data2 < 0x40:
                direction = "LEFT"
                step = 0x40 - data2
            else:
                direction = "CENTER"
                step = 0
            return f"{prefix} | JOG {direction} step={step} value={data2}"
        if data1 == 0x5E:
            return f"{prefix} | SHUTTLE value={data2}"
        if data1 <= 0x3F:
            channel = (data1 & 0x07) + 1
            return f"{prefix} | CH{channel} FADER MOVE cc=0x{data1:02X} value={data2}"
        return f"{prefix} | CONTROL CHANGE ch={channel} cc=0x{data1:02X} value={data2}"

    return f"{prefix} | UNKNOWN STATUS"


def state_summary(text):
    values = {
        "sel1": parse_number(text, "ProbeControlStateSelect1Pressed"),
        "f1touch": parse_number(text, "ProbeControlStateFader1Touched"),
        "f1cc": parse_number(text, "ProbeControlStateFader1ControlNumber"),
        "f1val": parse_number(text, "ProbeControlStateFader1Value"),
        "f1updates": parse_number(text, "ProbeControlStateFader1UpdateCount"),
        "rtz": parse_number(text, "ProbeControlStateTransportRTZPressed"),
        "rew": parse_number(text, "ProbeControlStateTransportRewindPressed"),
        "ffwd": parse_number(text, "ProbeControlStateTransportFastForwardPressed"),
        "stop": parse_number(text, "ProbeControlStateStopPressed"),
        "play": parse_number(text, "ProbeControlStatePlayPressed"),
        "rec": parse_number(text, "ProbeControlStateTransportRecordPressed"),
        "left": parse_number(text, "ProbeControlStateArrowLeftPressed"),
        "right": parse_number(text, "ProbeControlStateArrowRightPressed"),
        "up": parse_number(text, "ProbeControlStateArrowUpPressed"),
        "down": parse_number(text, "ProbeControlStateArrowDownPressed"),
        "jog_value": parse_number(text, "ProbeControlStateJogWheelValue"),
        "jog_direction": parse_number(text, "ProbeControlStateJogWheelDirection"),
        "jog_step": parse_number(text, "ProbeControlStateJogWheelStep"),
        "jog_updates": parse_number(text, "ProbeControlStateJogWheelUpdateCount"),
        "shuttle_value": parse_number(text, "ProbeControlStateShuttleValue"),
        "shuttle_updates": parse_number(text, "ProbeControlStateShuttleUpdateCount"),
        "mode_last": parse_number(text, "ProbeControlStateModeViewLastIndex", 0xFFFFFFFF),
        "mode_updates": parse_number(text, "ProbeControlStateModeViewUpdateCount"),
        "assign_last": parse_number(text, "ProbeControlStateEncoderAssignLastIndex", 0xFFFFFFFF),
        "assign_updates": parse_number(text, "ProbeControlStateEncoderAssignUpdateCount"),
        "encoder_last": parse_number(text, "ProbeControlStateRotaryEncoderLastIndex", 0xFFFFFFFF),
        "last_channel": parse_number(text, "ProbeControlStateLastMappedChannel", 0xFFFFFFFF),
        "mapped": parse_number(text, "ProbeControlStateMappedMessageCount"),
        "unknown": parse_number(text, "ProbeControlStateUnknownMessageCount"),
        "feedback": parse_number(text, "ProbeControlFeedbackMessageCount"),
        "drops": parse_number(text, "ProbeControlEchoDropCount"),
        "motor_triggers": parse_number(text, "ProbeControlMotorTestTriggerCount"),
        "motor_messages": parse_number(text, "ProbeControlMotorTestMessageCount"),
        "motor_skips": parse_number(text, "ProbeControlMotorTestSkippedCount"),
        "motor_channel": parse_number(text, "ProbeControlMotorTestLastChannel", 0xFFFFFFFF),
        "motor_target": parse_number(text, "ProbeControlMotorTestLastTarget10"),
    }
    channel_parts = []
    for channel in range(1, 9):
        select = parse_number(text, f"ProbeControlStateChannel{channel}SelectPressed")
        solo = parse_number(text, f"ProbeControlStateChannel{channel}SoloPressed")
        mute = parse_number(text, f"ProbeControlStateChannel{channel}MutePressed")
        touch = parse_number(text, f"ProbeControlStateChannel{channel}FaderTouched")
        value = parse_number(text, f"ProbeControlStateChannel{channel}FaderValue")
        updates = parse_number(text, f"ProbeControlStateChannel{channel}FaderUpdateCount")
        if select or solo or mute or touch or updates:
            channel_parts.append(
                f"ch{channel}[sel={select} solo={solo} mute={mute} touch={touch} "
                f"val={value} upd={updates}]"
            )
    last_channel = values["last_channel"]
    last_channel_text = "-" if last_channel == 0xFFFFFFFF else str(last_channel + 1)
    motor_channel = values["motor_channel"]
    motor_channel_text = "-" if motor_channel == 0xFFFFFFFF else str(motor_channel + 1)
    jog_direction_text = {0: "-", 1: "right", 2: "left"}.get(values["jog_direction"], "?")
    mode_last_text = "-" if values["mode_last"] == 0xFFFFFFFF else str(values["mode_last"] + 1)
    assign_last_text = "-" if values["assign_last"] == 0xFFFFFFFF else str(values["assign_last"] + 1)
    encoder_last_text = "-" if values["encoder_last"] == 0xFFFFFFFF else str(values["encoder_last"] + 1)
    return (
        f"state sel1={values['sel1']} f1touch={values['f1touch']} "
        f"f1cc=0x{values['f1cc']:02X} f1val={values['f1val']} "
        f"f1updates={values['f1updates']} transport="
        f"{values['rtz']}{values['rew']}{values['ffwd']}"
        f"{values['stop']}{values['play']}{values['rec']} "
        f"arrows=LRUD:{values['left']}{values['right']}{values['up']}{values['down']} "
        f"jog={jog_direction_text}:{values['jog_step']}@{values['jog_value']}/"
        f"{values['jog_updates']} shuttle={values['shuttle_value']}/"
        f"{values['shuttle_updates']} mode={mode_last_text}/{values['mode_updates']} "
        f"assign={assign_last_text}/{values['assign_updates']} "
        f"enc_last={encoder_last_text} "
        f"last_ch={last_channel_text} mapped={values['mapped']} unknown={values['unknown']} "
        f"feedback={values['feedback']} drops={values['drops']} "
        f"motor={values['motor_triggers']}/{values['motor_messages']}/"
        f"{values['motor_skips']} last_motor_ch={motor_channel_text} "
        f"target10={values['motor_target']}"
        + ("" if not channel_parts else " | " + " ".join(channel_parts))
    )


def main():
    parser = argparse.ArgumentParser(description="Live monitor for Digi 003 control messages.")
    parser.add_argument("--seconds", type=float, default=60.0)
    parser.add_argument("--poll", type=float, default=0.1)
    parser.add_argument("--include-existing", action="store_true")
    args = parser.parse_args()

    last_count = None
    start = time.monotonic()
    print(f"Monitoring Digi 003 control messages for {args.seconds:.1f}s", flush=True)

    while time.monotonic() - start < args.seconds:
        text = read_ioreg()
        if not text:
            print("FireWireOHCIProbe not found in IORegistry", file=sys.stderr, flush=True)
            time.sleep(args.poll)
            continue

        decoded_count = parse_number(text, "ProbeControlDecodedMessageCount")
        decoded_index = parse_number(text, "ProbeControlDecodedRecentIndex")
        decoded_recent_count = parse_number(text, "ProbeControlDecodedRecentCount")
        messages = parse_data_words(text, "ProbeControlDecodedRecentMessages")

        if last_count is None:
            last_count = 0 if args.include_existing else decoded_count
            print(state_summary(text), flush=True)

        delta = decoded_count - last_count
        if delta > 0 and messages:
            ordered = [message for _idx, message in ordered_ring_items(messages, decoded_index, decoded_recent_count)]
            for message in ordered[-min(delta, len(ordered)) :]:
                elapsed = time.monotonic() - start
                print(f"{elapsed:7.2f}s | {describe_message(message)}", flush=True)
            print(state_summary(text), flush=True)
            last_count = decoded_count
        elif delta < 0:
            last_count = decoded_count

        time.sleep(args.poll)

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
