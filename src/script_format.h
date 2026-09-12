#ifndef SCRIPT_FORMAT_H
#define SCRIPT_FORMAT_H

/* Byte 1 (the "kind" byte) of a compiled script/conversation/room resource -
 * see tools/compile_script.py's docstring for the full binary format. Kept
 * as a tiny shared header (unlike the opcode set) because both resident
 * code and modules/script.c need to agree on it: resident code
 * (game_room_script_entry() in src/script_runtime.c) reads a room's
 * KIND_ROOM entry table directly, without entering the interpreter service,
 * to decide whether a banked call is worth paying for. */
#define SCRIPT_KIND_SCRIPT       0u
#define SCRIPT_KIND_CONVERSATION 1u
#define SCRIPT_KIND_ROOM         2u

#endif
