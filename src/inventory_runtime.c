#include <stdint.h>
#include <string.h>

#include "game.h"

#define INVENTORY_BASE          ((uint8_t*)0xa4e9)
#define INVENTORY_MAX_BYTES     0x0ff0u
#define INVENTORY_HEADER_BYTES  16u
#define INVENTORY_EF_BANK       48u
#define INVENTORY_SCREEN        ((uint8_t*)0x0400)
#define INVENTORY_COLOR         ((uint8_t*)0xd800)
#define INVENTORY_VIC_CTRL1     (*(volatile uint8_t*)0xd011)

void platform_memory_game(void);
void platform_memory_kernal(void);
void platform_easyflash_copy_romh(void);
void raster_irq_suspend(void);
void raster_irq_resume(void);
void inventory_overlay_run_native(void);
uint16_t inventory_disk_load_native(void);
uint8_t __fastcall__ inventory_overlay_validate_native(uint16_t loaded_size);
extern uint8_t platform_ef_copy_bank;
extern uint16_t platform_ef_copy_offset;
extern uint16_t platform_ef_copy_destination;
extern uint16_t platform_ef_copy_size;

#pragma code-name (push, "UPPERCODE")

static uint8_t inventory_load_disk(void) {
    uint16_t size;

    platform_memory_kernal();
    size = inventory_disk_load_native();
    platform_memory_game();
    if (size == 0u) return PLATFORM_ERR_IO;
    return inventory_overlay_validate_native(size);
}

static uint8_t inventory_load_easyflash(void) {
    uint16_t size;

    platform_ef_copy_bank = INVENTORY_EF_BANK;
    platform_ef_copy_offset = 0u;
    platform_ef_copy_destination = (uint16_t)INVENTORY_BASE;
    platform_ef_copy_size = INVENTORY_HEADER_BYTES;
    platform_easyflash_copy_romh();
    size = (uint16_t)INVENTORY_BASE[6] |
           ((uint16_t)INVENTORY_BASE[7] << 8);
    if (size < INVENTORY_HEADER_BYTES || size > INVENTORY_MAX_BYTES) {
        return PLATFORM_ERR_FORMAT;
    }
    platform_ef_copy_bank = INVENTORY_EF_BANK;
    platform_ef_copy_offset = 0u;
    platform_ef_copy_destination = (uint16_t)INVENTORY_BASE;
    platform_ef_copy_size = size;
    platform_easyflash_copy_romh();
    return inventory_overlay_validate_native(size);
}

void game_inventory_show(void) {
    uint8_t result;

    platform_look_cursor_hide();
    INVENTORY_VIC_CTRL1 &= 0xefu;
    result = platform_storage == PLATFORM_STORAGE_EASYFLASH
                 ? inventory_load_easyflash() : inventory_load_disk();
    if (result == PLATFORM_OK) inventory_overlay_run_native();

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
