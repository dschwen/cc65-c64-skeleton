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

#endif
