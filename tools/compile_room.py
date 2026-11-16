#!/usr/bin/env python3
"""Compile a room-code DSL source file (a small declarative syntax covering
the mechanical patterns real room code turns out to need) into a C source
file compiled directly into the oscar64 whole-program build, each room
living in its own EasyFlash bank (see src/room_dispatch.c and
build/rooms/room_table.h, generated from rooms/rooms.manifest).

This DSL covers the mechanical shapes real room code needs - an empty
handler, an unconditional story-flag bump, running one room-script entry
(`script KEY`), or a tile-coordinate dispatch to game_room_script_entry()
from enter_tile()/look_at()/use_at() - plus an escape hatch (a hand-written
C fragment in rooms/c/, one per room per handler) for logic the script DSL
itself cannot express. Since the script DSL can check and consume
inventory, swap or add room objects and change tiles (tools/
compile_script.py), nearly every puzzle belongs in the .script source; a
C fragment is for things like reading the turn counter or a tile's ID. Each generated room provides four externally-callable,
room-prefixed functions (roomNN_enter_room/_enter_tile/_look_at/_use_at)
matching src/room_dispatch.c's RoomEntry contract; all four (and any spliced
fragment) are placed in that room's own #pragma code()/data()/bss() bank
section via src/banks.h's BANK_ROOM_FIRST + room index, and must be
__noinline (a banked function that got inlined into resident code would
silently stop being reachable through the intended bank switch - see
memmap.h's far_call_bank() comment).

DSL syntax (see ROOM_CODE_API.md):

    enter_room: default | flag NAME increment | flag NAME set VALUE
              | script KEY | asm "rooms/c/NN_enter_room.c"
    enter_tile: <same one-line forms as enter_room>, or a dispatch block:
    enter_tile:
        at X,Y -> script KEY
        default -> none | script KEY | asm "path"
    look_at: / use_at:
        at X,Y -> script KEY
        default -> GAME_LOOK_DEFAULT | asm "path"

`asm "path"` names a C fragment (rooms/c/*.c) defining enter_tile/
enter_room/look_at_fallback/use_at_fallback, spliced in with its name
substituted to the room-prefixed one. Numbers may be #define symbols from
the --flags files (src/story.h, src/game.h and the generated object-type
header by default).
"""

from __future__ import annotations

import argparse
import os
import re
from pathlib import Path


DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S+)")

DEFAULT_FLAG_FILES = (Path("src/story.h"), Path("src/game.h"),
                      Path("build/object_types.h"))

FRAGMENT_NAMES = ("enter_tile", "enter_room", "look_at_fallback", "use_at_fallback")


def load_symbols(paths: list[Path]) -> dict[str, int]:
    symbols: dict[str, int] = {}
    for path in paths:
        if not path.exists():
            continue
        for line in path.read_text(encoding="utf-8").splitlines():
            match = DEFINE_RE.match(line)
            if not match:
                continue
            name, raw_value = match.groups()
            text = raw_value.rstrip("uUlL")
            try:
                symbols[name] = int(text, 0)
            except ValueError:
                continue
    return symbols


def resolve(token: str, symbols: dict[str, int], line_no: int) -> int:
    try:
        return int(token, 0)
    except ValueError:
        pass
    if token not in symbols:
        raise SystemExit(
            f"line {line_no}: unknown symbol {token!r} (not a number and "
            "not defined in the flags file(s))"
        )
    return symbols[token]


class AtEntry:
    __slots__ = ("x", "y", "key")

    def __init__(self, x: int, y: int, key: int) -> None:
        self.x = x
        self.y = y
        self.key = key


class DispatchHandler:
    __slots__ = ("entries", "default_value", "default_fragment_path",
                 "default_script_key", "default_none")

    def __init__(self) -> None:
        self.entries: list[AtEntry] = []
        self.default_value: int | None = None
        self.default_fragment_path: str | None = None
        self.default_script_key: int | None = None
        self.default_none = False


class SimpleHandler:
    __slots__ = ("kind", "flag_index", "flag_op", "flag_value", "fragment_path",
                 "script_key")

    def __init__(self) -> None:
        self.kind = "default"  # "default" | "flag" | "fragment" | "script"
        self.flag_index = 0
        self.flag_op = "increment"  # "increment" | "set"
        self.flag_value = 0
        self.fragment_path = ""
        self.script_key = 0


