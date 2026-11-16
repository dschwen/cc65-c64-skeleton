#include <string.h>

#include "game.h"
#include "world.h"
#include "memmap.h"
#include "banks.h"
#include "env_banks.h" /* generated into build/rooms by tools/generate_room_table.py */

void platform_easyflash_copy_roml(uint8_t bank, uint16_t offset, void* destination, uint16_t size);
void game_room_enter_tile_native(void);
uint8_t game_room_look_at_native(uint8_t tile_x, uint8_t tile_y);
void game_room_enter_room_native(void);
uint8_t game_room_use_at_native(uint8_t tile_x, uint8_t tile_y);
void raster_irq_suspend(void);
void raster_irq_resume(void);

GameState game_state;
uint8_t game_entry_reason;
uint8_t game_transition_pending_message;
uint8_t game_ending_pending;

void game_player_sync_from_platform(void) {
    game_state.current_room = platform_current_room;
    if (platform_player != 0) {
        game_state.player_type = platform_player->type;
        game_state.player_x = platform_player->x;
        game_state.player_y = platform_player->y;
    }
}

void game_state_init(void) {
    memset(&game_state, 0, sizeof(game_state));
    game_state.health = 100u;
    game_state.maximum_health = 100u;
    game_state.mana = 20u;
    game_state.maximum_mana = 20u;
    game_entry_reason = GAME_ENTRY_STARTUP;
    game_player_sync_from_platform();
    (void)game_player_light_sync();
}

void game_enter_tile(void) {
    game_room_enter_tile_native();
}

/* Fetches and activates the current room's environment module - a real
 * EasyFlash bank-copy into IRQ-reachable memory (env_init(), and the
 * fetch's own destination, are both live the moment the raster IRQ's next
 * tick runs). The caller must already be inside a raster_irq_suspend()/
 * platform_screen_blank() bracket covering the whole room switch. Unlike
 * the old generic resource-directory fetch, each environment module is
 * its own small EasyFlash bank (rooms/env/*.c) looked up directly by room
 * ID - no checksum needed, since the module is a compiled, linked part of
 * this same program rather than a separately-packed, runtime-validated
 * blob. */
void game_enter_room(void) {
    uint8_t bank;
    platform_rain_disable();
    /* Re-derive the hero's light radius from the current inventory (see
     * src/hero_light.c). Idempotent and cheap. */
    (void)game_player_light_sync();
    bank = env_bank_for_room(game_state.current_room);
    if (bank == 0xffu) {
        env_install_null();
    } else {
        platform_easyflash_copy_roml(bank, 0u, ENVCODE_BASE, ENVCODE_SIZE);
    }
    env_init();
    game_room_enter_room_native();
}

uint8_t game_look_at(uint8_t tile_x, uint8_t tile_y) {
    return game_room_look_at_native(tile_x, tile_y);
}

uint8_t game_use_at(uint8_t tile_x, uint8_t tile_y) {
    return game_room_use_at_native(tile_x, tile_y);
}

uint8_t game_look_exit(uint8_t edge_x, uint8_t edge_y, uint8_t direction) {
    uint8_t neighbor;
    if (platform_look_tile_check(&platform_room, platform_player,
                                 edge_x, edge_y, 1u) != PLATFORM_OK) {
        return 0u;
    }
    if (platform_room_neighbor(&platform_room, direction, &neighbor) != PLATFORM_OK) {
        game_text_write(PLATFORM_TEXT_LINE_TOP, "There is no exit that way.", 1u);
        return 0u;
    }
    if (game_room_script_entry(GAME_SCRIPT_EXIT_NORTH + direction)) return 1u;
    game_text_write(PLATFORM_TEXT_LINE_TOP, "An exit.", 1u);
    return 0u;
}

uint8_t game_player_step(int8_t delta_x, int8_t delta_y) {
    uint8_t old_room;
    uint8_t old_tile_x;
    uint8_t old_tile_y;
    uint8_t result;

    if (platform_player == 0) return PLATFORM_ERR_ARGUMENT;
    if (platform_player->x != game_state.player_x ||
        platform_player->y != game_state.player_y) {
        platform_object_move(&platform_room, platform_player,
                             game_state.player_x, game_state.player_y,
                             platform_player);
    }
    old_room = game_state.current_room;
    old_tile_x = game_state.player_x >> 1;
    old_tile_y = game_state.player_y >> 1;
    result = platform_player_step(delta_x, delta_y);
    if (result != PLATFORM_OK) return result;
    game_player_sync_from_platform();
    ++game_state.turn;
    if (old_room != game_state.current_room ||
        old_tile_x != (game_state.player_x >> 1) ||
        old_tile_y != (game_state.player_y >> 1)) {
        game_entry_reason = old_room == game_state.current_room
                                ? GAME_ENTRY_MOVEMENT : GAME_ENTRY_TRANSITION;
        if (old_room != game_state.current_room) game_enter_room();
        game_enter_tile();
    }
    return PLATFORM_OK;
}

/* Called directly by modules/script.c right when a room_transition/
 * room_transition_here opcode with a message operand runs - long before
 * the deferred transition itself is applied. Shows the message
 * immediately (blank, clear the map area, write, unblank) so it's already
 * up and stays up uninterrupted until the transition is actually
 * processed. */
void game_transition_show_message(const char* text) {
    platform_screen_blank();
    memset((void*)0xf800, platform_text_screen_code(' '), 920u);
    memset((void*)0xd800, 0, 920u);
    game_text_write(PLATFORM_TEXT_LINE_TOP, text, 1u);
    platform_screen_unblank();
    game_transition_pending_message = 1u;
}

/* Phase A of a transition bracket - call in place of a bare
 * platform_screen_blank(), before raster_irq_suspend(). */
void game_transition_message_show(void) {
    if (!game_transition_pending_message) platform_screen_blank();
}

/* Phase C of a transition bracket - call after raster_irq_resume(), in
 * place of a bare platform_room_draw()+platform_screen_unblank(). */
void game_transition_reveal(void) {
    if (game_transition_pending_message) {
        platform_wait_fresh_key();
    }
    platform_screen_blank();
    platform_room_draw(&platform_room, platform_player);
    platform_text_clear_line(PLATFORM_TEXT_LINE_TOP);
    platform_text_clear_line(PLATFORM_TEXT_LINE_BOTTOM);
    env_enable();
    platform_screen_unblank();
    game_transition_pending_message = 0u;
}

uint8_t game_process_pending_transition(void) {
    uint8_t room;
    uint8_t x;
    uint8_t y;
    uint8_t result;

    if (!game_state.pending_transition) return PLATFORM_OK;
    if (platform_player == 0) {
        game_state.pending_transition = 0u;
        return PLATFORM_ERR_ARGUMENT;
    }
    room = game_state.pending_room;
    x = game_state.pending_x;
    y = game_state.pending_y;
    game_state.pending_transition = 0u;
    game_transition_message_show();
    raster_irq_suspend();
    result = platform_room_enter(room, platform_player->type, x, y);
    if (result == PLATFORM_OK) {
        game_player_sync_from_platform();
        game_entry_reason = GAME_ENTRY_TRANSITION;
        game_enter_room();
        platform_sprites_hide_all();
        game_enter_tile();
    }
    raster_irq_resume();
    game_transition_reveal();
    if (result != PLATFORM_OK) {
        /* A blocked arrival tile, a full world-state journal, or a
         * destination with no free actor slot: the old room is back on
         * screen, so say why nothing happened rather than fail silently. */
        game_text_write(PLATFORM_TEXT_LINE_TOP, "You cannot go that way.", 1u);
    }
    return result;
}
