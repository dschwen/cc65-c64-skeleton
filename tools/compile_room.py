#!/usr/bin/env python3
"""Compile a room-code DSL source file (a small declarative syntax covering
the mechanical patterns real room code turns out to need) into 6502 assembly
(.s), for assembling with cl65/ca65 exactly like a hand-written .s file.

This exists because room code (rooms/XX.c, compiled by cc65 - see
ROOM_CODE_API.md) turns out to be almost entirely one of three mechanical
shapes across every room in this project: an empty handler, an unconditional
story-flag bump on tile entry, or a tile-coordinate dispatch to
game_room_script_entry() from look_at()/use_at(). This DSL covers exactly
those three shapes plus an escape hatch (a hand-written .s fragment) for
genuinely custom logic (e.g. a room's enter_room() poking VIC-II registers
directly for a weather effect), and compiles straight to the same body a
room's cc65-compiled object file already exports (_enter_tile, _look_at,
_enter_room, _use_at) - rooms/room_header.s, the resolver
(tools/generate_room_resolver.py), the linker config (cfg/room_overlay.cfg),
and the finalizer (tools/finalize_room_code.py) are all unchanged; they only
ever cared about those four exported symbols and the final 24-byte header,
never about how the room's own code was produced.

This does NOT solve a size problem - compiled room overlays are nowhere near
their 1024-byte ceiling today (the largest, room 00, uses under 20% of it).
The point is authoring simplicity: a room's tile-dispatch/flag-bump logic is
one declarative line each instead of hand-written C, and every room's
dispatch pattern looks textually identical instead of hand-copied.

DSL syntax:

    enter_room: default
    enter_tile: flag STORY_STATE_ROOM_00_TILE_ENTRY_COUNT increment

    look_at:
        at 18,3 -> script STORY_ROOM00_LOOK_TREE
        default -> GAME_LOOK_DEFAULT

    use_at:
        at 18,3 -> script STORY_ROOM00_USE_TREE
        default -> GAME_USE_DEFAULT

`enter_room`/`enter_tile` take one line: `default` (empty handler, just
`rts`), `flag NAME increment` (`++game_state.flags[NAME]`), `flag NAME set
VALUE` (`game_state.flags[NAME] = VALUE`), or `asm "path"` (splices in a
hand-written .s fragment exporting `_enter_room`/`_enter_tile` itself,
verbatim, for logic no DSL statement covers - see rooms/asm/ for examples).

`look_at`/`use_at` each start an indented block: zero or more `at X,Y ->
script KEY` lines (tile-coordinate dispatch to game_room_script_entry(KEY),
returning GAME_LOOK_HANDLED/GAME_USE_HANDLED on a match), followed by exactly
one mandatory `default -> RETURN_CONST` line (what to return when nothing
matched - normally GAME_LOOK_DEFAULT/GAME_USE_DEFAULT, matching the C
handlers' own fallback). `#` starts a comment to end of line; blank lines are
ignored.

X, Y, KEY, and RETURN_CONST may be decimal/hex literals or symbols resolved
against #define NAME VALUE lines in --flags (default src/story.h/src/game.h
combined), the same convention tools/compile_script.py already uses.
"""

from __future__ import annotations

import argparse
import re
from pathlib import Path


DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S+)")

# GameState.flags[]'s byte offset within game_state (src/game.h) - turn(4) +
# day(2) + hour(1) + minute(1) + current_room(1) + player_type(1) +
# player_x(1) + player_y(1) + health(1) + maximum_health(1) + mana(1) +
# maximum_mana(1) + pending_transition(1) + pending_room(1) + pending_x(1) +
# pending_y(1) + inventory(32*2) = 84. Re-check this constant if GameState's
# field order/size in src/game.h ever changes - nothing here derives it
# automatically.
GAME_STATE_FLAGS_OFFSET = 84

# GAME_LOOK_DEFAULT/HANDLED and GAME_USE_DEFAULT/HANDLED are #defined in
# src/game.h, not src/story.h - load both by default so `default ->
# GAME_LOOK_DEFAULT` resolves without extra flags.
DEFAULT_FLAG_FILES = (Path("src/story.h"), Path("src/game.h"))


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
    """look_at/use_at: a list of `at X,Y -> script KEY` entries plus the
    mandatory trailing default return value."""

    __slots__ = ("entries", "default_value")

    def __init__(self) -> None:
        self.entries: list[AtEntry] = []
        self.default_value: int | None = None


