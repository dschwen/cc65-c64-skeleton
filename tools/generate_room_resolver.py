#!/usr/bin/env python3
"""Resolve room-overlay imports against the resident game's VICE labels."""

from __future__ import annotations

import argparse
import re
import subprocess
from pathlib import Path


LABEL = re.compile(r"^al\s+([0-9A-Fa-f]{6})\s+\.(\S+)$")
NAME = re.compile(r'^\s+Name:\s+"([^"]+)"$')


def labels(path: Path) -> dict[str, int]:
    result: dict[str, int] = {}
    for line in path.read_text(encoding="ascii").splitlines():
        match = LABEL.match(line)
        if match:
            result[match.group(2)] = int(match.group(1), 16)
    return result


def object_names(path: Path, mode: str) -> set[str]:
    output = subprocess.check_output(
        ["od65", f"--dump-{mode}", str(path)], text=True
    )
    return {match.group(1) for line in output.splitlines()
            if (match := NAME.match(line))}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--labels", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("objects", nargs="+", type=Path)
    args = parser.parse_args()

    resident = labels(args.labels)
    imported: set[str] = set()
    exported: set[str] = set()
    for path in args.objects:
        imported |= object_names(path, "imports")
        exported |= object_names(path, "exports")

    unresolved = sorted(imported - exported)
    missing = [name for name in unresolved if name not in resident]
    if missing:
        raise SystemExit("room overlay imports unavailable in resident game: " +
                         ", ".join(missing))

    lines = ["; Generated from build/game.lbl. Do not edit.", ".setcpu \"6502\"", ""]
    for name in unresolved:
        address = resident[name]
        directive = ".exportzp" if address < 0x100 else ".export"
        lines.append(f"{directive} {name}")
        lines.append(f"{name} = ${address:04x}")
    lines.append("")
    args.output.write_text("\n".join(lines), encoding="ascii")


if __name__ == "__main__":
    main()
