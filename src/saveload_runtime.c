#include <stdint.h>
#include <string.h>

#include "game.h"

#define SAVELOAD_EF_BANK      48u
#define SAVELOAD_MAGIC_0      0x53u /* 'S' */
#define SAVELOAD_MAGIC_1      0x4cu /* 'L' */
/* Save-detail overlay ("SV": name entry + encode + disk write), separate
 * from the browse/load overlay above because both together would not fit
 * the shared 4080-byte $A4E9 window. Stored in bank 47 ROMH, otherwise
 * unused (bank 47 ROML holds the object-type cold table). */
#define SAVELOAD_SAVE_EF_BANK 47u
#define SAVELOAD_SAVE_MAGIC_0 0x53u /* 'S' */
#define SAVELOAD_SAVE_MAGIC_1 0x56u /* 'V' */
#define SAVELOAD_SCREEN     ((uint8_t*)0x0400)
#define SAVELOAD_COLOR      ((uint8_t*)0xd800)
#define SAVELOAD_VIC_CTRL1  (*(volatile uint8_t*)0xd011)

void raster_irq_suspend(void);
void raster_irq_resume(void);
void platform_overlay_run_native(void);
uint8_t platform_overlay_load(uint8_t bank, uint8_t use_romh,
                              uint8_t magic0, uint8_t magic1);

uint8_t saveload_overlay_mode;
uint8_t saveload_selected_slot;

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
    result = platform_overlay_load(SAVELOAD_EF_BANK, 0u,
                                   SAVELOAD_MAGIC_0, SAVELOAD_MAGIC_1);
    if (result == PLATFORM_OK) platform_overlay_run_native();
    saveload_cleanup(result);
}

void game_save_show(void) {
    uint8_t result;

    platform_look_cursor_hide();
    SAVELOAD_VIC_CTRL1 &= 0xefu;
    saveload_overlay_mode = SAVELOAD_MODE_SAVE;
    saveload_selected_slot = SAVELOAD_SLOT_NONE;
    result = platform_overlay_load(SAVELOAD_EF_BANK, 0u,
                                   SAVELOAD_MAGIC_0, SAVELOAD_MAGIC_1);
    if (result == PLATFORM_OK) platform_overlay_run_native();

    if (result == PLATFORM_OK && saveload_selected_slot != SAVELOAD_SLOT_NONE) {
        result = platform_overlay_load(SAVELOAD_SAVE_EF_BANK, 1u,
                                       SAVELOAD_SAVE_MAGIC_0,
                                       SAVELOAD_SAVE_MAGIC_1);
        if (result == PLATFORM_OK) platform_overlay_run_native();
    }
    saveload_cleanup(result);
}

#pragma code-name (pop)
