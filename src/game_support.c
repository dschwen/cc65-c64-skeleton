#include "game.h"
#include "world.h"

#pragma code-name ("UPPERCODE")
#pragma rodata-name ("UPPERRODATA")

void __fastcall__ platform_text_output_native(const char* text);
uint8_t platform_text_output_color;
uint8_t platform_text_output_line;

/*
 * Count/select takeable (non-actor, non-PLATFORM_OBJECT_FLAG_NOT_TAKEABLE)
 * objects intersecting a tile. any_present, if non-null, is set to 1 if any
 * non-actor object intersects the tile regardless of takeability, so a
 * caller can tell "nothing here" from "something here, but not takeable"
 * even when the takeable count is zero.
 */
#pragma code-name (push, "HIGHCODE")
static uint8_t take_object_find(uint8_t tile_x, uint8_t tile_y,
                                uint8_t wanted, uint8_t* found_slot,
                                uint8_t* any_present) {
    PlatformObject* object;
    const PlatformObjectTypeInfo* object_info;
    uint16_t slot;
    uint16_t limit;
    uint8_t count;

    count = 0u;
    limit = platform_room_object_limit(&platform_room);
    for (slot = 0u; slot < limit; ++slot) {
        object = &platform_room.objects[slot];
        if (object->type != 0u && object != platform_player &&
            platform_object_intersects_tile(object, tile_x, tile_y)) {
            object_info = platform_object_type_info_get(object->type);
            if ((object_info->flags & PLATFORM_OBJECT_FLAG_ACTOR) != 0u) {
                continue;
            }
            if (any_present != 0) *any_present = 1u;
            if ((object_info->flags & PLATFORM_OBJECT_FLAG_NOT_TAKEABLE) == 0u) {
                if (count == wanted && found_slot != 0) *found_slot = (uint8_t)slot;
                ++count;
            }
        }
    }
    return count;
}
#pragma code-name (pop)

uint8_t game_take_tile(uint8_t tile_x, uint8_t tile_y) {
    uint8_t count;
    uint8_t selected;
    uint8_t slot;
    uint8_t key;
    uint8_t result;
    uint8_t taken_type;
    uint8_t any_present;

    if (tile_x >= PLATFORM_MAP_WIDTH || tile_y >= PLATFORM_MAP_HEIGHT) {
        return PLATFORM_ERR_ARGUMENT;
    }
    any_present = 0u;
    count = take_object_find(tile_x, tile_y, 0u, &slot, &any_present);
    if (count == 0u) {
        game_text_write(PLATFORM_TEXT_LINE_TOP,
                        any_present ? "I cannot take this." : "Nothing to take.",
                        1u);
        return PLATFORM_ERR_NOT_FOUND;
    }

    selected = 0u;
    if (count > 1u) {
        platform_object_take_prompt(platform_room.objects[slot].type, 1u);
        for (;;) {
            platform_wait_frame();
            platform_look_cursor_tick();
            key = platform_input_poll();
            if (key == PLATFORM_KEY_CURSOR_LEFT ||
                key == PLATFORM_KEY_CURSOR_UP) {
                selected = selected == 0u ? count - 1u : selected - 1u;
            } else if (key == PLATFORM_KEY_CURSOR_RIGHT ||
                       key == PLATFORM_KEY_CURSOR_DOWN) {
                selected = selected + 1u == count ? 0u : selected + 1u;
            } else if (key == PLATFORM_KEY_ENTER) {
                break;
            } else if (key == PLATFORM_KEY_TAKE) {
                platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
                platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
                return PLATFORM_ERR_BLOCKED;
            } else {
                continue;
            }
            (void)take_object_find(tile_x, tile_y, selected, &slot, 0);
            platform_object_take_prompt(platform_room.objects[slot].type, 1u);
        }
        (void)take_object_find(tile_x, tile_y, selected, &slot, 0);
    }

    taken_type = platform_room.objects[slot].type;
    result = game_take_object(slot);
    if (result == PLATFORM_OK) {
        platform_object_taken_message(taken_type, 1u);
    } else {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "You cannot take that.", 1u);
    }
    return result;
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
    const PlatformObjectTypeInfo* object_info;
    uint8_t type;
    uint8_t result;

    object = &platform_room.objects[slot];
    type = object->type;
    if (type == 0u || object == platform_player) return PLATFORM_ERR_ARGUMENT;
    object_info = platform_object_type_info_get(type);
    if ((object_info->flags &
         (PLATFORM_OBJECT_FLAG_ACTOR | PLATFORM_OBJECT_FLAG_NOT_TAKEABLE)) != 0u) {
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
    if (text == 0 || line > PLATFORM_TEXT_LINE_BOTTOM) return;
    platform_text_output_line = line;
    platform_text_output_color = color & 0x0fu;
    platform_text_output_native(text);
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