def parse(text: str, symbols: dict[str, int]) -> dict[str, object]:
    lines = text.splitlines()
    handlers: dict[str, object] = {}
    i = 0
    while i < len(lines):
        raw = lines[i]
        line_no = i + 1
        line = raw.split("#", 1)[0].strip()
        i += 1
        if not line:
            continue

        if line in ("look_at:", "use_at:", "enter_tile:"):
            name = line[:-1]
            handler = DispatchHandler()
            while i < len(lines):
                inner_raw = lines[i]
                inner_line = inner_raw.split("#", 1)[0].strip()
                if not inner_line:
                    i += 1
                    continue
                if not inner_raw[:1].isspace():
                    break
                i += 1
                inner_no = i
                if inner_line.startswith("at "):
                    m = re.match(
                        r"at\s+(\S+?)\s*,\s*(\S+)\s*->\s*script\s+(\S+)$",
                        inner_line,
                    )
                    if not m:
                        raise SystemExit(
                            f"line {inner_no}: malformed 'at' line: {inner_line!r}"
                        )
                    x = resolve(m.group(1), symbols, inner_no)
                    y = resolve(m.group(2), symbols, inner_no)
                    key = resolve(m.group(3), symbols, inner_no)
                    handler.entries.append(AtEntry(x, y, key))
                elif inner_line.startswith("default"):
                    am = re.match(r'default\s*->\s*asm\s+"([^"]+)"$', inner_line)
                    sm = re.match(r"default\s*->\s*script\s+(\S+)$", inner_line)
                    if am:
                        handler.default_fragment_path = am.group(1)
                    elif sm:
                        handler.default_script_key = resolve(sm.group(1), symbols, inner_no)
                    elif name == "enter_tile" and re.match(r"default\s*->\s*none$", inner_line):
                        handler.default_none = True
                    else:
                        m = re.match(r"default\s*->\s*(\S+)$", inner_line)
                        if not m or name == "enter_tile":
                            raise SystemExit(
                                f"line {inner_no}: malformed 'default' line: {inner_line!r}"
                            )
                        handler.default_value = resolve(m.group(1), symbols, inner_no)
                else:
                    raise SystemExit(
                        f"line {inner_no}: expected 'at X,Y -> script KEY' or "
                        f"'default -> ...', got {inner_line!r}"
                    )
            if (handler.default_value is None and handler.default_fragment_path is None
                    and handler.default_script_key is None and not handler.default_none):
                raise SystemExit(
                    f"{name}: missing mandatory 'default -> ...' line "
                    "(a return value, 'script KEY', 'asm \"path\"', or for "
                    "enter_tile 'none')"
                )
            handlers[name] = handler
            continue

        m = re.match(r"(enter_room|enter_tile):\s*(.+)$", line)
        if m:
            name, rest = m.group(1), m.group(2).strip()
            simple = SimpleHandler()
            if rest == "default":
                simple.kind = "default"
            elif rest.startswith("flag "):
                fm = re.match(r"flag\s+(\S+)\s+(increment|set)(?:\s+(\S+))?$", rest)
                if not fm:
                    raise SystemExit(f"line {line_no}: malformed flag statement: {rest!r}")
                simple.kind = "flag"
                simple.flag_index = resolve(fm.group(1), symbols, line_no)
                simple.flag_op = fm.group(2)
                if simple.flag_op == "set":
                    if fm.group(3) is None:
                        raise SystemExit(f"line {line_no}: 'flag NAME set' needs a VALUE")
                    simple.flag_value = resolve(fm.group(3), symbols, line_no)
            elif rest.startswith("asm "):
                am = re.match(r'asm\s+"([^"]+)"$', rest)
                if not am:
                    raise SystemExit(f"line {line_no}: malformed asm statement: {rest!r}")
                simple.kind = "fragment"
                simple.fragment_path = am.group(1)
            elif rest.startswith("script "):
                sm = re.match(r"script\s+(\S+)$", rest)
                if not sm:
                    raise SystemExit(f"line {line_no}: malformed script statement: {rest!r}")
                simple.kind = "script"
                simple.script_key = resolve(sm.group(1), symbols, line_no)
            else:
                raise SystemExit(f"line {line_no}: unrecognized {name} statement: {rest!r}")
            handlers[name] = simple
            continue

        raise SystemExit(f"line {line_no}: unrecognized statement: {line!r}")

    for required in ("enter_room", "enter_tile", "look_at", "use_at"):
        if required not in handlers:
            raise SystemExit(f"missing required section: {required}")
    return handlers


