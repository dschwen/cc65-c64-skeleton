#!/usr/bin/env python3
"""Patch and validate a linked room-code overlay header."""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


SEGMENT = re.compile(
    r"^(BSS)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+"
)
BASE = 0x9900
MAX_BYTES = 0x0400
HEADER_BYTES = 24
ABI_VERSION = 4


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--map", dest="map_file", type=Path, required=True)
    parser.add_argument("--room", type=lambda value: int(value, 16), required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    data = bytearray(args.input.read_bytes())
    if len(data) < HEADER_BYTES or len(data) > MAX_BYTES:
        raise SystemExit(f"room code size {len(data)} is outside {HEADER_BYTES}..{MAX_BYTES}")
    if (data[:1] != b"\x4c" or data[3:4] != b"\x4c" or
            data[6:7] != b"\x4c" or data[21:22] != b"\x4c" or
            data[9:13] != bytes((0x52, 0x43, ABI_VERSION, args.room))):
        raise SystemExit("invalid room overlay header")

    bss_start = BASE + len(data)
    bss_size = 0
    for line in args.map_file.read_text(encoding="ascii").splitlines():
        match = SEGMENT.match(line)
        if match:
            bss_start = int(match.group(2), 16)
            bss_size = int(match.group(4), 16)
            break
    if bss_start < BASE + len(data) or bss_start + bss_size > BASE + MAX_BYTES:
        raise SystemExit("room BSS lies outside its overlay window")

    struct.pack_into("<HHH", data, 13, len(data), bss_start - BASE, bss_size)
    data[19:21] = b"\0\0"
    checksum = sum(data[HEADER_BYTES:]) & 0xFFFF
    struct.pack_into("<H", data, 19, checksum)
    args.output.write_bytes(data)


if __name__ == "__main__":
    main()
