#include <string.h>

#include "world.h"

uint8_t __fastcall__ world_room_has_actor_slot(uint8_t room_id);

#pragma bss-name (push, "ROOMBASE")
static PlatformObject room_object_baseline[PLATFORM_ROOM_OBJECT_COUNT];
#pragma bss-name (pop)

#pragma bss-name (push, "WORLDDELTA")
GameWorldDelta game_world_deltas[GAME_WORLD_DELTA_CAPACITY];
#pragma bss-name (pop)

uint16_t game_world_delta_count;
static uint8_t baseline_room;
static uint8_t baseline_valid;

#pragma code-name (push, "SAVECODE")

static uint8_t object_equal(const PlatformObject* left,
                            const PlatformObject* right) {
    return left->type == right->type && left->x == right->x &&
           left->y == right->y;
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

    if (!world_room_has_actor_slot(room->id)) return PLATFORM_ERR_FULL;

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

#pragma code-name (pop)

#pragma code-name (push, "UPPERCODE")

void game_world_reset(void) {
    game_world_delta_count = 0u;
    memset(game_world_deltas, 0, sizeof(game_world_deltas));
    memcpy(room_object_baseline, platform_room.objects,
           sizeof(room_object_baseline));
    if (platform_player != 0) {
        memset(&room_object_baseline[platform_player_slot], 0,
               sizeof(PlatformObject));
    }
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

#pragma code-name (pop)
