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
 * Save/load overlay support: temporarily drop the leaving-room store hook
 * (keeping the restore hook) so platform_room_enter() does not merge the
 * about-to-be-replaced live session into a freshly loaded journal, then
 * restore normal capture behavior afterward. See SAVE_GAME.md.
 */
void game_world_disable_store_hook(void);
void game_world_enable_store_hook(void);

#endif
