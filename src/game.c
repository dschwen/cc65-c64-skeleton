#include <string.h>

#include "game.h"
#include "world.h"

void game_room_enter_tile_native(void);
uint8_t __fastcall__ game_room_look_at_native(uint8_t tile_x, uint8_t tile_y);
void game_room_enter_room_native(void);
uint8_t __fastcall__ game_room_use_at_native(uint8_t tile_x, uint8_t tile_y);

#pragma bss-name (push, "GAMESTATE")
GameState game_state;
#pragma bss-name (pop)

uint8_t game_entry_reason;

#pragma code-name (push, "UPPERCODE")

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
}

void game_enter_tile(void) {
    game_room_enter_tile_native();
}

void game_enter_room(void) {
    platform_rain_disable();
    game_room_enter_room_native();
}

uint8_t __fastcall__ game_look_at(uint8_t tile_x, uint8_t tile_y) {
    return game_room_look_at_native(tile_x, tile_y);
}

uint8_t __fastcall__ game_use_at(uint8_t tile_x, uint8_t tile_y) {
    return game_room_use_at_native(tile_x, tile_y);
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
    result = platform_room_enter(room, platform_player->type, x, y);
    if (result == PLATFORM_OK) {
        game_player_sync_from_platform();
        game_entry_reason = GAME_ENTRY_TRANSITION;
        game_enter_room();
        game_enter_tile();
    }
    return result;
}

#pragma code-name (pop)
