#!/usr/bin/env python3
"""Patch and validate an independently linked $A4E9-window overlay."""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path


SEGMENT = re.compile(
    r"^(BSS)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+"
)
LOAD_ADDRESS = 0xA4E9
MAX_BYTES = 0x1017
HEADER_BYTES = 16
ABI_VERSION = 1
DEFAULT_MAGIC = "IU"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Patch and validate a $A4E9-window loaded overlay "
                     "(inventory/story, save/load, ...)."
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--map", dest="map_file", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--magic", default=DEFAULT_MAGIC,
                        help=f"2-character overlay magic (default {DEFAULT_MAGIC!r})")
    args = parser.parse_args()
    if len(args.magic) != 2:
        raise SystemExit("--magic must be exactly 2 characters")
    magic = args.magic.encode("ascii")

    raw = bytearray(args.input.read_bytes())
    if len(raw) < 2 or int.from_bytes(raw[:2], "little") != LOAD_ADDRESS:
        raise SystemExit("overlay has an invalid PRG load address")
    data = raw[2:]
    if len(data) < HEADER_BYTES or len(data) > MAX_BYTES:
        raise SystemExit(
            f"overlay size {len(data)} is outside "
            f"{HEADER_BYTES}..{MAX_BYTES}"
        )
    if (data[0] != 0x4C or data[3:6] != magic + bytes((ABI_VERSION,))):
        raise SystemExit("invalid overlay header")

    bss_start = LOAD_ADDRESS + len(data)
    bss_size = 0
    for line in args.map_file.read_text(encoding="ascii").splitlines():
        match = SEGMENT.match(line)
        if match:
            bss_start = int(match.group(2), 16)
            bss_size = int(match.group(4), 16)
            break
    if (bss_start < LOAD_ADDRESS + len(data) or
            bss_start + bss_size > LOAD_ADDRESS + MAX_BYTES):
        raise SystemExit("overlay BSS lies outside its overlay window")

    struct.pack_into("<HHH", data, 6, len(data),
                     bss_start - LOAD_ADDRESS, bss_size)
    data[12:14] = b"\0\0"
    struct.pack_into("<H", data, 12, sum(data[HEADER_BYTES:]) & 0xFFFF)
    args.output.write_bytes(raw[:2] + data)


if __name__ == "__main__":
    main()
