#!/usr/bin/env python3
"""Verify fixed modules at their declared locations in a packed EF image."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from easyflash_layout import HALF_BYTES, load_layout


SEGMENT = re.compile(
    r"^(ENTRY)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+"
)


def overlay_payload(path: Path, magic: bytes) -> bytes:
    raw = path.read_bytes()
    if len(raw) < 18 or int.from_bytes(raw[:2], "little") != 0xB000:
        raise ValueError(f"{path}: invalid overlay load address or size")
    payload = raw[2:]
    if payload[0] != 0x4C or payload[3:5] != magic:
        raise ValueError(f"{path}: invalid {magic.decode('ascii')} header")
    return payload


def entry_address(map_path: Path) -> int:
    for line in map_path.read_text(encoding="ascii").splitlines():
        match = SEGMENT.match(line)
        if match:
            return int(match.group(2), 16)
    raise ValueError(f"{map_path}: no ENTRY segment")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--image", type=Path, required=True)
    parser.add_argument("--inventory-map", type=Path, required=True)
    parser.add_argument("--look-helpers-map", type=Path, required=True)
    parser.add_argument("--room-helpers-map", type=Path, required=True)
    parser.add_argument("--script-map", type=Path, required=True)
    parser.add_argument("--saveload-map", type=Path, required=True)
    parser.add_argument("--saveload-save-map", type=Path, required=True)
    parser.add_argument("--typeinfo-map", type=Path, required=True)
    for name in (
        "inventory", "saveload", "saveload-save", "room-helpers",
        "script", "look-helpers", "typeinfo",
    ):
        parser.add_argument(f"--{name}", type=Path, required=True)
    args = parser.parse_args()
    modules = load_layout(args.layout)
    image = args.image.read_bytes()
    paths = {
        "inventory": args.inventory,
        "saveload": args.saveload,
        "saveload_save": args.saveload_save,
        "room_helpers": args.room_helpers,
        "script": args.script,
        "look_helpers": args.look_helpers,
        "typeinfo": args.typeinfo,
    }

    required = set(paths)
    if set(modules) != required:
        missing = sorted(required - set(modules))
        extra = sorted(set(modules) - required)
        raise SystemExit(f"layout module mismatch: missing={missing}, extra={extra}")

    occupied: list[tuple[int, int, str]] = []
    for name, path in paths.items():
        placement = modules[name]
        if placement.kind == "overlay":
            assert placement.magic is not None
            payload = overlay_payload(path, placement.magic)
        else:
            payload = path.read_bytes()
        if placement.offset + len(payload) > HALF_BYTES:
            raise SystemExit(f"{name}: payload exceeds declared cartridge half")
        start = placement.image_offset
        end = start + len(payload)
        if image[start:end] != payload:
            raise SystemExit(
                f"{name}: packed bytes do not match bank {placement.bank} "
                f"{placement.half.upper()} offset ${placement.offset:04X}"
            )
        occupied.append((start, end, name))

    for index, first in enumerate(sorted(occupied)):
        for second in sorted(occupied)[index + 1:]:
            if second[0] >= first[1]:
                break
            raise SystemExit(f"fixed modules overlap: {first[2]} and {second[2]}")

    map_paths = {
        "inventory": args.inventory_map,
        "look_helpers": args.look_helpers_map,
        "room_helpers": args.room_helpers_map,
        "script": args.script_map,
        "saveload": args.saveload_map,
        "saveload_save": args.saveload_save_map,
        "typeinfo": args.typeinfo_map,
    }
    in_place = {name for name, placement in modules.items()
                if placement.kind == "in_place"}
    if set(map_paths) != in_place:
        missing = sorted(in_place - set(map_paths))
        extra = sorted(set(map_paths) - in_place)
        raise SystemExit(
            f"in-place module map mismatch: missing={missing}, extra={extra}")
    for name, map_path in map_paths.items():
        linked_entry = entry_address(map_path)
        expected_entry = modules[name].cpu_address
        if linked_entry != expected_entry:
            raise SystemExit(
                f"{name}: linked ENTRY ${linked_entry:04X} does not match "
                f"layout CPU address ${expected_entry:04X}"
            )

    print(f"validated {len(modules)} fixed EasyFlash module placements")


if __name__ == "__main__":
    main()