class SimpleHandler:
    """enter_room/enter_tile: one of "default", ("flag", name, op, value),
    or ("asm", path)."""

    __slots__ = ("kind", "flag_index", "flag_op", "flag_value", "asm_path")

    def __init__(self) -> None:
        self.kind = "default"  # "default" | "flag" | "asm"
        self.flag_index = 0
        self.flag_op = "increment"  # "increment" | "set"
        self.flag_value = 0
        self.asm_path = ""


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

        if line in ("look_at:", "use_at:"):
            name = line[:-1]
            handler = DispatchHandler()
            while i < len(lines):
                inner_raw = lines[i]
                inner_line = inner_raw.split("#", 1)[0].strip()
                if not inner_line:
                    i += 1
                    continue
                if not inner_raw[:1].isspace():
                    break  # next top-level statement
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
                    m = re.match(r"default\s*->\s*(\S+)$", inner_line)
                    if not m:
                        raise SystemExit(
                            f"line {inner_no}: malformed 'default' line: {inner_line!r}"
                        )
                    handler.default_value = resolve(m.group(1), symbols, inner_no)
                else:
                    raise SystemExit(
                        f"line {inner_no}: expected 'at X,Y -> script KEY' or "
                        f"'default -> VALUE', got {inner_line!r}"
                    )
            if handler.default_value is None:
                raise SystemExit(
                    f"{name}: missing mandatory 'default -> VALUE' line"
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
                simple.kind = "asm"
                simple.asm_path = am.group(1)
            else:
                raise SystemExit(f"line {line_no}: unrecognized {name} statement: {rest!r}")
            handlers[name] = simple
            continue

        raise SystemExit(f"line {line_no}: unrecognized statement: {line!r}")

    for required in ("enter_room", "enter_tile", "look_at", "use_at"):
        if required not in handlers:
            raise SystemExit(f"missing required section: {required}")
    return handlers


def gen_simple(name: str, handler: SimpleHandler, out: list[str]) -> None:
    if handler.kind == "asm":
        # The escape hatch: splice the named .s fragment's own text in
        # verbatim (it supplies its own .export _NAME, label, and body) -
        # this keeps every room's code in one object file, so no change is
        # needed to the linker inputs that assemble/link a room overlay.
        fragment_path = Path(handler.asm_path)
        if not fragment_path.exists():
            raise SystemExit(f"{name}: asm fragment not found: {handler.asm_path}")
        out.append(f"; {name}: spliced in verbatim from {handler.asm_path}")
        out.append(fragment_path.read_text(encoding="utf-8").rstrip("\n"))
        out.append("")
        return
    out.append(f".export _{name}")
    out.append('.segment "CODE"')
    out.append(f"_{name}:")
    if handler.kind == "default":
        out.append("    rts")
    elif handler.kind == "flag":
        addr = f"_game_state+{GAME_STATE_FLAGS_OFFSET + handler.flag_index}"
        if handler.flag_op == "increment":
            out.append(f"    inc {addr}")
        else:
            out.append(f"    lda #{handler.flag_value}")
            out.append(f"    sta {addr}")
        out.append("    rts")
    out.append("")


def gen_dispatch(
    name: str, handler: DispatchHandler, out: list[str], symbols: dict[str, int]
) -> None:
    # Confirmed against cc65's own compiled output for this exact ABI
    # (rooms/00.c's look_at/use_at): fastcall passes tile_y in A; `jsr
    # pusha` pushes it, so after that call the stack holds (offset 1)
    # tile_x - pushed by the caller before the call - and (offset 0)
    # tile_y - just pushed here. Both handlers pop exactly those 2 bytes
    # via `jmp incsp2` on every return path.
    handled_name = "GAME_LOOK_HANDLED" if name == "look_at" else "GAME_USE_HANDLED"
    ret_handled = resolve(handled_name, symbols, 0)
    out.append(f".export _{name}")
    out.append('.segment "CODE"')
    out.append(f"_{name}:")
    out.append("    jsr pusha")
    for n, entry in enumerate(handler.entries):
        out.append(f"    ldy #1")
        out.append(f"    lda (sp),y")
        out.append(f"    cmp #{entry.x}")
        out.append(f"    bne @miss{n}")
        out.append(f"    ldy #0")
        out.append(f"    lda (sp),y")
        out.append(f"    cmp #{entry.y}")
        out.append(f"    bne @miss{n}")
        out.append(f"    lda #{entry.key}")
        out.append("    jsr _game_room_script_entry")
        out.append("    ldx #0")
        out.append(f"    lda #{ret_handled}")
        out.append("    jmp incsp2")
        out.append(f"@miss{n}:")
    out.append("    ldx #0")
    out.append(f"    lda #{handler.default_value}")
    out.append("    jmp incsp2")
    out.append("")


def generate(handlers: dict[str, object], symbols: dict[str, int]) -> str:
    out: list[str] = [
        "; Generated by tools/compile_room.py - do not edit by hand.",
        ".setcpu \"6502\"",
        "",
        ".import _game_room_script_entry",
        ".import _game_state",
        ".import incsp2",
        ".importzp sp",
        ".import pusha",
        "",
    ]
    gen_simple("enter_tile", handlers["enter_tile"], out)  # type: ignore[arg-type]
    gen_simple("enter_room", handlers["enter_room"], out)  # type: ignore[arg-type]
    gen_dispatch("look_at", handlers["look_at"], out, symbols)  # type: ignore[arg-type]
    gen_dispatch("use_at", handlers["use_at"], out, symbols)  # type: ignore[arg-type]
    return "\n".join(out) + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile a room-code DSL source file into 6502 assembly."
    )
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
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
    output = generate(handlers, symbols)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(output, encoding="utf-8")
    print(f"{args.input} -> {args.output} ({len(output)} chars)")


if __name__ == "__main__":
    main()
