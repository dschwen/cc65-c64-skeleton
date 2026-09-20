#include <string.h>

#include "world.h"
#include "memmap.h"
#include "banks.h"

/* Resident state: the per-room object-delta journal and the last-loaded
 * room's pristine baseline, both needed every frame/transition regardless
 * of where the store/restore *code* lives (see below). */
static PlatformObject room_object_baseline[PLATFORM_ROOM_OBJECT_COUNT];
GameWorldDelta game_world_deltas[GAME_WORLD_DELTA_CAPACITY];
uint16_t game_world_delta_count;
static uint8_t baseline_room;
static uint8_t baseline_valid;

/* Whether platform.c's room-transition path should call the store side at
 * all - replaces the old registered/unregistered store-hook pointer (see
 * below and platform_room_enter()'s call site in src/platform.c). Restore
 * is unconditional once game_world_init() has run (was always registered
 * together with store, never independently), so it needs no flag. */
uint8_t world_store_enabled;

/* Parameter/result passing for world_state_entry() (below), the one
 * far_call_bank() target this file exposes - see its own comment and
 * world.h's. Not static: src/platform.c's platform_room_enter() sets
 * these directly before calling
 * far_call_bank(BANK_WORLD_STATE, world_state_entry). */
PlatformRoom* world_state_room;
uint8_t world_state_op;
uint8_t world_state_result;

#pragma section( world_state_code, 0 )
#pragma section( world_state_data, 0 )
#pragma region( world_state_bank, 0x8000, 0xc000, , BANK_WORLD_STATE,
                 { world_state_data, world_state_code } )
#pragma code(world_state_code)
#pragma data(world_state_data)

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

/* far_call_bank() target for both directions - a single shared entry point
 * (rather than two) keeps every far_call_bank() call site in the rest of
 * the program naming exactly one literal function, matching every other
 * banked service in this codebase. world_state_op/world_state_room are
 * set by the resident caller first (see world.c's own resident functions
 * below and src/platform.c's platform_room_enter()). */
__noinline void world_state_entry(void) {
    if (world_state_op == WORLD_STATE_OP_STORE) {
        world_state_result = world_store(world_state_room);
    } else {
        world_state_result = world_restore(world_state_room);
    }
}

#pragma code(code)
#pragma data(data)

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
    world_store_enabled = 1u;
}

uint8_t game_world_capture_current(void) {
    world_state_room = &platform_room;
    world_state_op = WORLD_STATE_OP_STORE;
    far_call_bank(BANK_WORLD_STATE, world_state_entry);
    return world_state_result;
}

void game_world_disable_store_hook(void) {
    world_store_enabled = 0u;
}

void game_world_enable_store_hook(void) {
    world_store_enabled = 1u;
}
