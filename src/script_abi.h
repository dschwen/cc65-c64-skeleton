#ifndef SCRIPT_ABI_H
#define SCRIPT_ABI_H

#include <stdint.h>

/* Mutable state for the banked script interpreter (modules/script.c, bank
 * BANK_SCRIPT - see src/banks.h). All of it is resident (defined in
 * src/script_runtime.c): banked code may not keep mutable data of its own
 * (see memmap.h). modules/script.c owns it only for the duration of a
 * synchronous script call. */
typedef struct ScriptWorkspace {
    uint8_t* buffer;
    uint16_t capacity;
    uint16_t window_base;
    uint16_t window_length;
    uint16_t resource_length;
} ScriptWorkspace;

extern ScriptWorkspace script_workspace;

/* The "context tile" the room-object opcodes (check_object,
 * replace_object - see tools/compile_script.py) act on: the tile a
 * look_at/use_at entry was dispatched for, or the player's own tile for an
 * enter_room/enter_tile entry. Set by src/room_dispatch.c before every
 * room hook runs. Whole-tile coordinates, 0-19 / 0-10. */
extern uint8_t script_context_x;
extern uint8_t script_context_y;

/* Nonzero while a script is running inside a still-open room-transition
 * bracket and has not yet needed the display - see modules/script.c's
 * display_ready(). */
extern uint8_t script_display_deferred;

#endif
