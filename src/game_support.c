#include "game.h"
#include "world.h"

void platform_text_output_native(const char* text);
uint8_t platform_text_output_color;
uint8_t platform_text_output_line;

/*
 * Count/select takeable (non-actor, non-PLATFORM_OBJECT_FLAG_NOT_TAKEABLE)
 * objects intersecting a tile. any_present, if non-null, is set to 1 if any
 * non-actor object intersects the tile regardless of takeability, so a
 * caller can tell "nothing here" from "something here, but not takeable"
 * even when the takeable count is zero.
 */
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

uint8_t game_inventory_has_one(uint8_t type) {
    return game_inventory_has(type, 1u);
}

void game_inventory_remove_one(uint8_t type) {
    (void)game_inventory_remove(type, 1u);
}

void game_inventory_add_one(uint8_t type) {
    (void)game_inventory_add(type, 1u);
}

/*
 * Locate an unlit Brazier (object type 41) at tile_x/tile_y and, if the
 * player carries a Lit Taper (type 53), swap it for a Lit Brazier (type
 * 40) - see rooms/asm/04_use_at.s and MOONFEN_STORY_DESIGN.md's "Lighting
 * the sconces". Returns GAME_USE_DEFAULT when no unlit brazier occupies
 * the tile. Was hand-written 6502 (src/brazier.s) purely because cc65's
 * codegen for this loop ran to ~500 bytes reloading the object pointer
 * from the software stack on every field access - oscar64 does not share
 * that weakness (Phase 0 measured ~2x denser codegen for comparable
 * loops), so this is ordinary C over the real room->objects array.
 */
uint8_t game_ignite_brazier(uint8_t tile_x, uint8_t tile_y) {
    uint8_t has_taper = game_inventory_has_one(53u); /* LIT_TAPER_TYPE */
    uint16_t i;
    uint16_t limit = platform_room_object_limit(&platform_room);

    for (i = 0u; i < limit; ++i) {
        PlatformObject* object = &platform_room.objects[i];
        if (object->type != 41u) continue; /* BRAZIER_UNLIT_TYPE */
        if ((object->x >> 1) != tile_x || (object->y >> 1) != tile_y) continue;

        if (has_taper) {
            object->type = 40u; /* BRAZIER_LIT_TYPE */
            (void)game_world_capture_current();
            platform_room_draw(&platform_room, platform_player);
            (void)game_room_script_entry(3u); /* STORY_ROOM04_LIGHT_BRAZIER */
        } else {
            (void)game_room_script_entry(4u); /* STORY_ROOM04_LIGHT_BRAZIER_NO_ITEM */
        }
        return GAME_USE_HANDLED;
    }
    return GAME_USE_DEFAULT;
}

extern uint8_t tile_data[];

/*
 * Recolors the moon mirror's two tiles (83, the frame half; 84, the glass
 * half - assets/tiles.ctil, placed only in room 04's tile grid, confirmed
 * by scanning for their shared cyan color) brighter once repaired -
 * MOONFEN_STORY_DESIGN.md's "The cracked moon mirror". Patches the shared
 * runtime tile_data table (src/assets_data.c) in place rather than adding
 * new tile IDs, since all 256 are already assigned; safe because no other
 * room places 83/84. Idempotent and cheap to call unconditionally - does
 * nothing unless the flag is set. Must be re-applied on every room 04
 * enter_room (rooms/c/04_enter_room.c), not just once at the moment of
 * repair: modules/saveload.c reuses tile_data as scratch space and
 * reloads it from ROM on every save/load, which would otherwise silently
 * revert the mirror's appearance after a save/load round trip even
 * though the flag (and the room's own object state) stayed correct. */
void game_mirror_repair_apply(void) {
    if (!game_state.flags[18]) return; /* STORY_STATE_MIRROR_REPAIRED */
    tile_data[83u * 8u + 1u] = 14u;
    tile_data[83u * 8u + 3u] = 14u;
    tile_data[83u * 8u + 5u] = 14u;
    tile_data[83u * 8u + 7u] = 14u;
    tile_data[84u * 8u + 1u] = 14u;
    tile_data[84u * 8u + 3u] = 14u;
    tile_data[84u * 8u + 5u] = 14u;
    tile_data[84u * 8u + 7u] = 14u;
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
