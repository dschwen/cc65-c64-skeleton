#!/usr/bin/env python3
"""Convert editor-authored ASCII text fields to C64 PETSCII."""

from __future__ import annotations

import argparse
from pathlib import Path


# Room text is a separate same-ID resource now (assets/resources/<id>), not
# part of the room file - see PlatformRoom in src/platform.h.
ROOM_BYTES = 13 + 220 + 768
OBJECT_TYPE_BYTES = 64
OBJECT_TYPE_COUNT = 256
OBJECT_NAME_OFFSET = 2
OBJECT_NAME_BYTES = 14


def ascii_to_petscii(value: int) -> int:
    if 0x41 <= value <= 0x5A:
        return value + 0x80
    if 0x61 <= value <= 0x7A:
        return value - 0x20
    return value


def prepare_room(data: bytearray, path: Path) -> None:
    if len(data) != ROOM_BYTES:
        raise ValueError(f"{path}: expected {ROOM_BYTES} bytes, got {len(data)}")
    if data[0:2] != bytes((20, 11)) or data[3] != 3:
        raise ValueError(f"{path}: invalid room header")


def prepare_objects(data: bytearray, path: Path) -> None:
    expected = OBJECT_TYPE_BYTES * OBJECT_TYPE_COUNT
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, got {len(data)}")
    for type_id in range(OBJECT_TYPE_COUNT):
        start = type_id * OBJECT_TYPE_BYTES + OBJECT_NAME_OFFSET
        for offset in range(start, start + OBJECT_NAME_BYTES):
            data[offset] = ascii_to_petscii(data[offset])


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("room", "objects"))
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()

    data = bytearray(args.input.read_bytes())
    if args.kind == "room":
        prepare_room(data, args.input)
    else:
        prepare_objects(data, args.input)
    args.output.write_bytes(data)


if __name__ == "__main__":
    main()
