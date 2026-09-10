#include <stdint.h>
#include <string.h>

#include "game.h"

#define INVENTORY_SCREEN        ((uint8_t*)0x0400)
#define INVENTORY_COLOR         ((uint8_t*)0xd800)
#define INVENTORY_VIC_CTRL1     (*(volatile uint8_t*)0xd011)

void raster_irq_suspend(void);
void raster_irq_resume(void);
void platform_inventory_run_banked(void);

/* Mutable state used while inventory code executes from cartridge ROM. Keep
 * it in the explicitly reserved always-visible tail after GameState, not in
 * the module's own BSS (which would be ROM at run time). The 32-byte selected-
 * slot cache was removed; the banked module derives a selected slot on demand.
 */
#pragma bss-name (push, "GAMESTATE")
uint8_t game_inventory_draw_index;
uint8_t game_inventory_draw_type;
uint8_t game_inventory_draw_quantity;
uint8_t game_inventory_menu_count;
uint8_t game_inventory_menu_selected;
#pragma bss-name (pop)

#pragma code-name (push, "UPPERCODE")

void game_inventory_show(void) {
    platform_look_cursor_hide();
    INVENTORY_VIC_CTRL1 &= 0xefu;
    platform_inventory_run_banked();

    INVENTORY_VIC_CTRL1 &= 0xefu;
    raster_irq_suspend();
    memset(INVENTORY_SCREEN, platform_text_screen_code(' '), 1000u);
    memset(INVENTORY_COLOR, 0, 1000u);
    platform_text_screen_leave();
    platform_room_draw(&platform_room, platform_player);
    raster_irq_resume();
    INVENTORY_VIC_CTRL1 |= 0x10u;
}

#pragma code-name (pop)
