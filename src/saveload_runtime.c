#include <stdint.h>
#include <string.h>

#include "game.h"
#include "saveload_abi.h"
#include "world.h"

/* Resident orchestration for the in-place SL/SV services. Both execute from
 * bank 48 ROML and use only explicitly resident mutable state. */
#define SAVELOAD_SCREEN     ((uint8_t*)0xf800)
#define SAVELOAD_COLOR      ((uint8_t*)0xd800)
#define SAVELOAD_VIC_CTRL1  (*(volatile uint8_t*)0xd011)
#define SAVE_RECORD_RAM     ((const uint8_t*)SAVELOAD_RECORD_ADDRESS)
#define SAVE_HEADER_BYTES   16u
#define SAVE_PREFIX_BYTES   130u
#define SAVE_DELTA_BYTES    5u
#define SAVE_MAX_DELTAS     200u

void raster_irq_suspend(void);
void raster_irq_resume(void);
uint8_t saveload_mode;
uint8_t saveload_selected_slot;
uint8_t saveload_load_pending;

#pragma bss-name (push, "SAVEWORK")
uint8_t saveload_workspace[SAVELOAD_WORKSPACE_BYTES];
#pragma bss-name (pop)

#pragma code-name (push, "HIGHCODE")

static uint16_t save_get16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

#pragma code-name (pop)
#pragma code-name (push, "LOWCODE")

static uint8_t saveload_restore_tiles(void) {
    return platform_resource_fetch(PLATFORM_RESOURCE_KIND_ASSET,
                                   PLATFORM_ASSET_TILES,
                                   (uint8_t*)SAVELOAD_RECORD_ADDRESS,
                                   2048u + 256u) == 2048u + 256u
               ? PLATFORM_OK
               : PLATFORM_ERR_FORMAT;
}

/* Apply only after the banked browser has returned. The record temporarily
 * occupies tile-source RAM, so every exit restores that asset before normal
 * drawing resumes. */
static uint8_t saveload_apply_pending(void) {
    const uint8_t* p;
    uint16_t delta_count;
    uint16_t delta_bytes;
    uint8_t room;
    uint8_t type;
    uint8_t x;
    uint8_t y;
    uint8_t result;

    if (!saveload_load_pending) return PLATFORM_ERR_FORMAT;
    saveload_load_pending = 0u;
    p = SAVE_RECORD_RAM + SAVE_HEADER_BYTES;
    delta_count = save_get16(p + 128u);
    delta_bytes = (uint16_t)(delta_count << 2) + delta_count;
    if (delta_count > SAVE_MAX_DELTAS ||
        SAVE_PREFIX_BYTES + delta_bytes !=
            save_get16(SAVE_RECORD_RAM + 10u) ||
        p[28] > p[29] || p[30] > p[31]) {
        (void)saveload_restore_tiles();
        return PLATFORM_ERR_FORMAT;
    }

    room = p[24];
    type = p[25];
    x = p[26];
    y = p[27];
    /* turn through maximum_mana are the first 16 bytes of GameState and
     * immediately follow the 16-byte save name. */
    memcpy(&game_state, p + 16u, 16u);
    game_state.pending_transition = 0u;
    memcpy(game_state.inventory, p + 32u,
           sizeof(game_state.inventory));
    memcpy(game_state.flags, p + 96u, sizeof(game_state.flags));

    game_world_delta_count = 0u;
    memcpy(game_world_deltas, p + SAVE_PREFIX_BYTES, delta_bytes);
    game_world_delta_count = delta_count;

    if (saveload_restore_tiles() != PLATFORM_OK) return PLATFORM_ERR_FORMAT;
    game_world_disable_store_hook();

    /* One screen-blanked, interrupt-suspended bracket for the whole switch,
     * same as game_process_pending_transition() - see platform_room_enter()'s
     * own comment for why the gap between two separate brackets is unsafe.
     * game_transition_message is always empty on this path in practice
     * (nothing sets it for a save/load restore) - wired through the same
     * two helpers as game_process_pending_transition() purely for
     * consistency, not because this path needs the loading-message feature. */
    game_transition_message_show();
    raster_irq_suspend();
    result = platform_room_enter(room, type, x, y);
    game_world_enable_store_hook();
    if (result == PLATFORM_OK) {
        game_player_sync_from_platform();
        game_entry_reason = GAME_ENTRY_LOAD;
        game_enter_room();
        platform_sprites_hide_all();
        game_enter_tile();
    }
    raster_irq_resume();
    game_transition_reveal();
    return result;
}

#pragma code-name (pop)

#pragma code-name (push, "HIGHCODE")

static void saveload_cleanup(uint8_t result, uint8_t tiles_restored) {
    if (!tiles_restored && saveload_restore_tiles() != PLATFORM_OK) {
        result = PLATFORM_ERR_FORMAT;
    }
    SAVELOAD_VIC_CTRL1 &= 0xefu;
    raster_irq_suspend();
    memset(SAVELOAD_SCREEN, platform_text_screen_code(' '), 1000u);
    memset(SAVELOAD_COLOR, 0, 1000u);
    platform_text_screen_leave();
    platform_room_draw(&platform_room, platform_player);
    raster_irq_resume();
    SAVELOAD_VIC_CTRL1 |= 0x10u;
    if (result != PLATFORM_OK) {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "Save/load error.", 1u);
    }
}

void game_load_show(void) {
    uint8_t result;
    uint8_t tiles_restored;

    platform_look_cursor_hide();
    SAVELOAD_VIC_CTRL1 &= 0xefu;
    saveload_mode = SAVELOAD_MODE_LOAD;
    saveload_load_pending = 0u;
    platform_saveload_run_banked();
    result = PLATFORM_OK;
    tiles_restored = 0u;
    if (saveload_load_pending) {
        result = saveload_apply_pending();
        tiles_restored = 1u;
    }
    saveload_cleanup(result, tiles_restored);
}

void game_save_show(void) {
    uint8_t result;

    platform_look_cursor_hide();
    SAVELOAD_VIC_CTRL1 &= 0xefu;
    saveload_mode = SAVELOAD_MODE_SAVE;
    saveload_selected_slot = SAVELOAD_SLOT_NONE;
    platform_saveload_run_banked();
    result = PLATFORM_OK;

    if (saveload_selected_slot != SAVELOAD_SLOT_NONE) {
        platform_saveload_save_run_banked();
    }
    saveload_cleanup(result, 0u);
}

#pragma code-name (pop)
