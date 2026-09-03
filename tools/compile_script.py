#!/usr/bin/env python3
"""Compile a cutscene/conversation/room script (a small text DSL) into the
binary bytecode format the script-interpreter overlay (modules/script.c)
executes.

One format serves cutscenes (a single linear script), conversations (a
keyword-triggered set of topic responses), and rooms (a set of entries keyed
by a small number room code picks, e.g. a tile index) - a conversation topic
or room entry is just a script body reached by matching player input or a
numeric key, respectively, instead of played straight through. A room's
compiled resource replaces what used to be a plain room-text pool: it uses
the same resource ID as the room itself (0-239), giving every Look/Use/
room-entry/tile-entry/exit-description hook full script logic (flags,
give_object, branching) instead of a static string.

Resource layout (all multi-byte fields little-endian; all offsets are
absolute, counted from byte 0 of the resource):

    [0]      format version (currently 1)
    [1]      kind: 0 = script (cutscene), 1 = conversation, 2 = room
    [2]      topic_count/entry_count (0 for kind 0)
    if kind == 1:
        topic_count * {
            [4]  keyword, uppercased ASCII, NUL-padded to 4 bytes
                 (a keyword of "*" is the fallback topic: matched when no
                 other keyword does)
            [2]  entry_offset - absolute offset of this topic's bytecode
        }
    if kind == 2:
        entry_count * {
            [1]  key - the numeric key room code passes to
                 game_room_script_entry(); no fallback/wildcard entry -
                 an unmatched key is a normal, cheap no-op
            [2]  entry_offset - absolute offset of this entry's bytecode
        }
    bytecode - kind 0's single body starts right after the 3-byte header;
        kind 1's topic bodies and kind 2's entry bodies are each
        concatenated in declaration order, addressed by their table's
        entry_offset
    string table - immediately after the bytecode; each string is a
        NUL-terminated byte sequence; TEXT's operand is the absolute offset
        of its first byte

Bytecode (one opcode byte, then its operands, repeated until END):

    0x00 END                                            no operands
    0x01 TEXT           str_offset:2
    0x02 PORTRAIT_SHOW   id:1  side:1        (side 0=left, 1=right)
    0x03 PORTRAIT_HIDE                                   no operands
    0x04 SET_FLAG        index:1  value:1
    0x05 CHECK_FLAG       index:1  cmp:1  value:1  true_len:2  false_len:2
                         followed by true_len bytes (executed if
                         game_state.flags[index] cmp value), then false_len
                         bytes (executed otherwise) - both are themselves
                         complete statement sequences and may nest further
                         CHECK_FLAGs. cmp: 0 == , 1 != , 2 < , 3 > , 4 <= ,
                         5 >= , 6 & (bit test: value is a bit index 0-7,
                         true when that bit of flags[index] is set - packs
                         multiple yes/no facts into one flag byte; there's
                         no separate "bit clear" cmp, use the false branch)
    0x06 WAIT_KEY                                        no operands
    0x07 ROOM_TRANSITION  room:1  x:1  y:1
    0x08 SOUND            id:1
    0x09 GIVE_OBJECT       type:1  quantity:1

A block (the top level of a script, a conversation topic, or either branch
of a CHECK_FLAG) always ends with END; there is no separate "return"
opcode. What "the block ended" means - resume the game, or show the topic
prompt again - is the interpreter's job, not the format's.

DSL syntax:

    script intro {
        text "Welcome."
        portrait_show 3 left
        check_flag MET_WIZARD == 0 {
            text "A wizard appears!"
            set_flag MET_WIZARD 1
            give_object 5 1
        } else {
            text "The wizard nods at you."
        }
        wait_key
        portrait_hide
    }

    conversation wizard {
        topic "wiza" {
            text "I am the wizard of this land."
        }
        topic "*" {
            text "The wizard doesn't answer that."
        }
    }

    room 00 {
        entry 0 {
            text "A warm light shines from the window."
        }
        entry 4 {
            check_flag FOUND_KEY == 0 {
                text "You find a rusty key in the grass."
                give_object 9 1
                set_flag FOUND_KEY 1
            }
        }
        entry 5 {
            check_flag QUEST_FLAGS & 2 {
                text "The seal on the door is broken."
            } else {
                text "The door is sealed shut."
            }
        }
    }

A room's entry keys are whatever numbering convention its own C code (see
rooms/XX.c and game_room_script_entry() in src/script_runtime.c) picks -
e.g. tile index for tile-entry hooks, a reserved constant range for exit
descriptions. This compiler doesn't assign or interpret them; it just
stores whatever key literal/symbol you write.

Flag names (MET_WIZARD above) resolve against #define NAME value lines in
--flags (default src/story.h); portrait/room/sound/object-type IDs may be a
bare decimal/hex literal or any symbol from the same file. give_object's
type is an object-type ID (see the asset editor's Object mode); quantity 0
is legal but a no-op.
"""

