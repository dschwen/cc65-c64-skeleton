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


# Splits a (PETSCII-converted) 64-byte objects.cobj record into a 35-byte
# "hot" part (PlatformObjectType: dimensions, hotspot, chars[16],
# colors[16], light - kept resident, always visible) and a 15-byte "cold"
# part (PlatformObjectTypeInfo: name[14], flags - kept in an EasyFlash
# bank, fetched only for Look/Take text). Both parts are embedded directly
# as C data (src/object_types_data.c, modules/typeinfo.c). Bytes 50-63 are
# unused padding, dropped.
OBJECT_TYPE_HOT_BYTES = 35
OBJECT_TYPE_COLD_BYTES = 15


def split_objects_hot_cold(data: bytes, path: Path) -> tuple[bytes, bytes]:
    expected = OBJECT_TYPE_BYTES * OBJECT_TYPE_COUNT
    if len(data) != expected:
        raise ValueError(f"{path}: expected {expected} bytes, got {len(data)}")
    hot = bytearray()
    cold = bytearray()
    for type_id in range(OBJECT_TYPE_COUNT):
        record = data[type_id * OBJECT_TYPE_BYTES : (type_id + 1) * OBJECT_TYPE_BYTES]
        dimensions_hotspot = record[0:2]
        name = record[2:16]
        chars = record[16:32]
        colors = record[32:48]
        flags = record[48:49]
        light = record[49:50]
        hot += dimensions_hotspot + chars + colors + light
        cold += name + flags
    assert len(hot) == OBJECT_TYPE_HOT_BYTES * OBJECT_TYPE_COUNT
    assert len(cold) == OBJECT_TYPE_COLD_BYTES * OBJECT_TYPE_COUNT
    return bytes(hot), bytes(cold)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("kind", choices=("room", "objects", "objects-split"))
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument(
        "--cold-output", type=Path,
        help="objects-split only: where to write the 15-byte-per-record "
        "cold (name+flags) blob; --output receives the 35-byte-per-record "
        "hot blob",
    )
    args = parser.parse_args()

    data = bytearray(args.input.read_bytes())
    if args.kind == "room":
        prepare_room(data, args.input)
        args.output.write_bytes(data)
    elif args.kind == "objects":
        prepare_objects(data, args.input)
        args.output.write_bytes(data)
    else:
        if args.cold_output is None:
            raise SystemExit("objects-split requires --cold-output")
        prepare_objects(data, args.input)
        hot, cold = split_objects_hot_cold(bytes(data), args.input)
        args.output.write_bytes(hot)
        args.cold_output.write_bytes(cold)


if __name__ == "__main__":
    main()
