#include <stdint.h>

#include "game.h"
#include "story.h"

#define INVENTORY_SCREEN       ((uint8_t*)0x0400)
#define INVENTORY_COLOR        ((uint8_t*)0xd800)
#define INVENTORY_VIC_CTRL1    (*(volatile uint8_t*)0xd011)
#define INVENTORY_BODY_ROWS    23u
#define INVENTORY_COLUMN_ROWS  16u

uint8_t game_inventory_draw_index;
uint8_t game_inventory_draw_type;
uint8_t game_inventory_draw_quantity;

void game_inventory_draw_item_native(void);

static uint8_t inventory_slots[GAME_INVENTORY_SLOTS];
static uint8_t inventory_count;
static uint8_t inventory_selected;

static const uint8_t inventory_title[9] = {
    73u, 14u, 22u, 5u, 14u, 20u, 15u, 18u, 25u
};
static const uint8_t inventory_empty[7] = {
    40u, 5u, 13u, 16u, 20u, 25u, 41u
};

static void clear_cells(uint16_t count, uint8_t color) {
    uint16_t i;
    uint8_t blank;

    blank = platform_text_screen_code(' ');
    for (i = 0u; i < count; ++i) {
        INVENTORY_SCREEN[i] = blank;
        INVENTORY_COLOR[i] = color;
    }
}

static void copy_screen(uint8_t* destination, const uint8_t* source,
                        uint8_t size) {
    while (size-- != 0u) *destination++ = *source++;
}

static void wait_key_release(void) {
    do {
        platform_wait_frame();
    } while (platform_input_poll() != 0u);
}

static void inventory_draw(void) {
    uint8_t i;
    uint8_t row;
    uint16_t cursor;

    clear_cells((uint16_t)INVENTORY_BODY_ROWS * 40u, 1u);
    copy_screen(INVENTORY_SCREEN + 15u, inventory_title,
                sizeof(inventory_title));

    inventory_count = 0u;
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        if (game_state.inventory[i].type == 0u) continue;
        inventory_slots[inventory_count] = i;
        game_inventory_draw_index = inventory_count;
        game_inventory_draw_type = game_state.inventory[i].type;
        game_inventory_draw_quantity = game_state.inventory[i].quantity;
        game_inventory_draw_item_native();
        ++inventory_count;
    }

    if (inventory_count == 0u) {
        inventory_selected = 0u;
        copy_screen(INVENTORY_SCREEN + 81u, inventory_empty,
                    sizeof(inventory_empty));
        return;
    }
    if (inventory_selected >= inventory_count) {
        inventory_selected = inventory_count - 1u;
    }
    row = inventory_selected;
    cursor = 80u;
    if (row >= INVENTORY_COLUMN_ROWS) {
        row -= INVENTORY_COLUMN_ROWS;
        cursor += 20u;
    }
    cursor += (uint16_t)row * 40u;
    INVENTORY_SCREEN[cursor] = platform_text_screen_code('>');
    INVENTORY_COLOR[cursor] = 1u;
}

void inventory_overlay_run(void) {
    uint8_t key;
    uint8_t slot;

    inventory_selected = 0u;
    platform_text_screen_enter();
    clear_cells(1000u, 1u);
    inventory_draw();
    INVENTORY_VIC_CTRL1 |= 0x10u;
    wait_key_release();

    for (;;) {
        platform_wait_frame();
        key = platform_input_poll();
        if (key == 0u) continue;
        if (key == PLATFORM_KEY_INVENTORY) break;

        if (key == PLATFORM_KEY_CURSOR_UP) {
            if (inventory_selected != 0u &&
                inventory_selected != INVENTORY_COLUMN_ROWS) {
                --inventory_selected;
            }
        } else if (key == PLATFORM_KEY_CURSOR_DOWN) {
            if ((inventory_selected & 0x0fu) + 1u < INVENTORY_COLUMN_ROWS &&
                inventory_selected + 1u < inventory_count) {
                ++inventory_selected;
            }
        } else if (key == PLATFORM_KEY_CURSOR_LEFT) {
            if (inventory_selected >= INVENTORY_COLUMN_ROWS) {
                inventory_selected -= INVENTORY_COLUMN_ROWS;
            }
        } else if (key == PLATFORM_KEY_CURSOR_RIGHT) {
            if (inventory_selected < INVENTORY_COLUMN_ROWS &&
                inventory_selected + INVENTORY_COLUMN_ROWS < inventory_count) {
                inventory_selected += INVENTORY_COLUMN_ROWS;
            }
        } else if (key == PLATFORM_KEY_USE && inventory_count != 0u) {
            slot = inventory_slots[inventory_selected];
            if (story_use_inventory(slot) == GAME_USE_DEFAULT) {
                game_text_write(PLATFORM_TEXT_LINE_TOP,
                                "You cannot use that.", 1u);
            }
        } else {
            wait_key_release();
            continue;
        }

        inventory_draw();
        wait_key_release();
    }

    wait_key_release();
    INVENTORY_VIC_CTRL1 &= 0xefu;
    clear_cells(1000u, 0u);
}