from __future__ import annotations

import argparse
import re
import struct
from pathlib import Path

FORMAT_VERSION = 1
KIND_SCRIPT = 0
KIND_CONVERSATION = 1
KIND_ROOM = 2

OP_END = 0x00
OP_TEXT = 0x01
OP_PORTRAIT_SHOW = 0x02
OP_PORTRAIT_HIDE = 0x03
OP_SET_FLAG = 0x04
OP_CHECK_FLAG = 0x05
OP_WAIT_KEY = 0x06
OP_ROOM_TRANSITION = 0x07
OP_SOUND = 0x08
OP_GIVE_OBJECT = 0x09

CMP_OPS = {"==": 0, "!=": 1, "<": 2, ">": 3, "<=": 4, ">=": 5, "&": 6}


# --------------------------------------------------------------------------
# Tokenizer
# --------------------------------------------------------------------------

TOKEN_RE = re.compile(
    r"""
      (?P<ws>\s+)
    | (?P<linecomment>//[^\n]*)
    | (?P<blockcomment>/\*.*?\*/)
    | (?P<string>"(?:\\.|[^"\\])*")
    | (?P<number>0[xX][0-9a-fA-F]+|[0-9]+)
    | (?P<cmp><=|>=|==|!=|<|>|&)
    | (?P<lbrace>\{)
    | (?P<rbrace>\})
    | (?P<semi>;)
    | (?P<ident>[A-Za-z_][A-Za-z0-9_]*)
    """,
    re.VERBOSE | re.DOTALL,
)

STRING_ESCAPES = {"n": "\n", "t": "\t", '"': '"', "\\": "\\"}


class Token:
    __slots__ = ("kind", "value", "line")

    def __init__(self, kind: str, value: str, line: int) -> None:
        self.kind = kind
        self.value = value
        self.line = line

    def __repr__(self) -> str:
        return f"Token({self.kind!r}, {self.value!r})"


def tokenize(text: str) -> list[Token]:
    tokens: list[Token] = []
    line = 1
    pos = 0
    while pos < len(text):
        match = TOKEN_RE.match(text, pos)
        if not match:
            raise SystemExit(
                f"line {line}: unexpected character {text[pos]!r}"
            )
        kind = match.lastgroup
        value = match.group()
        if kind in ("ws", "linecomment", "blockcomment"):
            line += value.count("\n")
            pos = match.end()
            continue
        if kind == "string":
            value = decode_string(value, line)
        tokens.append(Token(kind, value, line))
        line += value.count("\n") if kind == "string" else 0
        pos = match.end()
    tokens.append(Token("eof", "", line))
    return tokens


def decode_string(raw: str, line: int) -> str:
    body = raw[1:-1]
    out: list[str] = []
    i = 0
    while i < len(body):
        ch = body[i]
        if ch == "\\":
            if i + 1 >= len(body):
                raise SystemExit(f"line {line}: dangling escape in string")
            esc = body[i + 1]
            if esc not in STRING_ESCAPES:
                raise SystemExit(f"line {line}: unknown escape \\{esc}")
            out.append(STRING_ESCAPES[esc])
            i += 2
        else:
            out.append(ch)
            i += 1
    return "".join(out)


# --------------------------------------------------------------------------
# AST
# --------------------------------------------------------------------------

class Stmt:
    pass


class TextStmt(Stmt):
    def __init__(self, text: str) -> None:
        self.text = text


class PortraitShowStmt(Stmt):
    def __init__(self, portrait_id: int, side: int) -> None:
        self.portrait_id = portrait_id
        self.side = side


class PortraitHideStmt(Stmt):
    pass


class SetFlagStmt(Stmt):
    def __init__(self, index: int, value: int) -> None:
        self.index = index
        self.value = value


class CheckFlagStmt(Stmt):
    def __init__(self, index: int, cmp_op: int, value: int,
                 true_body: list[Stmt], false_body: list[Stmt]) -> None:
        self.index = index
        self.cmp_op = cmp_op
        self.value = value
        self.true_body = true_body
        self.false_body = false_body