def splice_fragment(path_str: str, prefix: str, out: list[str]) -> None:
    """Reads a rooms/c/*.c fragment (a full function definition using one of
    FRAGMENT_NAMES) and appends it with its function name replaced by
    `prefix` + that name, so it becomes a uniquely-named symbol in the
    whole-program build instead of colliding with every other room's
    identically-named fragment."""
    fragment_path = Path(path_str)
    if not fragment_path.exists():
        raise SystemExit(f"fragment not found: {path_str}")
    text = fragment_path.read_text(encoding="utf-8")
    renamed = False
    for name in FRAGMENT_NAMES:
        # Rename the function and mark it __noinline: every function placed
        # in a bank's #pragma code() section must be, or oscar64's
        # whole-program optimizer can inline it into resident code and
        # silently defeat the bank switch (see memmap.h's far_call_bank()
        # comment). Matches "void enter_tile(void) {" / "uint8_t
        # look_at_fallback(uint8_t tile_x, uint8_t tile_y) {" - the return
        # type token immediately before the function name.
        pattern = re.compile(
            rf"^((?:void|uint8_t)\s+){name}(\s*\()", re.MULTILINE
        )
        new_text, count = pattern.subn(rf"__noinline \1{prefix}{name}\2", text)
        if count:
            text = new_text
            renamed = True
            break
    if not renamed:
        raise SystemExit(
            f"{path_str}: no recognized function name found "
            f"(expected one of {FRAGMENT_NAMES})"
        )
    out.append(f"/* spliced from {path_str} */")
    out.append(text.rstrip("\n"))
    out.append("")


def gen_simple(name: str, room_prefix: str, handler: SimpleHandler, out: list[str]) -> None:
    fn_name = f"{room_prefix}{name}"
    if handler.kind == "fragment":
        splice_fragment(handler.fragment_path, room_prefix, out)
        return
    out.append(f"__noinline void {fn_name}(void) {{")
    if handler.kind == "flag":
        if handler.flag_op == "increment":
            out.append(f"    ++game_state.flags[{handler.flag_index}];")
        else:
            out.append(f"    game_state.flags[{handler.flag_index}] = {handler.flag_value};")
    elif handler.kind == "script":
        out.append(f"    (void)game_room_script_entry({handler.script_key});")
    out.append("}")
    out.append("")


def gen_dispatch(
    name: str, room_prefix: str, handler: DispatchHandler, out: list[str],
    symbols: dict[str, int],
) -> None:
    fn_name = f"{room_prefix}{name}"

    if name == "enter_tile":
        # The player's own tile is the dispatch key; a fragment default is
        # a whole replacement enter_tile() that runs when no `at` matched,
        # so it is spliced under a private name and called from here.
        if handler.default_fragment_path:
            splice_fragment(handler.default_fragment_path, room_prefix + "unmatched_", out)
        out.append(f"__noinline void {fn_name}(void) {{")
        out.append("    uint8_t tile_x = game_state.player_x >> 1;")
        out.append("    uint8_t tile_y = game_state.player_y >> 1;")
        for entry in handler.entries:
            out.append(f"    if (tile_x == {entry.x} && tile_y == {entry.y}) {{")
            out.append(f"        (void)game_room_script_entry({entry.key});")
            out.append("        return;")
            out.append("    }")
        if handler.default_fragment_path:
            out.append(f"    {room_prefix}unmatched_enter_tile();")
        elif handler.default_script_key is not None:
            out.append(f"    (void)game_room_script_entry({handler.default_script_key});")
        out.append("}")
        out.append("")
        return

    handled_name = "GAME_LOOK_HANDLED" if name == "look_at" else "GAME_USE_HANDLED"
    default_call = f"{room_prefix}{name}_fallback(tile_x, tile_y)"

    if handler.default_fragment_path:
        splice_fragment(handler.default_fragment_path, room_prefix, out)

    out.append(f"__noinline uint8_t {fn_name}(uint8_t tile_x, uint8_t tile_y) {{")
    for entry in handler.entries:
        out.append(f"    if (tile_x == {entry.x} && tile_y == {entry.y}) {{")
        out.append(f"        (void)game_room_script_entry({entry.key});")
        out.append(f"        return {handled_name};")
        out.append("    }")
    if handler.default_fragment_path:
        out.append(f"    return {default_call};")
    elif handler.default_script_key is not None:
        out.append(f"    (void)game_room_script_entry({handler.default_script_key});")
        out.append(f"    return {handled_name};")
    else:
        out.append(f"    return {handler.default_value};")
    out.append("}")
    out.append("")


