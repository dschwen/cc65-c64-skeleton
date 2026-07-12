#!/usr/bin/env python3
"""Check that a PRG contains every linked byte at its declared CPU address."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

SEGMENT = re.compile(
    r"^([A-Z][A-Z0-9_]*)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+"
)
LOAD_ADDRESS = 0x0801
LAST_FILE_ADDRESS = 0x5FFF


def linked_last_address(map_text: str) -> int:
    in_segments = False
    last = LOAD_ADDRESS - 1
    for line in map_text.splitlines():
        if line == "Segment list:":
            in_segments = True
            continue
        if not in_segments:
            continue
        match = SEGMENT.match(line)
        if not match:
            continue
        start = int(match.group(2), 16)
        end = int(match.group(3), 16)
        if LOAD_ADDRESS <= start <= LAST_FILE_ADDRESS:
            last = max(last, end)
    if last < LOAD_ADDRESS:
        raise ValueError("no loadable PRG segments found in linker map")
    return last


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--prg", type=Path, required=True)
    parser.add_argument("--map", dest="map_file", type=Path, required=True)
    args = parser.parse_args()

    last = linked_last_address(args.map_file.read_text(encoding="ascii"))
    expected = 2 + (last - LOAD_ADDRESS + 1)
    actual = args.prg.stat().st_size
    if actual != expected:
        raise SystemExit(
            f"{args.prg}: {actual} bytes, expected {expected}; "
            "a linker memory area likely omitted required fill bytes"
        )


if __name__ == "__main__":
    main()
