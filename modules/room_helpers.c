#include <stdint.h>

#include "platform.h"

/* Independently linked, in-place room-helpers service ("RH"): the bodies of
 * platform_room_object_remove() and platform_room_neighbor(), moved out of
 * the always-resident engine. Both are confirmed safe to run from here -
 * every call site (game_support.c's Take handler, platform_player_step(),
 * platform_look_exit()) runs from resident code, never from a room's own
 * banked-in code. `platform_room_object_add()` remains resident because room
 * entry uses it while destination room code is staged at `$B000`.
 *
 * Entry/parameters: the resident wrappers in src/platform.c stage their
 * arguments into room_helpers_* globals and set room_helpers_op before
 * entering this service (there is one parameterless native entry point,
 * `room_helpers_banked_run()`; see `src/banked_api.s`). The result goes back
 * out through room_helpers_result (and room_helpers_room_id for the
 * neighbor lookup).
 */

#define ROOM_HELPERS_OP_NEIGHBOR 0u
#define ROOM_HELPERS_OP_REMOVE   1u

extern PlatformRoom* room_helpers_room;
extern uint8_t room_helpers_slot;
extern uint8_t room_helpers_direction;
extern uint8_t room_helpers_room_id;
extern uint8_t room_helpers_op;
extern uint8_t room_helpers_result;
extern const PlatformRoom* rendered_room;
extern uint16_t rendered_object_limit;

static uint8_t room_helpers_neighbor(void) {
    const PlatformRoom* room;
    uint8_t direction;
    uint8_t mask;
    uint8_t neighbor;

    room = room_helpers_room;
    direction = room_helpers_direction;
    switch (direction) {
        case PLATFORM_DIRECTION_NORTH:
            mask = PLATFORM_ROOM_EXIT_NORTH;
            neighbor = room->north;
            break;
        case PLATFORM_DIRECTION_EAST:
            mask = PLATFORM_ROOM_EXIT_EAST;
            neighbor = room->east;
            break;
        case PLATFORM_DIRECTION_WEST:
            mask = PLATFORM_ROOM_EXIT_WEST;
            neighbor = room->west;
            break;
        case PLATFORM_DIRECTION_SOUTH:
            mask = PLATFORM_ROOM_EXIT_SOUTH;
            neighbor = room->south;
            break;
        default:
            return PLATFORM_ERR_ARGUMENT;
    }
    if ((room->exit_mask & mask) == 0u) return PLATFORM_ERR_NOT_FOUND;
    room_helpers_room_id = neighbor;
    return PLATFORM_OK;
}

/* Only mutate the object array and resident rendered-object limit. Light,
 * dirty-cell, redraw, and lighting work stay in the resident wrapper so this
 * immutable service has one small, explicit side-effect boundary and never
 * depends on `$B000` renderer workspace. */
static uint8_t room_helpers_object_remove(void) {
    PlatformRoom* room;
    PlatformObject* object;
    uint8_t slot;

    room = room_helpers_room;
    slot = room_helpers_slot;
    object = &room->objects[slot];
    if (object->type == 0u) return PLATFORM_ERR_ARGUMENT;
    object->type = 0;
    object->x = 0;
    object->y = 0;
    if (room == rendered_room && (uint16_t)slot + 1u == rendered_object_limit) {
        while (rendered_object_limit > 0u &&
               room->objects[rendered_object_limit - 1u].type == 0u) {
            --rendered_object_limit;
        }
    }
    return PLATFORM_OK;
}

void room_helpers_banked_run(void) {
    if (room_helpers_op == ROOM_HELPERS_OP_REMOVE) {
        room_helpers_result = room_helpers_object_remove();
    } else {
        room_helpers_result = room_helpers_neighbor();
    }
}
