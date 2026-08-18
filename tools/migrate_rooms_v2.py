#!/usr/bin/env python3
"""Migrate hexadecimal room assets to the fixed room-v2 header."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOM_V1_BYTES = 1248
ROOM_V2_BYTES = 1253
ROOM_HEADER_V1_BYTES = 4
ROOM_HEADER_V2_BYTES = 9
ROOM_DIRECTIONS = {
    "north": (0x01, 5),
    "east": (0x02, 6),
    "west": (0x04, 7),
    "south": (0x08, 8),
}


def parse_link(value: str) -> tuple[int, str, int]:
    try:
        source_text, direction, target_text = value.split(":", 2)
        source = int(source_text, 16)
        target = int(target_text, 16)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("link must be SRC:DIRECTION:DST") from exc
    direction = direction.lower()
    if not 0 <= source <= 0xFF or not 0 <= target <= 0xFF:
        raise argparse.ArgumentTypeError("room IDs must be 00 through FF")
    if direction not in ROOM_DIRECTIONS:
        raise argparse.ArgumentTypeError("direction must be north, east, west, or south")
    return source, direction, target


def migrate(data: bytes, room_id: int) -> bytearray:
    if len(data) == ROOM_V1_BYTES:
        if data[:4] != bytes((20, 11, room_id, 1)):
            raise ValueError(f"invalid v1 header {data[:4].hex()}")
        return bytearray((20, 11, room_id, 2, 0, 0, 0, 0, 0)) + data[ROOM_HEADER_V1_BYTES:]
    if len(data) == ROOM_V2_BYTES:
        if data[:4] != bytes((20, 11, room_id, 2)):
            raise ValueError(f"invalid v2 header {data[:4].hex()}")
        return bytearray(data)
    raise ValueError(f"expected {ROOM_V1_BYTES} or {ROOM_V2_BYTES} bytes, got {len(data)}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("asset_dir", type=Path)
    parser.add_argument("--link", action="append", default=[], type=parse_link,
                        metavar="SRC:DIRECTION:DST")
    args = parser.parse_args()

    links: dict[int, list[tuple[str, int]]] = {}
    for source, direction, target in args.link:
        links.setdefault(source, []).append((direction, target))

    for path in sorted(args.asset_dir.iterdir()):
        if len(path.name) != 2:
            continue
        try:
            room_id = int(path.name, 16)
        except ValueError:
            continue
        room = migrate(path.read_bytes(), room_id)
        for direction, target in links.get(room_id, []):
            bit, offset = ROOM_DIRECTIONS[direction]
            room[4] |= bit
            room[offset] = target
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_bytes(room)
        temporary.replace(path)


if __name__ == "__main__":
    main()