class WaitKeyStmt(Stmt):
    pass


class RoomTransitionStmt(Stmt):
    def __init__(self, room: int, x: int, y: int) -> None:
        self.room = room
        self.x = x
        self.y = y


class SoundStmt(Stmt):
    def __init__(self, sound_id: int) -> None:
        self.sound_id = sound_id


class GiveObjectStmt(Stmt):
    def __init__(self, object_type: int, quantity: int) -> None:
        self.object_type = object_type
        self.quantity = quantity


class Topic:
    def __init__(self, keyword: str, body: list[Stmt]) -> None:
        self.keyword = keyword
        self.body = body


class ScriptDecl:
    def __init__(self, name: str, body: list[Stmt]) -> None:
        self.name = name
        self.body = body


class ConversationDecl:
    def __init__(self, name: str, topics: list[Topic]) -> None:
        self.name = name
        self.topics = topics


class RoomEntry:
    def __init__(self, key: int, body: list[Stmt]) -> None:
        self.key = key
        self.body = body


class RoomDecl:
    def __init__(self, name: str, entries: list[RoomEntry]) -> None:
        self.name = name
        self.entries = entries


# --------------------------------------------------------------------------
# Parser
# --------------------------------------------------------------------------

class Parser:
    def __init__(self, tokens: list[Token], symbols: dict[str, int]) -> None:
        self.tokens = tokens
        self.pos = 0
        self.symbols = symbols

    def peek(self) -> Token:
        return self.tokens[self.pos]

    def advance(self) -> Token:
        tok = self.tokens[self.pos]
        self.pos += 1
        return tok

    def expect(self, kind: str, value: str | None = None) -> Token:
        tok = self.peek()
        if tok.kind != kind or (value is not None and tok.value != value):
            expected = value if value is not None else kind
            raise SystemExit(
                f"line {tok.line}: expected {expected!r}, got {tok.value!r}"
            )
        return self.advance()

    def at_keyword(self, word: str) -> bool:
        tok = self.peek()
        return tok.kind == "ident" and tok.value == word

    def skip_semi(self) -> None:
        while self.peek().kind == "semi":
            self.advance()

    def parse_program(self) -> list[ScriptDecl | ConversationDecl | RoomDecl]:
        decls: list[ScriptDecl | ConversationDecl | RoomDecl] = []
        self.skip_semi()
        while self.peek().kind != "eof":
            if self.at_keyword("script"):
                decls.append(self.parse_script())
            elif self.at_keyword("conversation"):
                decls.append(self.parse_conversation())
            elif self.at_keyword("room"):
                decls.append(self.parse_room())
            else:
                tok = self.peek()
                raise SystemExit(
                    f"line {tok.line}: expected 'script', 'conversation', "
                    f"or 'room', got {tok.value!r}"
                )
            self.skip_semi()
        return decls

    def parse_script(self) -> ScriptDecl:
        self.expect("ident", "script")
        name = self.expect("ident").value
        body = self.parse_block()
        return ScriptDecl(name, body)

    def parse_conversation(self) -> ConversationDecl:
        self.expect("ident", "conversation")
        name = self.expect("ident").value
        self.expect("lbrace")
        self.skip_semi()
        topics: list[Topic] = []
        while not (self.peek().kind == "rbrace"):
            self.expect("ident", "topic")
            keyword_tok = self.expect("string")
            # Matching only ever looks at the first 4 characters (of both
            # the topic keyword and whatever the player typed), so the
            # designer can write a full word here for readability; only the
            # prefix is stored and compared.
            keyword = keyword_tok.value
            body = self.parse_block()
            topics.append(Topic(keyword, body))
            self.skip_semi()
        self.expect("rbrace")
        if not topics:
            raise SystemExit(f"conversation {name!r} has no topics")
        return ConversationDecl(name, topics)

    def parse_room(self) -> RoomDecl:
        self.expect("ident", "room")
        # A room's name is conventionally its (hex or decimal) room ID,
        # matching the resource file it's compiled to - accept a number
        # token here too, not just an identifier.
        name_tok = self.peek()
        if name_tok.kind not in ("ident", "number"):
            raise SystemExit(f"line {name_tok.line}: expected a room name/ID, "
                             f"got {name_tok.value!r}")
        name = name_tok.value
        self.advance()
        self.expect("lbrace")
        self.skip_semi()
        entries: list[RoomEntry] = []
        while not (self.peek().kind == "rbrace"):
            self.expect("ident", "entry")
            key = self.resolve_number()
            body = self.parse_block()
            entries.append(RoomEntry(key, body))
            self.skip_semi()
        self.expect("rbrace")
        if not entries:
            raise SystemExit(f"room {name!r} has no entries")
        return RoomDecl(name, entries)

    def parse_block(self) -> list[Stmt]:
        self.expect("lbrace")
        self.skip_semi()
        stmts: list[Stmt] = []
        while self.peek().kind != "rbrace":
            stmts.append(self.parse_stmt())
            self.skip_semi()
        self.expect("rbrace")
        return stmts

    def resolve_number(self) -> int:
        tok = self.peek()
        if tok.kind == "number":
            self.advance()
            return int(tok.value, 0)
        if tok.kind == "ident":
            self.advance()
            if tok.value not in self.symbols:
                raise SystemExit(
                    f"line {tok.line}: unknown symbol {tok.value!r} "
                    "(not a number and not defined in the flags file)"
                )
            return self.symbols[tok.value]
        raise SystemExit(f"line {tok.line}: expected a number or symbol")

    def parse_stmt(self) -> Stmt:
        tok = self.peek()
        if tok.kind != "ident":
            raise SystemExit(f"line {tok.line}: expected a statement")
        if tok.value == "text":
            self.advance()
            text = self.expect("string").value
            return TextStmt(text)
        if tok.value == "portrait_show":
            self.advance()
            portrait_id = self.resolve_number()
            side_tok = self.peek()
            if side_tok.kind == "ident" and side_tok.value in ("left", "right"):
                self.advance()
                side = 0 if side_tok.value == "left" else 1
            else:
                side = self.resolve_number()
            return PortraitShowStmt(portrait_id, side)
        if tok.value == "portrait_hide":
            self.advance()
            return PortraitHideStmt()
        if tok.value == "set_flag":
            self.advance()
            index = self.resolve_number()
            value = self.resolve_number()
            return SetFlagStmt(index, value)
        if tok.value == "check_flag":
            return self.parse_check_flag()
        if tok.value == "wait_key":
            self.advance()
            return WaitKeyStmt()
        if tok.value == "room_transition":
            self.advance()
            room = self.resolve_number()
            x = self.resolve_number()
            y = self.resolve_number()
            return RoomTransitionStmt(room, x, y)
        if tok.value == "sound":
            self.advance()
            return SoundStmt(self.resolve_number())
        if tok.value == "give_object":
            self.advance()
            object_type = self.resolve_number()
            quantity = self.resolve_number()
            return GiveObjectStmt(object_type, quantity)
        raise SystemExit(f"line {tok.line}: unknown statement {tok.value!r}")

    def parse_check_flag(self) -> CheckFlagStmt:
        self.expect("ident", "check_flag")
        index = self.resolve_number()
        cmp_tok = self.expect("cmp")
        if cmp_tok.value not in CMP_OPS:
            raise SystemExit(f"line {cmp_tok.line}: bad comparison {cmp_tok.value!r}")
        cmp_op = CMP_OPS[cmp_tok.value]
        value = self.resolve_number()
        true_body = self.parse_block()
        false_body: list[Stmt] = []
        if self.at_keyword("else"):
            self.advance()
            false_body = self.parse_block()
        return CheckFlagStmt(index, cmp_op, value, true_body, false_body)


