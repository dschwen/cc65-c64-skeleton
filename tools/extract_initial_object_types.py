#!/usr/bin/env python3
"""Extract hot (resident) bytes for the first N object types.

Used only to build the tiny placeholder object-type data baked directly into
game.prg for the case where platform_init() finds no EasyFlash cartridge (see
INITIAL_OBJECT_TYPE_COUNT in src/platform.c). Every other object-type load
goes through tools/pack_easyflash.py's split instead; keep the two in sync.
"""

from __future__ import annotations

import argparse
from pathlib import Path

OBJECT_TYPE_FILE_BYTES = 64


def extract_hot(record: bytes) -> bytes:
    dimensions_hotspot = record[0:2]
    chars = record[16:32]
    colors = record[32:48]
    light = record[49:50]
    return dimensions_hotspot + chars + colors + light


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--count", type=int, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()

    data = args.input.read_bytes()
    out = bytearray()
    for i in range(args.count):
        record = data[i * OBJECT_TYPE_FILE_BYTES : (i + 1) * OBJECT_TYPE_FILE_BYTES]
        out += extract_hot(record)
    args.output.write_bytes(bytes(out))


if __name__ == "__main__":
    main()
