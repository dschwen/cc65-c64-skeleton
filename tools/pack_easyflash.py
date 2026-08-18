#!/usr/bin/env python3
"""Append fixed-layout runtime room and object assets to an EasyFlash image."""

from __future__ import annotations

import argparse
from pathlib import Path

BANK_BYTES = 0x4000
ROML_BYTES = 0x2000
ROOM_BYTES = 1253
ROOMS_PER_BANK = 6
FIRST_ROOM_BANK = 2
ROOM_BANKS = 43
TYPE_BANK_0 = 45
TYPE_BANK_1 = 46
OUTPUT_BANKS = 47


def load_room(asset_dir: Path, room_id: int) -> bytes:
    path = asset_dir / f"{room_id:02X}"
    if not path.exists():
        return bytes([0xFF]) * ROOM_BYTES
    data = path.read_bytes()
    if len(data) != ROOM_BYTES:
        raise ValueError(f"{path}: expected {ROOM_BYTES} bytes, got {len(data)}")
    if data[:4] != bytes((20, 11, room_id, 2)):
        raise ValueError(f"{path}: invalid room header {data[:4].hex()}")
    return data


def build_image(base: bytes, asset_dir: Path, object_types: Path) -> bytes:
    if len(base) != 2 * BANK_BYTES:
        raise ValueError(f"bootstrap image must be 32768 bytes, got {len(base)}")
    types = object_types.read_bytes()
    if len(types) != 0x4000:
        raise ValueError(f"{object_types}: expected 16384 bytes, got {len(types)}")

    image = bytearray([0xFF]) * (OUTPUT_BANKS * BANK_BYTES)
    image[: len(base)] = base
    for room_id in range(256):
        bank = FIRST_ROOM_BANK + room_id // ROOMS_PER_BANK
        offset = (room_id % ROOMS_PER_BANK) * ROOM_BYTES
        start = bank * BANK_BYTES + offset
        image[start : start + ROOM_BYTES] = load_room(asset_dir, room_id)

    start = TYPE_BANK_0 * BANK_BYTES
    image[start : start + ROML_BYTES] = types[:ROML_BYTES]
    start = TYPE_BANK_1 * BANK_BYTES
    image[start : start + ROML_BYTES] = types[ROML_BYTES:]
    return bytes(image)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base", type=Path, required=True)
    parser.add_argument("--assets", type=Path, required=True)
    parser.add_argument("--objects", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.write_bytes(
        build_image(args.base.read_bytes(), args.assets, args.objects)
    )


if __name__ == "__main__":
    main()
