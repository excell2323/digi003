#!/usr/bin/env python3
import re
import subprocess
import sys


def read_ioreg():
    proc = subprocess.run(
        ["ioreg", "-r", "-n", "FireWireOHCIProbe", "-d", "1"],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    return proc.stdout


def parse_number(text, key, default=0):
    match = re.search(rf'"{re.escape(key)}" = ([0-9]+)', text)
    if match is None:
        return default
    return int(match.group(1))


def parse_data_words(text, key):
    data = re.search(rf'"{re.escape(key)}" = <([0-9a-fA-F\s]+)>', text)
    if data is not None:
        hex_bytes = re.sub(r"\s+", "", data.group(1))
        raw = bytes.fromhex(hex_bytes)
        return [
            int.from_bytes(raw[i : i + 4], "little")
            for i in range(0, len(raw), 4)
            if i + 4 <= len(raw)
        ]
    return []


def parse_words(text):
    words = parse_data_words(text, "ProbeControlRecentRawWordsBE")
    if words:
        return words

    indexed = {}
    for match in re.finditer(r'"ProbeControlRecentRawWord([0-9]+)BE" = ([0-9]+)', text):
        indexed[int(match.group(1))] = int(match.group(2))
    if not indexed:
        return []
    return [indexed.get(i, 0) for i in range(max(indexed) + 1)]


def bytes_for_word(word):
    return (
        (word >> 24) & 0xFF,
        (word >> 16) & 0xFF,
        (word >> 8) & 0xFF,
        word & 0xFF,
    )


def ordered_ring_items(items, index, count):
    count = min(count, len(items))
    if count == len(items):
        order = list(range(index, len(items))) + list(range(0, index))
    else:
        order = list(range(0, count))
    return [(i, items[i]) for i in order if items[i] != 0]


def midi_bytes_for_word(word):
    _marker, data0, data1, control = bytes_for_word(word)
    length = control & 0x0F
    result = []
    if length >= 1:
        result.append(data0)
    if length >= 2:
        result.append(data1)
    return (control >> 4) & 0x0F, result


def append_decoded_byte(pending_by_port, port, byte):
    pending = pending_by_port.setdefault(port, [])
    if byte & 0x80:
        pending[:] = [byte]
        return None
    if not pending:
        return None
    pending.append(byte)
    if len(pending) < 3:
        return None
    msg = pending[:3]
    del pending[:]
    return msg


def main():
    text = read_ioreg()
    message_count = parse_number(text, "ProbeControlMessageCount")
    recent_index = parse_number(text, "ProbeControlRecentIndex")
    recent_count = parse_number(text, "ProbeControlRecentCount")
    decoded_count = parse_number(text, "ProbeControlDecodedMessageCount")
    decoded_index = parse_number(text, "ProbeControlDecodedRecentIndex")
    decoded_recent_count = parse_number(text, "ProbeControlDecodedRecentCount")
    mapped_count = parse_number(text, "ProbeControlStateMappedMessageCount")
    unknown_count = parse_number(text, "ProbeControlStateUnknownMessageCount")
    last_kind = parse_number(text, "ProbeControlStateLastMappedKind")
    select1 = parse_number(text, "ProbeControlStateSelect1Pressed")
    fader1_touch = parse_number(text, "ProbeControlStateFader1Touched")
    fader1_cc = parse_number(text, "ProbeControlStateFader1ControlNumber")
    fader1_value = parse_number(text, "ProbeControlStateFader1Value")
    fader1_updates = parse_number(text, "ProbeControlStateFader1UpdateCount")
    stop = parse_number(text, "ProbeControlStateStopPressed")
    play = parse_number(text, "ProbeControlStatePlayPressed")
    feedback_messages = parse_number(text, "ProbeControlFeedbackMessageCount")
    feedback_skipped = parse_number(text, "ProbeControlFeedbackSkippedCount")
    echo_appends = parse_number(text, "ProbeControlEchoAppendCount")
    echo_transmits = parse_number(text, "ProbeControlEchoTransmitCount")
    echo_drops = parse_number(text, "ProbeControlEchoDropCount")
    words = parse_words(text)
    decoded_words = parse_data_words(text, "ProbeControlDecodedRecentMessages")
    if not words:
        print("No ProbeControl recent words found", file=sys.stderr)
        return 1

    ordered_words = ordered_ring_items(words, recent_index, recent_count)
    active_words = [word for _idx, word in ordered_words]
    print(f"message_count={message_count} recent_index={recent_index} recent_count={recent_count}")
    print(
        f"decoded_count={decoded_count} "
        f"decoded_index={decoded_index} decoded_recent_count={decoded_recent_count}"
    )
    print(
        f"state mapped={mapped_count} unknown={unknown_count} last_kind={last_kind} "
        f"select1={select1} fader1_touch={fader1_touch} "
        f"fader1_cc=0x{fader1_cc:02X} fader1_value={fader1_value} "
        f"fader1_updates={fader1_updates} stop={stop} play={play}"
    )
    print(
        f"feedback messages={feedback_messages} skipped={feedback_skipped} "
        f"echo_words_append={echo_appends} echo_words_transmit={echo_transmits} "
        f"echo_drops={echo_drops}"
    )
    print(f"active_words={len(active_words)}")

    print("words:")
    for idx, word in ordered_words:
        b0, b1, b2, b3 = bytes_for_word(word)
        print(
            f"  slot={idx:03d} raw=0x{word:08X} bytes={b0:02X} {b1:02X} {b2:02X} {b3:02X} "
            f"port={b3 >> 4:X} len={b3 & 0x0F}"
        )

    print("reconstructed_midi:")
    pending_by_port = {}
    for word in active_words:
        port, payload = midi_bytes_for_word(word)
        for byte in payload:
            msg = append_decoded_byte(pending_by_port, port, byte)
            if msg is not None:
                print(f"  port={port:X} " + " ".join(f"{byte:02X}" for byte in msg))
    for port, pending in sorted(pending_by_port.items()):
        if pending:
            print(f"  port={port:X} pending " + " ".join(f"{byte:02X}" for byte in pending))

    if decoded_words:
        print("driver_decoded_midi:")
        for _idx, message in ordered_ring_items(decoded_words, decoded_index, decoded_recent_count):
            port = (message >> 24) & 0x0F
            status = (message >> 16) & 0xFF
            data1 = (message >> 8) & 0xFF
            data2 = message & 0xFF
            print(f"  port={port:X} {status:02X} {data1:02X} {data2:02X}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
