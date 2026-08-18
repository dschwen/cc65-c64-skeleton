#include "game.h"

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
