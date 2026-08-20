#include <string.h>

#include "game.h"
#include "world.h"

#pragma code-name ("UPPERCODE")
#pragma rodata-name ("UPPERRODATA")

#define GAME_SCREEN_RAM ((uint8_t*)0x0400)
#define GAME_COLOR_RAM  ((uint8_t*)0xd800)
#define GAME_VIC_CTRL1  (*(volatile uint8_t*)0xd011)

uint8_t game_inventory_draw_index;
uint8_t game_inventory_draw_type;
uint8_t game_inventory_draw_quantity;
void game_inventory_draw_item_native(void);

static const uint8_t inventory_title[9] = {
    73u, 14u, 22u, 5u, 14u, 20u, 15u, 18u, 25u
};
static const uint8_t inventory_empty[7] = {
    40u, 5u, 13u, 16u, 20u, 25u, 41u
};

uint8_t game_take_direction(uint8_t direction) {
    PlatformObject* object;
    const PlatformObjectType* object_type;
    uint8_t tile_x;
    uint8_t tile_y;
    uint8_t slot;
    uint8_t result;

    if (platform_player == 0 || direction > PLATFORM_DIRECTION_SOUTH) {
        return PLATFORM_ERR_ARGUMENT;
    }
    tile_x = platform_player->x >> 1;
    tile_y = platform_player->y >> 1;
    switch (direction) {
        case PLATFORM_DIRECTION_NORTH:
            if (tile_y == 0u) goto nothing;
            --tile_y;
            break;
        case PLATFORM_DIRECTION_EAST:
            if (++tile_x >= PLATFORM_MAP_WIDTH) goto nothing;
            break;
        case PLATFORM_DIRECTION_WEST:
            if (tile_x == 0u) goto nothing;
            --tile_x;
            break;
        default:
            if (++tile_y >= PLATFORM_MAP_HEIGHT) goto nothing;
            break;
    }

    slot = 0u;
    do {
        object = &platform_room.objects[slot];
        if (object->type != 0u && object != platform_player &&
            (object->x >> 1) == tile_x && (object->y >> 1) == tile_y) {
            object_type = platform_object_type_get(object->type);
            if ((object_type->reserved[0] & PLATFORM_OBJECT_FLAG_ACTOR) == 0u) {
                result = game_take_object(slot);
                game_text_write(PLATFORM_TEXT_LINE_TOP,
                                result == PLATFORM_OK ? "Taken."
                                                      : "You cannot take that.", 1u);
                platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
                return result;
            }
        }
        ++slot;
    } while (slot != 0u);

nothing:
    game_text_write(PLATFORM_TEXT_LINE_TOP, "Nothing to take.", 1u);
    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
    return PLATFORM_ERR_NOT_FOUND;
}

void game_inventory_show(void) {
    uint8_t i;
    uint8_t shown;
    uint8_t key;

    platform_overlay_hide();
    GAME_VIC_CTRL1 &= 0xefu;
    platform_text_screen_enter();
    memset(GAME_SCREEN_RAM, platform_text_screen_code(' '), 1000u);
    memset(GAME_COLOR_RAM, 1u, 1000u);
    memcpy(GAME_SCREEN_RAM + 15u, inventory_title, sizeof(inventory_title));
    shown = 0u;
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        if (game_state.inventory[i].type == 0u) continue;
        game_inventory_draw_index = shown;
        game_inventory_draw_type = game_state.inventory[i].type;
        game_inventory_draw_quantity = game_state.inventory[i].quantity;
        game_inventory_draw_item_native();
        ++shown;
    }
    if (shown == 0u) {
        memcpy(GAME_SCREEN_RAM + 81u, inventory_empty, sizeof(inventory_empty));
    }
    GAME_VIC_CTRL1 |= 0x10u;

    do {
        platform_wait_frame();
        key = platform_input_poll();
    } while (key != 0u);
    do {
        platform_wait_frame();
        key = platform_input_poll();
    } while (key == 0u);
    do {
        platform_wait_frame();
        key = platform_input_poll();
    } while (key != 0u);

    platform_text_screen_leave();
    platform_room_draw(&platform_room, platform_player);
}

uint8_t game_inventory_count(uint8_t type) {
    uint8_t i;
    uint8_t total;
    if (type == 0u) return 0u;
    total = 0u;
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        if (game_state.inventory[i].type == type) {
            if ((uint16_t)total + game_state.inventory[i].quantity > 255u) {
                return 255u;
            }
            total += game_state.inventory[i].quantity;
        }
    }
    return total;
}