# --------------------------------------------------------------------------
# Symbol table (flag/portrait/room/sound names)
# --------------------------------------------------------------------------

DEFINE_RE = re.compile(r"^\s*#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+(\S+)")


def load_symbols(path: Path) -> dict[str, int]:
    if not path.exists():
        return {}
    symbols: dict[str, int] = {}
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


# --------------------------------------------------------------------------
# Code generation (2-pass: assemble bottom-up with placeholder string
# references, then resolve string offsets once the layout is final)
# --------------------------------------------------------------------------

class Patch:
    __slots__ = ("position", "text")

    def __init__(self, position: int, text: str) -> None:
        self.position = position
        self.text = text


def compile_stmts(stmts: list[Stmt]) -> tuple[bytearray, list[Patch]]:
    buf = bytearray()
    patches: list[Patch] = []
    for stmt in stmts:
        if isinstance(stmt, TextStmt):
            patches.append(Patch(len(buf) + 1, stmt.text))
            buf += bytes([OP_TEXT, 0, 0])
        elif isinstance(stmt, PortraitShowStmt):
            buf += bytes([OP_PORTRAIT_SHOW, check_u8(stmt.portrait_id),
                          check_u8(stmt.side)])
        elif isinstance(stmt, PortraitHideStmt):
            buf += bytes([OP_PORTRAIT_HIDE])
        elif isinstance(stmt, SetFlagStmt):
            buf += bytes([OP_SET_FLAG, check_u8(stmt.index),
                          check_u8(stmt.value)])
        elif isinstance(stmt, CheckFlagStmt):
            true_bytes, true_patches = compile_stmts(stmt.true_body)
            false_bytes, false_patches = compile_stmts(stmt.false_body)
            buf += bytes([OP_CHECK_FLAG, check_u8(stmt.index), stmt.cmp_op,
                          check_u8(stmt.value)])
            buf += struct.pack("<HH", len(true_bytes), len(false_bytes))
            base = len(buf)
            buf += true_bytes
            patches.extend(Patch(base + p.position, p.text) for p in true_patches)
            base = len(buf)
            buf += false_bytes
            patches.extend(Patch(base + p.position, p.text) for p in false_patches)
        elif isinstance(stmt, WaitKeyStmt):
            buf += bytes([OP_WAIT_KEY])
        elif isinstance(stmt, RoomTransitionStmt):
            buf += bytes([OP_ROOM_TRANSITION, check_u8(stmt.room),
                          check_u8(stmt.x), check_u8(stmt.y)])
        elif isinstance(stmt, SoundStmt):
            buf += bytes([OP_SOUND, check_u8(stmt.sound_id)])
        elif isinstance(stmt, GiveObjectStmt):
            buf += bytes([OP_GIVE_OBJECT, check_u8(stmt.object_type),
                          check_u8(stmt.quantity)])
        else:
            raise SystemExit(f"internal error: unhandled statement {stmt!r}")
    return buf, patches


