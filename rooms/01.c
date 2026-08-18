#include "game.h"

void enter_tile(void) {
    ++game_state.flags[1];
}

uint8_t __fastcall__ look_at(uint8_t direction) {
    (void)direction;
    return GAME_LOOK_DEFAULT;
}
