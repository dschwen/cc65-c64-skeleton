#include <stdint.h>
#include <string.h>

#include "game.h"
#include "world.h"

#define SAVELOAD_EF_BANK      48u
#define SAVELOAD_MAGIC_0      0x53u /* 'S' */
#define SAVELOAD_MAGIC_1      0x4cu /* 'L' */
/* Save-detail overlay ("SV": name entry + encode + disk write), separate
 * from the browse/load overlay above because both together would not fit
 * the shared 4119-byte $A4E9 window. Stored in bank 47 ROMH, otherwise
 * unused (bank 47 ROML holds the object-type cold table). */
#define SAVELOAD_SAVE_EF_BANK 47u
#define SAVELOAD_SAVE_MAGIC_0 0x53u /* 'S' */
#define SAVELOAD_SAVE_MAGIC_1 0x56u /* 'V' */
#define SAVELOAD_SCREEN     ((uint8_t*)0x0400)
#define SAVELOAD_COLOR      ((uint8_t*)0xd800)
#define SAVELOAD_VIC_CTRL1  (*(volatile uint8_t*)0xd011)
#define SAVE_RECORD_RAM     ((const uint8_t*)0xa000)
#define SAVE_HEADER_BYTES   16u
#define SAVE_PREFIX_BYTES   130u
#define SAVE_DELTA_BYTES    5u
#define SAVE_MAX_DELTAS     200u

void raster_irq_suspend(void);
void raster_irq_resume(void);
void platform_overlay_run_native(void);
uint8_t __fastcall__ platform_overlay_load(uint8_t bank, uint8_t use_romh,
                                           uint16_t offset, uint8_t magic0,
                                           uint8_t magic1);

uint8_t saveload_overlay_mode;
uint8_t saveload_selected_slot;
uint8_t saveload_load_pending;

#pragma code-name (push, "HIGHCODE")

static uint16_t save_get16(const uint8_t* p) {
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

#pragma code-name (pop)
#pragma code-name (push, "LOWCODE")

/* Apply only after the browse overlay has returned. platform_room_enter()
 * stages room code at $A4E9 and room data at $A000, so calling it from the
 * overlay would overwrite both the executing code and this record. */
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
        p[28] > p[29] || p[30] > p[31]) return PLATFORM_ERR_FORMAT;

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

    game_world_disable_store_hook();
    game_world_delta_count = 0u;
    memcpy(game_world_deltas, p + SAVE_PREFIX_BYTES, delta_bytes);
    game_world_delta_count = delta_count;

    result = platform_room_enter(room, type, x, y);
    game_world_enable_store_hook();
    if (result != PLATFORM_OK) return result;
    game_player_sync_from_platform();
    game_entry_reason = GAME_ENTRY_LOAD;
    game_enter_room();
    game_enter_tile();
    return PLATFORM_OK;
}

#pragma code-name (pop)

#pragma code-name (push, "HIGHCODE")

static void saveload_cleanup(uint8_t result) {
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

    platform_look_cursor_hide();
    SAVELOAD_VIC_CTRL1 &= 0xefu;
    saveload_overlay_mode = SAVELOAD_MODE_LOAD;
    saveload_load_pending = 0u;
    result = platform_overlay_load(SAVELOAD_EF_BANK, 0u, 0u,
                                   SAVELOAD_MAGIC_0, SAVELOAD_MAGIC_1);
    if (result == PLATFORM_OK) platform_overlay_run_native();
    if (result == PLATFORM_OK && saveload_load_pending) {
        result = saveload_apply_pending();
    }
    saveload_cleanup(result);
}

void game_save_show(void) {
    uint8_t result;

    platform_look_cursor_hide();
    SAVELOAD_VIC_CTRL1 &= 0xefu;
    saveload_overlay_mode = SAVELOAD_MODE_SAVE;
    saveload_selected_slot = SAVELOAD_SLOT_NONE;
    result = platform_overlay_load(SAVELOAD_EF_BANK, 0u, 0u,
                                   SAVELOAD_MAGIC_0, SAVELOAD_MAGIC_1);
    if (result == PLATFORM_OK) platform_overlay_run_native();

    if (result == PLATFORM_OK && saveload_selected_slot != SAVELOAD_SLOT_NONE) {
        result = platform_overlay_load(SAVELOAD_SAVE_EF_BANK, 1u, 0u,
                                       SAVELOAD_SAVE_MAGIC_0,
                                       SAVELOAD_SAVE_MAGIC_1);
        if (result == PLATFORM_OK) platform_overlay_run_native();
    }
    saveload_cleanup(result);
}

#pragma code-name (pop)
