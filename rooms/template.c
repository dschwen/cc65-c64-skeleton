#include "game.h"

void enter_room(void) {
}

void enter_tile(void) {
}

uint8_t __fastcall__ look_at(uint8_t tile_x, uint8_t tile_y) {
    (void)tile_x;
    (void)tile_y;
    return GAME_LOOK_DEFAULT;
}
