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


def parse_words(text):
    data = re.search(r'"ProbeControlRecentRawWordsBE" = <([0-9a-fA-F\s]+)>', text)
    if data is not None:
        hex_bytes = re.sub(r"\s+", "", data.group(1))
        raw = bytes.fromhex(hex_bytes)
        return [
            int.from_bytes(raw[i : i + 4], "little")
            for i in range(0, len(raw), 4)
            if i + 4 <= len(raw)
        ]

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


def main():
    text = read_ioreg()
    message_count = parse_number(text, "ProbeControlMessageCount")
    recent_index = parse_number(text, "ProbeControlRecentIndex")
    recent_count = parse_number(text, "ProbeControlRecentCount")
    words = parse_words(text)
    if not words:
        print("No ProbeControl recent words found", file=sys.stderr)
        return 1

    count = min(recent_count, len(words))
    if count == len(words):
        order = list(range(recent_index, len(words))) + list(range(0, recent_index))
    else:
        order = list(range(0, count))

    active_words = [words[i] for i in order if words[i] != 0]
    print(f"message_count={message_count} recent_index={recent_index} recent_count={recent_count}")
    print(f"active_words={len(active_words)}")

    print("words:")
    for idx, word in [(i, words[i]) for i in order if words[i] != 0]:
        b0, b1, b2, b3 = bytes_for_word(word)
        print(
            f"  slot={idx:03d} raw=0x{word:08X} bytes={b0:02X} {b1:02X} {b2:02X} {b3:02X} "
            f"port={b3 >> 4:X} len={b3 & 0x0F}"
        )

    print("midi:")
    pending = []
    for word in active_words:
        _marker, data0, data1, control = bytes_for_word(word)
        length = control & 0x0F
        if length >= 1:
            pending.append(data0)
        if length >= 2:
            pending.append(data1)
        while len(pending) >= 3:
            msg = pending[:3]
            del pending[:3]
            print("  " + " ".join(f"{byte:02X}" for byte in msg))
    if pending:
        print("  pending " + " ".join(f"{byte:02X}" for byte in pending))

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
