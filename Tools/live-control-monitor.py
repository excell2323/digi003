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
    0x09: "STOP",
    0x0A: "PLAY",
}


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
        state = "press" if pressed else "release"
        if data1 in TRANSPORT_NOTE_NAMES:
            name = TRANSPORT_NOTE_NAMES[data1]
            return f"{prefix} | {name} {state} value=0x{data2:02X}"
        if data1 in CHANNEL_NOTE_NAMES:
            channel = (data2 & 0x07) + 1
            name = CHANNEL_NOTE_NAMES[data1]
            return f"{prefix} | CH{channel} {name} {state} value=0x{data2:02X}"
        return f"{prefix} | UNKNOWN NOTE 0x{data1:02X} {state} value=0x{data2:02X}"

    if command == 0xB0:
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
        "stop": parse_number(text, "ProbeControlStateStopPressed"),
        "play": parse_number(text, "ProbeControlStatePlayPressed"),
        "last_channel": parse_number(text, "ProbeControlStateLastMappedChannel", 0xFFFFFFFF),
        "mapped": parse_number(text, "ProbeControlStateMappedMessageCount"),
        "unknown": parse_number(text, "ProbeControlStateUnknownMessageCount"),
        "feedback": parse_number(text, "ProbeControlFeedbackMessageCount"),
        "drops": parse_number(text, "ProbeControlEchoDropCount"),
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
    return (
        f"state sel1={values['sel1']} f1touch={values['f1touch']} "
        f"f1cc=0x{values['f1cc']:02X} f1val={values['f1val']} "
        f"f1updates={values['f1updates']} stop={values['stop']} play={values['play']} "
        f"last_ch={last_channel_text} mapped={values['mapped']} unknown={values['unknown']} "
        f"feedback={values['feedback']} drops={values['drops']}"
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