uint8_t game_inventory_has(uint8_t type, uint8_t quantity) {
    return game_inventory_count(type) >= quantity;
}

uint8_t game_inventory_add(uint8_t type, uint8_t quantity) {
    uint8_t i;
    uint16_t combined;
    if (type == 0u || quantity == 0u) return PLATFORM_ERR_ARGUMENT;
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        if (game_state.inventory[i].type == type) {
            combined = (uint16_t)game_state.inventory[i].quantity + quantity;
            game_state.inventory[i].quantity = combined > 255u ? 255u : (uint8_t)combined;
            return PLATFORM_OK;
        }
    }
    for (i = 0u; i < GAME_INVENTORY_SLOTS; ++i) {
        if (game_state.inventory[i].type == 0u) {
            game_state.inventory[i].type = type;
            game_state.inventory[i].quantity = quantity;
            return PLATFORM_OK;
        }
    }
    return PLATFORM_ERR_FULL;
}

uint8_t game_inventory_remove(uint8_t type, uint8_t quantity) {
    uint8_t i;
    if (quantity == 0u || !game_inventory_has(type, quantity)) {
        return PLATFORM_ERR_NOT_FOUND;
    }
    for (i = 0u; i < GAME_INVENTORY_SLOTS && quantity != 0u; ++i) {
        if (game_state.inventory[i].type != type) continue;
        if (game_state.inventory[i].quantity > quantity) {
            game_state.inventory[i].quantity -= quantity;
            quantity = 0u;
        } else {
            quantity -= game_state.inventory[i].quantity;
            game_state.inventory[i].type = 0u;
            game_state.inventory[i].quantity = 0u;
        }
    }
    return PLATFORM_OK;
}

uint8_t game_take_object(uint8_t slot) {
    PlatformObject* object;
    PlatformObject original;
    const PlatformObjectType* object_type;
    uint8_t type;
    uint8_t result;

    object = &platform_room.objects[slot];
    type = object->type;
    if (type == 0u || object == platform_player) return PLATFORM_ERR_ARGUMENT;
    object_type = platform_object_type_get(type);
    if ((object_type->reserved[0] & PLATFORM_OBJECT_FLAG_ACTOR) != 0u) {
        return PLATFORM_ERR_ARGUMENT;
    }
    original = *object;
    result = game_inventory_add(type, 1u);
    if (result != PLATFORM_OK) return result;
    result = platform_room_object_remove(&platform_room, slot, platform_player);
    if (result == PLATFORM_OK) result = game_world_capture_current();
    if (result != PLATFORM_OK) {
        *object = original;
        (void)game_inventory_remove(type, 1u);
        platform_room_draw(&platform_room, platform_player);
    }
    return result;
}

uint8_t game_health(void) { return game_state.health; }
uint8_t game_mana(void) { return game_state.mana; }

void game_heal(uint8_t amount) {
    uint16_t health;
    health = (uint16_t)game_state.health + amount;
    game_state.health = health > game_state.maximum_health
                            ? game_state.maximum_health : (uint8_t)health;
}

void game_damage(uint8_t amount) {
    game_state.health = amount >= game_state.health
                            ? 0u : game_state.health - amount;
}

uint8_t game_spend_mana(uint8_t amount) {
    if (amount > game_state.mana) return 0u;
    game_state.mana -= amount;
    return 1u;
}

void game_text_write(uint8_t line, const char* text, uint8_t color) {
    platform_text_clear_line(line);
    platform_text_write_line(line, 0u, text, color);
}

void game_text_write_room(uint8_t line, uint8_t text_offset, uint8_t color) {
    platform_text_clear_line(line);
    platform_text_write_room_line(&platform_room, line, 0u, text_offset, color);
}

void game_dialog_show(const char* line0, const char* line1,
                      const char* line2, uint8_t color) {
    (void)platform_overlay_show_text(8u, 16u, line0, line1, line2, color);
}

void game_dialog_show_room(uint8_t line0, uint8_t line1,
                           uint8_t line2, uint8_t color) {
    (void)platform_overlay_show(&platform_room, 8u, 16u,
                                line0, line1, line2, color);
}

uint8_t game_transition_request(uint8_t room, uint8_t x, uint8_t y) {
    if (x >= PLATFORM_MAP_CHAR_WIDTH || y >= PLATFORM_MAP_CHAR_HEIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }
    if (game_state.pending_transition) return PLATFORM_ERR_FULL;
    game_state.pending_room = room;
    game_state.pending_x = x;
    game_state.pending_y = y;
    game_state.pending_transition = 1u;
    return PLATFORM_OK;
}
