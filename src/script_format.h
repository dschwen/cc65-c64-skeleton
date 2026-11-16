#ifndef SCRIPT_FORMAT_H
#define SCRIPT_FORMAT_H

/* The compiled script/conversation/room resource format, shared between
 * the interpreter (modules/script.c) and the compiler
 * (tools/compile_script.py, which parses the SCRIPT_* defines below at
 * startup - so this header is the single source of truth for the opcode
 * numbering; the full binary layout is documented in that tool's module
 * docstring).
 *
 * Keep each define on one line as `#define NAME value` - that is all the
 * tool's #define parser understands. */

#define SCRIPT_FORMAT_VERSION    1u

/* Byte 1 (the "kind" byte) of a compiled resource. */
#define SCRIPT_KIND_SCRIPT       0u
#define SCRIPT_KIND_CONVERSATION 1u
#define SCRIPT_KIND_ROOM         2u

/* Opcodes (operand layout in tools/compile_script.py's docstring). */
#define SCRIPT_OP_END                  0x00u
#define SCRIPT_OP_TEXT                 0x01u
#define SCRIPT_OP_PORTRAIT_SHOW        0x02u
#define SCRIPT_OP_PORTRAIT_HIDE        0x03u
#define SCRIPT_OP_SET_FLAG             0x04u
#define SCRIPT_OP_CHECK_FLAG           0x05u
#define SCRIPT_OP_WAIT_KEY             0x06u
#define SCRIPT_OP_ROOM_TRANSITION      0x07u
#define SCRIPT_OP_SOUND                0x08u
#define SCRIPT_OP_GIVE_OBJECT          0x09u
#define SCRIPT_OP_ROOM_TRANSITION_HERE 0x0Au
#define SCRIPT_OP_SET_BIT              0x0Bu
#define SCRIPT_OP_CLEAR_BIT            0x0Cu
#define SCRIPT_OP_LIGHTNING            0x0Du
#define SCRIPT_OP_CONVERSE             0x0Eu
/* Inventory and room-object opcodes - what lets a puzzle live entirely in
 * its .script source instead of a C fragment (see ROOM_CODE_API.md). */
#define SCRIPT_OP_CHECK_ITEM           0x0Fu
#define SCRIPT_OP_TAKE_ITEM            0x10u
#define SCRIPT_OP_CHECK_OBJECT         0x11u
#define SCRIPT_OP_REPLACE_OBJECT       0x12u
#define SCRIPT_OP_ADD_OBJECT           0x13u
#define SCRIPT_OP_SET_TILE             0x14u
#define SCRIPT_OP_TILE_COLOR           0x15u

/* CHECK_FLAG comparison operands. */
#define SCRIPT_CMP_EQ   0u
#define SCRIPT_CMP_NE   1u
#define SCRIPT_CMP_LT   2u
#define SCRIPT_CMP_GT   3u
#define SCRIPT_CMP_LE   4u
#define SCRIPT_CMP_GE   5u
#define SCRIPT_CMP_BIT  6u

/* ROOM_TRANSITION/_HERE message operand meaning "no message". */
#define SCRIPT_NO_MESSAGE 0xFFFFu

#define SCRIPT_KEYWORD_BYTES     4u
#define SCRIPT_TOPIC_ENTRY_BYTES 6u
#define SCRIPT_ROOM_ENTRY_BYTES  3u

#endif
