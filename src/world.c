#include <string.h>

#include "world.h"

static PlatformObject room_object_baseline[PLATFORM_ROOM_OBJECT_COUNT];
GameWorldDelta game_world_deltas[GAME_WORLD_DELTA_CAPACITY];
uint16_t game_world_delta_count;
static uint8_t baseline_room;
static uint8_t baseline_valid;

static uint8_t object_equal(const PlatformObject* left,
                            const PlatformObject* right) {
    return left->type == right->type && left->x == right->x &&
           left->y == right->y;
}

/*
 * Preflight one actor slot in `room` (always called with room_stage's own
 * address, the staged destination room, before any delta from
 * game_world_deltas has been applied to it) - native (oscar64 C)
 * replacement for src/world_slots.s, which read the staged room through a
 * hardcoded fixed address instead of a parameter. Counts free (type==0)
 * slots in the room's base object list, then adjusts that count for every
 * delta on file for this room: a delta that clears a previously-occupied
 * base slot frees it; a delta that fills a previously-free base slot
 * occupies it; anything else (still occupied, still free) is a no-op.
 */
static uint8_t world_room_has_actor_slot(const PlatformRoom* room) {
    int16_t free_count = 0;
    uint16_t i;

    for (i = 0u; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (room->objects[i].type == 0u) ++free_count;
    }
    for (i = 0u; i < game_world_delta_count; ++i) {
        const GameWorldDelta* delta = &game_world_deltas[i];
        uint8_t base_type;
        if (delta->room != room->id) continue;
        base_type = room->objects[delta->slot].type;
        if (base_type != 0u) {
            if (delta->object.type == 0u) ++free_count;
        } else {
            if (delta->object.type != 0u) --free_count;
        }
    }
    return free_count > 0 ? 1u : 0u;
}

static uint8_t world_store(const PlatformRoom* room) {
    uint16_t existing;
    uint16_t changed;
    uint16_t i;
    uint16_t write;

    if (room == 0 || !baseline_valid || baseline_room != room->id) {
        return PLATFORM_ERR_FORMAT;
    }

    existing = 0u;
    for (i = 0u; i < game_world_delta_count; ++i) {
        if (game_world_deltas[i].room != room->id) ++existing;
    }
    changed = 0u;
    for (i = 0u; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (&room->objects[i] == platform_player) continue;
        if (!object_equal(&room->objects[i], &room_object_baseline[i])) ++changed;
    }
    if (existing + changed > GAME_WORLD_DELTA_CAPACITY) {
        return PLATFORM_ERR_FULL;
    }

    write = 0u;
    for (i = 0u; i < game_world_delta_count; ++i) {
        if (game_world_deltas[i].room != room->id) {
            if (write != i) game_world_deltas[write] = game_world_deltas[i];
            ++write;
        }
    }
    for (i = 0u; i < PLATFORM_ROOM_OBJECT_COUNT; ++i) {
        if (&room->objects[i] == platform_player ||
            object_equal(&room->objects[i], &room_object_baseline[i])) continue;
        game_world_deltas[write].room = room->id;
        game_world_deltas[write].slot = (uint8_t)i;
        game_world_deltas[write].object = room->objects[i];
        ++write;
    }
    game_world_delta_count = write;
    return PLATFORM_OK;
}

static uint8_t world_restore(PlatformRoom* room) {
    uint16_t i;

    if (!world_room_has_actor_slot(room)) return PLATFORM_ERR_FULL;

    memcpy(room_object_baseline, room->objects, sizeof(room_object_baseline));
    baseline_room = room->id;
    baseline_valid = 1u;
    for (i = 0u; i < game_world_delta_count; ++i) {
        if (game_world_deltas[i].room == room->id) {
            room->objects[game_world_deltas[i].slot] =
                game_world_deltas[i].object;
        }
    }
    return PLATFORM_OK;
}

void game_world_reset(void) {
    game_world_delta_count = 0u;
    memset(game_world_deltas, 0, sizeof(game_world_deltas));
    memcpy(room_object_baseline, platform_room.objects,
           sizeof(room_object_baseline));
    /* platform_player is always valid here: this function's only caller
     * (game_world_init()) runs right after platform_init(), which
     * unconditionally sets platform_player before returning. */
    memset(&room_object_baseline[platform_player_slot], 0,
           sizeof(PlatformObject));
    baseline_room = platform_room.id;
    baseline_valid = 1u;
}

void game_world_init(void) {
    game_world_reset();
    platform_room_state_hooks(world_store, world_restore);
}

uint8_t game_world_capture_current(void) {
    return world_store(&platform_room);
}

void game_world_disable_store_hook(void) {
    platform_room_state_hooks(0, world_restore);
}

void game_world_enable_store_hook(void) {
    platform_room_state_hooks(world_store, world_restore);
}
