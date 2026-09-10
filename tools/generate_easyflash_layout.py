#!/usr/bin/env python3
"""Generate C and ca65 constants from cfg/easyflash_layout.json."""

from __future__ import annotations

import argparse
from pathlib import Path

from easyflash_layout import load_layout


def macro_name(name: str) -> str:
    return "EF_LAYOUT_" + name.upper()


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--layout", type=Path, required=True)
    parser.add_argument("--header", type=Path, required=True)
    parser.add_argument("--include", type=Path, required=True)
    args = parser.parse_args()
    modules = load_layout(args.layout)

    c_lines = [
        "/* Generated from cfg/easyflash_layout.json. Do not edit. */",
        "#ifndef EASYFLASH_LAYOUT_H",
        "#define EASYFLASH_LAYOUT_H",
        "",
    ]
    asm_lines = [
        "; Generated from cfg/easyflash_layout.json. Do not edit.",
        "",
    ]
    for placement in modules.values():
        prefix = macro_name(placement.name)
        c_lines.extend((
            f"#define {prefix}_BANK {placement.bank}u",
            f"#define {prefix}_USE_ROMH {placement.use_romh}u",
            f"#define {prefix}_OFFSET {placement.offset}u",
            f"#define {prefix}_ENTRY 0x{placement.cpu_address:04X}u",
        ))
        asm_lines.extend((
            f"{prefix}_BANK = {placement.bank}",
            f"{prefix}_USE_ROMH = {placement.use_romh}",
            f"{prefix}_OFFSET = ${placement.offset:04X}",
            f"{prefix}_ENTRY = ${placement.cpu_address:04X}",
        ))
        if placement.kind == "in_place":
            c_lines.extend((
                f"#define {prefix}_CPU_MAP 0x{placement.execution_cpu_map:02X}u",
                f"#define {prefix}_CONTROL 0x{placement.execution_control:02X}u",
            ))
            asm_lines.extend((
                f"{prefix}_CPU_MAP = ${placement.execution_cpu_map:02X}",
                f"{prefix}_CONTROL = ${placement.execution_control:02X}",
            ))
        if placement.magic is not None:
            c_lines.extend((
                f"#define {prefix}_MAGIC_0 0x{placement.magic[0]:02X}u",
                f"#define {prefix}_MAGIC_1 0x{placement.magic[1]:02X}u",
            ))
            asm_lines.extend((
                f"{prefix}_MAGIC_0 = ${placement.magic[0]:02X}",
                f"{prefix}_MAGIC_1 = ${placement.magic[1]:02X}",
            ))
        c_lines.append("")
        asm_lines.append("")
    c_lines.extend(("#endif", ""))
    args.header.write_text("\n".join(c_lines), encoding="ascii")
    args.include.write_text("\n".join(asm_lines), encoding="ascii")


if __name__ == "__main__":
    main()
