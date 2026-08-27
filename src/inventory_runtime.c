#include <stdint.h>
#include <string.h>

#include "game.h"

#define INVENTORY_EF_BANK       48u
#define INVENTORY_MAGIC_0       0x49u /* 'I' */
#define INVENTORY_MAGIC_1       0x55u /* 'U' */
#define INVENTORY_SCREEN        ((uint8_t*)0x0400)
#define INVENTORY_COLOR         ((uint8_t*)0xd800)
#define INVENTORY_VIC_CTRL1     (*(volatile uint8_t*)0xd011)

void raster_irq_suspend(void);
void raster_irq_resume(void);
void platform_overlay_run_native(void);
uint8_t platform_overlay_load(uint8_t bank, uint8_t use_romh,
                              uint8_t magic0, uint8_t magic1);

#pragma code-name (push, "UPPERCODE")

void game_inventory_show(void) {
    uint8_t result;

    platform_look_cursor_hide();
    INVENTORY_VIC_CTRL1 &= 0xefu;
    result = platform_overlay_load(INVENTORY_EF_BANK, 1u,
                                   INVENTORY_MAGIC_0, INVENTORY_MAGIC_1);
    if (result == PLATFORM_OK) platform_overlay_run_native();

    INVENTORY_VIC_CTRL1 &= 0xefu;
    raster_irq_suspend();
    memset(INVENTORY_SCREEN, platform_text_screen_code(' '), 1000u);
    memset(INVENTORY_COLOR, 0, 1000u);
    platform_text_screen_leave();
    platform_room_draw(&platform_room, platform_player);
    raster_irq_resume();
    INVENTORY_VIC_CTRL1 |= 0x10u;
    if (result != PLATFORM_OK) {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "Inventory error.", 1u);
    }
}

#pragma code-name (pop)