def generate(room_id: int, handlers: dict[str, object], symbols: dict[str, int],
             room_data_path: Path, room_data_bytes: int) -> str:
    room_prefix = f"room{room_id:02x}_"
    bank_region = f"room{room_id:02x}"
    out: list[str] = [
        "/* Generated by tools/compile_room.py - do not edit by hand. */",
        "#include <stdint.h>",
        '#include "game.h"',
        '#include "banks.h"',
        '#include "story.h"',
        '#include "object_types.h"',
        "",
        f'#pragma section( {bank_region}data, 0 )',
        f'#pragma section( {bank_region}code, 0 )',
        # data listed before code so the embedded room data below lands at
        # this bank's byte 0 - platform_room_data_fetch() (src/room_
        # dispatch.c) reads it with a plain offset-0 bank copy, the same
        # convention rooms/env/*.c's EnvTable header uses.
        f'#pragma region( {bank_region}bank, 0x8000, 0xc000, , '
        f'BANK_ROOM_FIRST + {room_id}, {{ {bank_region}data, {bank_region}code }} )',
        f"#pragma data({bank_region}data)",
        "",
        f"/* Raw PlatformRoom bytes (tools/prepare_c64_assets.py room), the",
        f" * same 1001-byte format the asset editor authors - see",
        f" * PLATFORM_ROOM_FILE_BYTES in platform.h. */",
        f"const uint8_t {room_prefix}data[{room_data_bytes}] = {{",
        f'#embed {room_data_bytes} "{room_data_path.as_posix()}"',
        "};",
        f"#pragma reference({room_prefix}data)",
        "",
        f"#pragma code({bank_region}code)",
        "",
    ]
    if isinstance(handlers["enter_tile"], DispatchHandler):
        gen_dispatch("enter_tile", room_prefix, handlers["enter_tile"], out, symbols)
    else:
        gen_simple("enter_tile", room_prefix, handlers["enter_tile"], out)  # type: ignore[arg-type]
    gen_simple("enter_room", room_prefix, handlers["enter_room"], out)  # type: ignore[arg-type]
    gen_dispatch("look_at", room_prefix, handlers["look_at"], out, symbols)  # type: ignore[arg-type]
    gen_dispatch("use_at", room_prefix, handlers["use_at"], out, symbols)  # type: ignore[arg-type]
    out.append("#pragma code(code)")
    out.append("#pragma data(data)")
    out.append("")
    return "\n".join(out) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile a room-code DSL source file into oscar64 C."
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--room-id", type=lambda s: int(s, 0), required=True)
    parser.add_argument(
        "--room-data", type=Path, required=True,
        help="prepared 1001-byte PlatformRoom file (tools/prepare_c64_assets.py "
        "room), embedded into this room's own bank",
    )
    parser.add_argument(
        "--flags",
        type=Path,
        action="append",
        default=None,
        help="file(s) to read #define NAME value symbols from "
        "(default: src/story.h and src/game.h)",
    )
    args = parser.parse_args()

    flag_files = args.flags if args.flags else list(DEFAULT_FLAG_FILES)
    symbols = load_symbols(flag_files)
    handlers = parse(args.input.read_text(encoding="utf-8"), symbols)
    room_data_bytes = args.room_data.stat().st_size
    # #embed resolves relative paths against the including file's own
    # directory (same convention as #include) - compute the path relative
    # to --output's directory, not the current working directory, so the
    # generated file works regardless of where oscar64 is invoked from.
    room_data_relative = Path(os.path.relpath(args.room_data, start=args.output.parent))
    output = generate(args.room_id, handlers, symbols, room_data_relative, room_data_bytes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")
    print(f"{args.input} -> {args.output} ({len(output)} chars)")


if __name__ == "__main__":
    main()
