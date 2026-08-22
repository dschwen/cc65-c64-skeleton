#include "game.h"
#include "story.h"

void enter_room(void) {
}

void enter_tile(void) {
    ++game_state.flags[STORY_STATE_ROOM_01_TILE_ENTRY_COUNT];
}

uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y) {
    (void)tile_x;
    (void)tile_y;
    return GAME_LOOK_DEFAULT;
}

uint8_t __fastcall__ use_at(uint8_t tile_x, uint8_t tile_y) {
    (void)tile_x;
    (void)tile_y;
    return GAME_USE_DEFAULT;
}