def check_u8(value: int) -> int:
    if not 0 <= value <= 255:
        raise SystemExit(f"value {value} does not fit in one byte")
    return value


def decl_kind_label(decl: ScriptDecl | ConversationDecl | RoomDecl) -> str:
    if isinstance(decl, ConversationDecl):
        return "conversation"
    if isinstance(decl, RoomDecl):
        return "room"
    return "script"


def compile_decl(decl: ScriptDecl | ConversationDecl | RoomDecl) -> bytes:
    if isinstance(decl, ScriptDecl):
        body, patches = compile_stmts(decl.body)
        body += bytes([OP_END])
        header = bytes([FORMAT_VERSION, KIND_SCRIPT, 0])
        bytecode = bytearray(header) + body
        patches = [Patch(len(header) + p.position, p.text) for p in patches]
        return link_strings(bytecode, patches)

    if isinstance(decl, RoomDecl):
        return compile_room_decl(decl)

    assert isinstance(decl, ConversationDecl)
    if len(decl.topics) > 255:
        raise SystemExit(f"conversation {decl.name!r} has more than 255 topics")

    seen: dict[bytes, str] = {}
    for topic in decl.topics:
        prefix = topic.keyword.upper().encode("ascii")[:4].ljust(4, b"\0")
        if prefix in seen:
            raise SystemExit(
                f"conversation {decl.name!r}: topics {seen[prefix]!r} and "
                f"{topic.keyword!r} both match the first 4 letters "
                f"({prefix.rstrip(chr(0).encode()).decode()!r}) - one would "
                "always shadow the other"
            )
        seen[prefix] = topic.keyword

    header = bytes([FORMAT_VERSION, KIND_CONVERSATION, len(decl.topics)])
    topic_table_len = len(decl.topics) * 6
    bytecode = bytearray(header)
    bytecode += bytes(topic_table_len)  # placeholder, patched below
    all_patches: list[Patch] = []
    for i, topic in enumerate(decl.topics):
        body, patches = compile_stmts(topic.body)
        body += bytes([OP_END])
        entry_offset = len(bytecode)
        table_pos = len(header) + i * 6
        keyword_bytes = topic.keyword.upper().encode("ascii")
        keyword_bytes = keyword_bytes[:4].ljust(4, b"\0")
        bytecode[table_pos:table_pos + 4] = keyword_bytes
        struct.pack_into("<H", bytecode, table_pos + 4, entry_offset)
        all_patches.extend(Patch(entry_offset + p.position, p.text) for p in patches)
        bytecode += body
    return link_strings(bytecode, all_patches)


