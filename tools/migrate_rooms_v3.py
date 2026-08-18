#!/usr/bin/env python3
"""Migrate hexadecimal room assets from format 2 to format 3."""

from __future__ import annotations

import argparse
from pathlib import Path


ROOM_V2_BYTES = 1253
ROOM_V3_BYTES = 1257
ROOM_V2_HEADER_BYTES = 9
ROOM_V3_HEADER_BYTES = 13


def migrate(data: bytes, room_id: int) -> bytes:
    if len(data) == ROOM_V3_BYTES:
        if data[:4] != bytes((20, 11, room_id, 3)):
            raise ValueError(f"invalid v3 header {data[:4].hex()}")
        return data
    if len(data) != ROOM_V2_BYTES:
        raise ValueError(
            f"expected {ROOM_V2_BYTES} or {ROOM_V3_BYTES} bytes, got {len(data)}"
        )
    if data[:4] != bytes((20, 11, room_id, 2)):
        raise ValueError(f"invalid v2 header {data[:4].hex()}")
    header = bytearray(data[:ROOM_V2_HEADER_BYTES])
    header[3] = 3
    header.extend((0, 0, 0, 0))
    return bytes(header) + data[ROOM_V2_HEADER_BYTES:]


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("asset_dir", type=Path)
    args = parser.parse_args()

    for path in sorted(args.asset_dir.iterdir()):
        if len(path.name) != 2:
            continue
        try:
            room_id = int(path.name, 16)
        except ValueError:
            continue
        room = migrate(path.read_bytes(), room_id)
        temporary = path.with_name(path.name + ".tmp")
        temporary.write_bytes(room)
        temporary.replace(path)


if __name__ == "__main__":
    main()
