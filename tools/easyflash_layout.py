#!/usr/bin/env python3
"""Read and validate the fixed EasyFlash module layout."""

from __future__ import annotations

import json
from dataclasses import dataclass
from pathlib import Path


BANK_BYTES = 0x4000
HALF_BYTES = 0x2000


@dataclass(frozen=True)
class ModulePlacement:
    name: str
    bank: int
    half: str
    offset: int
    kind: str
    magic: bytes | None

    @property
    def use_romh(self) -> int:
        return int(self.half == "romh")

    @property
    def image_offset(self) -> int:
        return self.bank * BANK_BYTES + self.use_romh * HALF_BYTES + self.offset

    @property
    def cpu_address(self) -> int:
        return (0xA000 if self.use_romh else 0x8000) + self.offset

    @property
    def execution_control(self) -> int:
        """Value written to the EasyFlash control register for this call."""
        if self.kind != "in_place":
            raise ValueError(f"{self.name}: overlays have no execution mode")
        return 0x07 if self.use_romh else 0x06

    @property
    def execution_cpu_map(self) -> int:
        """6510 port low bits needed to make an in-place cart window visible.

        This is deliberately separate from ``execution_control``. Although
        both values happen to be $07 for ROMH, an 8 KiB EasyFlash control
        value of $06 still needs CPU port $37. CPU port $36 disables ROML as
        well as BASIC, exposing RAM at $8000-$BFFF instead of the cartridge.
        """
        if self.kind != "in_place":
            raise ValueError(f"{self.name}: overlays have no execution map")
        return 0x07


def load_layout(path: Path) -> dict[str, ModulePlacement]:
    raw = json.loads(path.read_text(encoding="utf-8"))
    modules = raw.get("modules")
    if not isinstance(modules, dict) or not modules:
        raise ValueError(f"{path}: missing non-empty 'modules' object")

    result: dict[str, ModulePlacement] = {}
    for name, item in modules.items():
        if not isinstance(item, dict):
            raise ValueError(f"{path}: module {name!r} is not an object")
        bank = item.get("bank")
        half = item.get("half")
        offset = item.get("offset")
        kind = item.get("kind")
        magic_text = item.get("magic")
        if not isinstance(bank, int) or not 0 <= bank < 64:
            raise ValueError(f"{path}: module {name!r} has invalid bank")
        if half not in ("roml", "romh"):
            raise ValueError(f"{path}: module {name!r} has invalid half")
        if not isinstance(offset, int) or not 0 <= offset < HALF_BYTES:
            raise ValueError(f"{path}: module {name!r} has invalid offset")
        if kind not in ("overlay", "in_place"):
            raise ValueError(f"{path}: module {name!r} has invalid kind")
        if kind == "overlay":
            if not isinstance(magic_text, str) or len(magic_text) != 2:
                raise ValueError(f"{path}: overlay {name!r} needs two-byte magic")
            magic = magic_text.encode("ascii")
        else:
            if magic_text is not None:
                raise ValueError(f"{path}: in-place module {name!r} cannot have magic")
            magic = None
        result[name] = ModulePlacement(
            name=name, bank=bank, half=half, offset=offset,
            kind=kind, magic=magic,
        )
    return result