def compile_room_decl(decl: RoomDecl) -> bytes:
    if len(decl.entries) > 255:
        raise SystemExit(f"room {decl.name!r} has more than 255 entries")

    seen: dict[int, None] = {}
    for entry in decl.entries:
        key = check_u8(entry.key)
        if key in seen:
            raise SystemExit(
                f"room {decl.name!r}: two entries both use key {key} "
                f"(0x{key:02X})"
            )
        seen[key] = None

    header = bytes([FORMAT_VERSION, KIND_ROOM, len(decl.entries)])
    entry_table_len = len(decl.entries) * 3
    bytecode = bytearray(header)
    bytecode += bytes(entry_table_len)  # placeholder, patched below
    all_patches: list[Patch] = []
    for i, entry in enumerate(decl.entries):
        body, patches = compile_stmts(entry.body)
        body += bytes([OP_END])
        entry_offset = len(bytecode)
        table_pos = len(header) + i * 3
        bytecode[table_pos] = check_u8(entry.key)
        struct.pack_into("<H", bytecode, table_pos + 1, entry_offset)
        all_patches.extend(Patch(entry_offset + p.position, p.text) for p in patches)
        bytecode += body
    return link_strings(bytecode, all_patches)


def ascii_to_petscii(value: int) -> int:
    """Same mapping as tools/prepare_c64_assets.py - the platform's text
    renderer expects screen codes, not raw ASCII, for any string it prints."""
    if 0x41 <= value <= 0x5A:
        return value + 0x80
    if 0x61 <= value <= 0x7A:
        return value - 0x20
    return value


def link_strings(bytecode: bytearray, patches: list[Patch]) -> bytes:
    pool: dict[str, int] = {}
    string_table = bytearray()
    table_start = len(bytecode)
    for patch in patches:
        if patch.text not in pool:
            pool[patch.text] = table_start + len(string_table)
            encoded = bytes(ascii_to_petscii(b) for b in patch.text.encode("ascii"))
            string_table += encoded + b"\0"
    for patch in patches:
        struct.pack_into("<H", bytecode, patch.position, pool[patch.text])
    return bytes(bytecode) + bytes(string_table)


# --------------------------------------------------------------------------
# CLI
# --------------------------------------------------------------------------

def main() -> None:
    parser = argparse.ArgumentParser(
        description="Compile a cutscene/conversation script into the "
                     "binary format the script-interpreter overlay executes."
    )
    parser.add_argument("--input", type=Path, required=True)
    output_group = parser.add_mutually_exclusive_group(required=True)
    output_group.add_argument("--output-dir", type=Path,
                        help="directory to write one <name>.scr per "
                             "top-level script/conversation declaration")
    output_group.add_argument("--single-output", type=Path,
                        help="write the input's one top-level declaration "
                             "directly to this path (errors if the input "
                             "declares zero or more than one) - for a build "
                             "rule that maps one source file to one "
                             "resource ID, e.g. assets/scripts/F0.script -> "
                             "assets/resources/F0")
    parser.add_argument("--flags", type=Path, default=Path("src/story.h"),
                        help="file to read #define NAME value symbols from "
                             "(default: src/story.h)")
    args = parser.parse_args()

    symbols = load_symbols(args.flags)
    tokens = tokenize(args.input.read_text(encoding="utf-8"))
    decls = Parser(tokens, symbols).parse_program()
    if not decls:
        raise SystemExit(f"{args.input}: no script or conversation declared")

    if args.single_output is not None:
        if len(decls) != 1:
            names = ", ".join(decl.name for decl in decls)
            raise SystemExit(
                f"{args.input}: --single-output requires exactly one "
                f"top-level declaration, found {len(decls)} ({names})"
            )
        decl = decls[0]
        data = compile_decl(decl)
        args.single_output.parent.mkdir(parents=True, exist_ok=True)
        args.single_output.write_bytes(data)
        kind = decl_kind_label(decl)
        print(f"{decl.name}: {kind}, {len(data)} bytes -> {args.single_output}")
        return

    args.output_dir.mkdir(parents=True, exist_ok=True)
    for decl in decls:
        data = compile_decl(decl)
        out_path = args.output_dir / f"{decl.name}.scr"
        out_path.write_bytes(data)
        kind = decl_kind_label(decl)
        print(f"{decl.name}: {kind}, {len(data)} bytes -> {out_path}")


if __name__ == "__main__":
    main()
