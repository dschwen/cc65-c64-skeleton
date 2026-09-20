#include <stdint.h>
#include "memmap.h"
#include "banks.h"
#include "game.h"

/*
 * Native (oscar64 C) replacement for src/room_code.s + src/room_runtime.c
 * (the $9900 copied-overlay scheme). Under oscar64's whole-program model,
 * every room's compiled code already exists in the finished binary,
 * living in that room's own EasyFlash bank - there is nothing to "load":
 * no runtime header/checksum validation, no copy to a fixed RAM address,
 * no BSS-clear step. Activating a room is just remembering which bank and
 * which four function pointers to use; game_room_enter_tile_native() and
 * friends bank-switch to them directly.
 *
 * Each room provides four functions with room-specific names (see
 * rooms/room_table.h, generated alongside the rooms themselves) matching
 * game.h's enter_room/enter_tile/look_at/use_at contract. All must be
 * __noinline (see memmap.h's far_call_bank() comment) since they live in
 * a #pragma code() bank section.
 */

#include "../rooms/room_table.h"

typedef struct RoomEntry {
    uint8_t id;
    uint8_t bank;
    void (*enter_room)(void);
    void (*enter_tile)(void);
    uint8_t (*look_at)(uint8_t tile_x, uint8_t tile_y);
    uint8_t (*use_at)(uint8_t tile_x, uint8_t tile_y);
} RoomEntry;

static const RoomEntry room_table[] = {
    ROOM_TABLE_ENTRIES
};
#define ROOM_TABLE_COUNT (sizeof(room_table) / sizeof(room_table[0]))

uint8_t game_room_code_active;
static const RoomEntry* active_room;

void platform_easyflash_copy_roml(uint8_t bank, uint16_t offset, void* destination, uint16_t size);

/* Copies room_id's raw PlatformRoom bytes (embedded at its own bank's
 * byte 0 by tools/compile_room.py - see room00_data and friends) into
 * `destination`. Returns 1 on success, 0 for an unrecognized room_id -
 * replaces the old runtime-validated EasyFlash room directory; there is
 * nothing left to validate since the data is a compiled, linked part of
 * this same program. */
uint8_t platform_room_data_fetch(uint8_t room_id, void* destination) {
    uint8_t i;
    for (i = 0u; i < ROOM_TABLE_COUNT; ++i) {
        if (room_table[i].id == room_id) {
            platform_easyflash_copy_roml(room_table[i].bank, 0u, destination, ROOM_FILE_BYTES);
            return 1u;
        }
    }
    return 0u;
}

/* Finds room_id's table entry and remembers it as the active room, or
 * clears the active room if room_id has no compiled room code (an
 * unrecognized ID - every current room is always found, this only
 * matters for defensive robustness). Both game_room_code_prepare() and
 * game_room_code_activate() collapse into this single step: there is no
 * separate prepare/validate phase left to do. */
uint8_t game_room_code_prepare(uint8_t room_id) {
    uint8_t i;
    for (i = 0u; i < ROOM_TABLE_COUNT; ++i) {
        if (room_table[i].id == room_id) {
            active_room = &room_table[i];
            return PLATFORM_OK;
        }
    }
    active_room = 0;
    return PLATFORM_ERR_NOT_FOUND;
}

void game_room_code_activate(void) {
    game_room_code_active = active_room != 0 ? 1u : 0u;
}

uint8_t game_room_code_load_current(void) {
    uint8_t status = game_room_code_prepare(platform_current_room);
    game_room_code_activate();
    return status;
}

void game_room_enter_tile_native(void) {
    if (!game_room_code_active) return;
    far_call_bank(active_room->bank, active_room->enter_tile);
}

void game_room_enter_room_native(void) {
    if (!game_room_code_active) return;
    far_call_bank(active_room->bank, active_room->enter_room);
}

static uint8_t look_at_tile_x, look_at_tile_y, look_at_result;
static void look_at_call(void) {
    look_at_result = active_room->look_at(look_at_tile_x, look_at_tile_y);
}

uint8_t game_room_look_at_native(uint8_t tile_x, uint8_t tile_y) {
    if (!game_room_code_active) return GAME_LOOK_DEFAULT;
    look_at_tile_x = tile_x;
    look_at_tile_y = tile_y;
    far_call_bank(active_room->bank, look_at_call);
    return look_at_result;
}

static uint8_t use_at_tile_x, use_at_tile_y, use_at_result;
static void use_at_call(void) {
    use_at_result = active_room->use_at(use_at_tile_x, use_at_tile_y);
}

uint8_t game_room_use_at_native(uint8_t tile_x, uint8_t tile_y) {
    if (!game_room_code_active) return GAME_USE_DEFAULT;
    use_at_tile_x = tile_x;
    use_at_tile_y = tile_y;
    far_call_bank(active_room->bank, use_at_call);
    return use_at_result;
}
