#ifndef GAME_WORLD_H
#define GAME_WORLD_H

#include <stdint.h>

#include "platform.h"

#define GAME_WORLD_DELTA_CAPACITY 200u

typedef struct GameWorldDelta {
    uint8_t room;
    uint8_t slot;
    PlatformObject object;
} GameWorldDelta;

extern GameWorldDelta game_world_deltas[GAME_WORLD_DELTA_CAPACITY];
extern uint16_t game_world_delta_count;

/* Install room hooks and establish the pristine baseline for startup room 0. */
void game_world_init(void);
/* Discard all mutations and re-baseline the currently loaded room. */
void game_world_reset(void);
/* Capture current non-player object slots as deltas against the room asset. */
uint8_t game_world_capture_current(void);

/*
 * Save/load overlay support: temporarily drop the leaving-room store
 * (keeping restore active) so platform_room_enter() does not merge the
 * about-to-be-replaced live session into a freshly loaded journal, then
 * restore normal capture behavior afterward. See SAVE_GAME.md.
 */
void game_world_disable_store_hook(void);
void game_world_enable_store_hook(void);

/*
 * Banked world_store()/world_restore() (src/banks.h's BANK_WORLD_STATE) -
 * src/platform.c's platform_room_enter() is the only other caller, and
 * calls world_state_entry() directly through far_call_bank() rather than
 * through a registered function pointer (see src/world.c's header
 * comment). world_store_enabled replaces the old store-hook's registered/
 * unregistered state; restore has no such flag; it was always registered
 * together with store, never independently.
 */
#define WORLD_STATE_OP_STORE   0u
#define WORLD_STATE_OP_RESTORE 1u
extern uint8_t world_store_enabled;
extern PlatformRoom* world_state_room;
extern uint8_t world_state_op;
extern uint8_t world_state_result;
void world_state_entry(void);

#endif
