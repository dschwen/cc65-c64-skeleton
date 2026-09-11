#ifndef GAME_LOOK_HELPERS_ABI_H
#define GAME_LOOK_HELPERS_ABI_H

#include "platform.h"

/* Temporary LH state borrows room_stage through platform_room_scratch().
 * Every current caller runs between room transitions. */
typedef struct LookHelpersWorkspace {
    uint8_t intersects[PLATFORM_ROOM_OBJECT_COUNT];
    uint8_t counts[PLATFORM_OBJECT_TYPE_COUNT];
    char buffer[81];
    uint8_t length;
    uint8_t truncated;
} LookHelpersWorkspace;

#endif
