#!/usr/bin/env python3
"""Enforce the memory contract for code executed from an EasyFlash window."""

from __future__ import annotations

import argparse
import re
from pathlib import Path

from easyflash_layout import load_layout


SEGMENT = re.compile(
    r"^([A-Z][A-Z0-9_]*)\s+([0-9A-F]{6})\s+([0-9A-F]{6})\s+"
    r"([0-9A-F]{6})\s+"
)
RESOLVED_SYMBOL = re.compile(
    r"^([A-Za-z_][A-Za-z0-9_]*)\s*=\s*\$([0-9A-Fa-f]{4,6})\s*$"
)

# In-place ROM code cannot initialize or own mutable storage. ZEROPAGE is
# rejected too: allocating private ZP in an independently linked module would
# alias the resident program's ZP allocation even though ld65 accepted it.
READ_ONLY_SEGMENTS = {"ENTRY", "CODE", "RODATA"}


def parse_segments(path: Path) -> list[tuple[str, int, int, int]]:
    result: list[tuple[str, int, int, int]] = []
    in_segments = False
    for line in path.read_text(encoding="ascii").splitlines():
        if line == "Segment list:":
            in_segments = True
            continue
        if not in_segments:
            continue
        match = SEGMENT.match(line)
        if match:
            result.append(
                (
                    match.group(1),
                    int(match.group(2), 16),
                    int(match.group(3), 16),
                    int(match.group(4), 16),
                )
            )
        elif result and line.startswith("Exports list"):
            break
    if not result:
        raise ValueError(f"{path}: no linked segments")
    return result


def parse_resolved_symbols(path: Path) -> list[tuple[str, int]]:
    result: list[tuple[str, int]] = []
    for line in path.read_text(encoding="ascii").splitlines():
        match = RESOLVED_SYMBOL.match(line)
        if match:
            result.append((match.group(1), int(match.group(2), 16)))
    return result


def resident_visible(address: int, half: str) -> bool:
    # Both ROML and ROMH execution require CPU map $37. In 8 KiB mode ROML is
    # visible but BASIC ROM occupies $A000-$BFFF; in 16 KiB mode cartridge
    # ROMH occupies it. Either way the underlying upper RAM is unreadable.
    # Hardware I/O and KERNAL entry points remain outside the resident ABI
    # even though the CPU can reach them.
    if 0x0000 <= address <= 0x7FFF or 0xC000 <= address <= 0xCFFF:
        return True
    return False


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--module", required=True)
    parser.add_argument("--map", dest="map_file", type=Path, required=True)
    parser.add_argument("--resolver", type=Path, required=True)
    args = parser.parse_args()

    modules = load_layout(args.layout)
    try:
        placement = modules[args.module]
    except KeyError as exc:
        raise SystemExit(f"layout has no module {args.module!r}") from exc
    if placement.kind != "in_place":
        raise SystemExit(f"{args.module}: expected an in_place layout entry")

    window_start = placement.cpu_address
    window_end = (0xA000 if placement.half == "roml" else 0xC000) - 1
    errors: list[str] = []

    for name, start, end, size in parse_segments(args.map_file):
        if size == 0:
            continue
        if name not in READ_ONLY_SEGMENTS:
            errors.append(
                f"segment {name} has {size} byte(s); in-place modules may only "
                "contain ENTRY/CODE/RODATA"
            )
        if start < window_start or end > window_end:
            errors.append(
                f"segment {name} ${start:04X}-${end:04X} lies outside declared "
                f"window ${window_start:04X}-${window_end:04X}"
            )

    for name, address in parse_resolved_symbols(args.resolver):
        if not resident_visible(address, placement.half):
            errors.append(
                f"import {name} resolves to hidden/non-resident ${address:04X}"
            )

    if errors:
        raise SystemExit(f"{args.module}: banked-module contract failed:\n  " +
                         "\n  ".join(errors))

    mode = "8 KiB ROML" if placement.half == "roml" else "16 KiB ROMH"
    print(
        f"validated {args.module} banked ABI: {mode}, read-only "
        f"${window_start:04X}-${window_end:04X}, "
        f"{len(parse_resolved_symbols(args.resolver))} imports"
    )


if __name__ == "__main__":
    main()
