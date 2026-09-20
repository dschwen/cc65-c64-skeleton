#include <stdint.h>

#include "game.h"
#include "story.h"
#include "banks.h"

/* Resident, not bank-local: game_inventory_show() below (called directly
 * with far_call_bank() from src/main.c, before this bank is even paged in)
 * and this bank's own drawing code both touch these, so they must survive
 * the bank switch either way (see memmap.h's header comment). This used to
 * be src/inventory_runtime.c's job when that file's game_inventory_show()
 * was itself resident; now that function lives in this same bank (see its
 * own comment below), so only its state declarations still need a home
 * outside any bank pragma. */
uint8_t game_inventory_draw_index;
uint8_t game_inventory_draw_type;
uint8_t game_inventory_draw_quantity;
uint8_t game_inventory_menu_count;
uint8_t game_inventory_menu_selected;

#pragma section( inventory_code, 0 )
#pragma section( inventory_data, 0 )
#pragma region( inventory_bank, 0x8000, 0xc000, , BANK_INVENTORY,
                 { inventory_data, inventory_code } )
#pragma code(inventory_code)
#pragma data(inventory_data)

#define INVENTORY_SCREEN       ((uint8_t*)0xf800)
#define INVENTORY_COLOR        ((uint8_t*)0xd800)
#define INVENTORY_VIC_CTRL1    (*(volatile uint8_t*)0xd011)
#define INVENTORY_BODY_ROWS    23u
#define INVENTORY_COLUMN_ROWS  16u
#define INVENTORY_MESSAGE_ROW  23u
#define INVENTORY_SCREEN_LEFT  (INVENTORY_SCREEN + 2u * 40u + 1u)
#define INVENTORY_SCREEN_RIGHT (INVENTORY_SCREEN + 2u * 40u + 21u)
#define INVENTORY_NAME_LENGTH  14u

/* Same PETSCII-to-screencode conversion as text.c's write_character() -
 * duplicated rather than shared because that one is file-local static and
 * this module lives in its own bank. platform_text_screen_code() is a
 * *different* mapping (tuned for this game's custom charset's uppercase
 * placement) and not a substitute - matching the original inventory_draw.s
 * exactly here is what matters, not consistency with that function. */
static uint8_t inventory_screen_code(uint8_t ch) {
    if (ch >= 0x41u && ch < 0x5bu) return (uint8_t)(ch - 0x40u);
    if (ch >= 0xc1u && ch < 0xdbu) return (uint8_t)(ch - 0x80u);
    return ch;
}

/* Draw one compact inventory entry (quantity, then up to 14 name chars).
 * Ported from modules/inventory_draw.s. Columns zero and twenty are
 * deliberately left free for the selection cursor drawn below. */
static void game_inventory_draw_item_native(void) {
    uint8_t index = game_inventory_draw_index;
    uint8_t row;
    uint8_t* dest;
    uint8_t quantity = game_inventory_draw_quantity;
    uint8_t hundreds = 0u;
    uint8_t tens = 0u;
    const PlatformObjectTypeInfo* info;
    uint8_t i;
    uint8_t ch;

    if (index < 16u) {
        row = index;
        dest = INVENTORY_SCREEN_LEFT;
    } else {
        row = (uint8_t)(index - 16u);
        dest = INVENTORY_SCREEN_RIGHT;
    }
    dest += (uint16_t)row * 40u;

    while (quantity >= 100u) { quantity = (uint8_t)(quantity - 100u); ++hundreds; }
    while (quantity >= 10u) { quantity = (uint8_t)(quantity - 10u); ++tens; }
    if (hundreds != 0u) {
        *dest++ = inventory_screen_code((uint8_t)('0' + hundreds));
    }
    if (tens != 0u || hundreds != 0u) {
        *dest++ = inventory_screen_code((uint8_t)('0' + tens));
    }
    *dest++ = inventory_screen_code((uint8_t)('0' + quantity));
    *dest++ = inventory_screen_code(' ');

    info = platform_object_type_info_get(game_inventory_draw_type);
    for (i = 0u; i < INVENTORY_NAME_LENGTH; ++i) {
        ch = (uint8_t)info->name[i];
        if (ch == 0u) break;
        *dest++ = inventory_screen_code(ch);
    }
}

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

void raster_irq_suspend(void);
void raster_irq_resume(void);
void inventory_entry(void);

/* game_inventory_show()'s entry/exit screen handling stays in this same
 * bank rather than resident: moving it out of src/inventory_runtime.c and
 * in here (an ordinary intra-bank call to inventory_entry() below, no
 * far_call_bank() needed since both are already in BANK_INVENTORY) is what
 * let the resident "main" region (memmap.h) fit again once this file's own
 * screen-transition code was added - see banks.h's BANK_DISK_IO comment
 * for the matching reason that module moved out of "main" too. Called
 * directly with far_call_bank(BANK_INVENTORY, game_inventory_show) from
 * src/main.c. */
__noinline void game_inventory_show(void) {
    platform_look_cursor_hide();
    INVENTORY_VIC_CTRL1 &= 0xefu;
    inventory_entry();

    INVENTORY_VIC_CTRL1 &= 0xefu;
    raster_irq_suspend();
    clear_cells(1000u, 0u);
    platform_text_screen_leave();
    platform_room_draw(&platform_room, platform_player);
    raster_irq_resume();
    INVENTORY_VIC_CTRL1 |= 0x10u;
}

__noinline void inventory_entry(void) {
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

#pragma code(code)
#pragma data(data)
