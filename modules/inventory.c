#include <stdint.h>

#include "game.h"
#include "story.h"

#define INVENTORY_SCREEN       ((uint8_t*)0xf800)
#define INVENTORY_COLOR        ((uint8_t*)0xd800)
#define INVENTORY_VIC_CTRL1    (*(volatile uint8_t*)0xd011)
#define INVENTORY_BODY_ROWS    23u
#define INVENTORY_COLUMN_ROWS  16u
#define INVENTORY_MESSAGE_ROW  23u

extern uint8_t game_inventory_draw_index;
extern uint8_t game_inventory_draw_type;
extern uint8_t game_inventory_draw_quantity;
extern uint8_t game_inventory_menu_count;
extern uint8_t game_inventory_menu_selected;

void game_inventory_draw_item_native(void);

static const uint8_t inventory_title[9] = {
    73u, 14u, 22u, 5u, 14u, 20u, 15u, 18u, 25u
};
static const uint8_t inventory_empty[7] = {
    40u, 5u, 13u, 16u, 20u, 25u, 41u
};
static const uint8_t inventory_cannot_use[20] = {
    25u, 15u, 21u, 32u, 3u, 1u, 14u, 14u, 15u, 20u,
    32u, 21u, 19u, 5u, 32u, 20u, 8u, 1u, 20u, 46u
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

static void inventory_cursor_draw(uint8_t index, uint8_t visible) {
    uint8_t row;
    uint16_t cursor;

    row = index;
    cursor = 80u;
    if (row >= INVENTORY_COLUMN_ROWS) {
        row -= INVENTORY_COLUMN_ROWS;
        cursor += 20u;
    }
    cursor += (uint16_t)row * 40u;
    INVENTORY_SCREEN[cursor] =
        platform_text_screen_code(visible ? '>' : ' ');
    INVENTORY_COLOR[cursor] = visible ? 1u : 0u;
}

static uint8_t inventory_slot_for_index(uint8_t selected) {
    uint8_t i;
    uint8_t index;

    index = 0u;
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        if (game_state.inventory[i].type == 0u) continue;
        if (index == selected) return i;
        ++index;
    }
    return 0xffu;
}

static void inventory_show_cannot_use(void) {
    uint8_t i;

    for (i = 0u; i < 80u; ++i) {
        INVENTORY_SCREEN[INVENTORY_MESSAGE_ROW * 40u + i] = 32u;
        INVENTORY_COLOR[INVENTORY_MESSAGE_ROW * 40u + i] = 1u;
    }
    copy_screen(INVENTORY_SCREEN + INVENTORY_MESSAGE_ROW * 40u,
                inventory_cannot_use, sizeof(inventory_cannot_use));
}

static void inventory_draw(void) {
    uint8_t i;

    clear_cells((uint16_t)INVENTORY_BODY_ROWS * 40u, 1u);
    copy_screen(INVENTORY_SCREEN + 15u, inventory_title,
                sizeof(inventory_title));

    game_inventory_menu_count = 0u;
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        if (game_state.inventory[i].type == 0u) continue;
        game_inventory_draw_index = game_inventory_menu_count;
        game_inventory_draw_type = game_state.inventory[i].type;
        game_inventory_draw_quantity = game_state.inventory[i].quantity;
        game_inventory_draw_item_native();
        ++game_inventory_menu_count;
    }

    if (game_inventory_menu_count == 0u) {
        game_inventory_menu_selected = 0u;
        copy_screen(INVENTORY_SCREEN + 81u, inventory_empty,
                    sizeof(inventory_empty));
        return;
    }
    if (game_inventory_menu_selected >= game_inventory_menu_count) {
        game_inventory_menu_selected = game_inventory_menu_count - 1u;
    }
    inventory_cursor_draw(game_inventory_menu_selected, 1u);
}

void inventory_banked_run(void) {
    uint8_t key;
    uint8_t slot;
    uint8_t old_selected;

    game_inventory_menu_selected = 0u;
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

        old_selected = game_inventory_menu_selected;
        if (key == PLATFORM_KEY_CURSOR_UP) {
            if (game_inventory_menu_selected != 0u &&
                game_inventory_menu_selected != INVENTORY_COLUMN_ROWS) {
                --game_inventory_menu_selected;
            }
        } else if (key == PLATFORM_KEY_CURSOR_DOWN) {
            if ((game_inventory_menu_selected & 0x0fu) + 1u < INVENTORY_COLUMN_ROWS &&
                game_inventory_menu_selected + 1u < game_inventory_menu_count) {
                ++game_inventory_menu_selected;
            }
        } else if (key == PLATFORM_KEY_CURSOR_LEFT) {
            if (game_inventory_menu_selected >= INVENTORY_COLUMN_ROWS) {
                game_inventory_menu_selected -= INVENTORY_COLUMN_ROWS;
            }
        } else if (key == PLATFORM_KEY_CURSOR_RIGHT) {
            if (game_inventory_menu_selected < INVENTORY_COLUMN_ROWS &&
                game_inventory_menu_selected + INVENTORY_COLUMN_ROWS <
                    game_inventory_menu_count) {
                game_inventory_menu_selected += INVENTORY_COLUMN_ROWS;
            }
        } else if (key == PLATFORM_KEY_USE &&
                   game_inventory_menu_count != 0u) {
            slot = inventory_slot_for_index(game_inventory_menu_selected);
            if (slot != 0xffu &&
                story_use_inventory(slot) == GAME_USE_DEFAULT) {
                inventory_show_cannot_use();
            }
            inventory_draw();
            wait_key_release();
            continue;
        } else {
            wait_key_release();
            continue;
        }

        if (game_inventory_menu_selected != old_selected) {
            inventory_cursor_draw(old_selected, 0u);
            inventory_cursor_draw(game_inventory_menu_selected, 1u);
        }
        wait_key_release();
    }

    wait_key_release();
    INVENTORY_VIC_CTRL1 &= 0xefu;
    clear_cells(1000u, 0u);
}
